#include "obs/replay_dock.hpp"

#include "core/league_catalog.hpp"
#include "core/narration.hpp"
#include "core/replay.hpp"
#include "feed/details_feed.hpp"
#include "feed/match_feed.hpp"
#include "obs/match_picker.hpp"
#include "obs/services.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs-hotkey.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fscore::obs {
namespace {

constexpr const char *kDockId = "football_replay_dock";
constexpr const char *kKofiUrl = "https://ko-fi.com/alejoxcode";
constexpr const char *kOptionsFile = "replay-dock.json";
constexpr int kPollMs = 500;
// An automatic goal replay goes back to the live match after this long.
constexpr int kAutoReturnMs = 12000;

const QColor kAmber(245, 165, 36);
const QColor kRed(225, 29, 46);

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

bool spanish()
{
	return QString::fromUtf8(obs_get_locale()).startsWith(QStringLiteral("es"));
}

struct HotkeyDef {
	const char *name;
	const char *text_key;
	ReplayStep step;
};

const HotkeyDef kHotkeys[] = {
	{"football_replay_first", "Replay.Hotkey.First", ReplayStep::First},
	{"football_replay_previous", "Replay.Hotkey.Previous", ReplayStep::Previous},
	{"football_replay_repeat", "Replay.Hotkey.Repeat", ReplayStep::Repeat},
	{"football_replay_next", "Replay.Hotkey.Next", ReplayStep::Next},
	{"football_replay_live", "Replay.Hotkey.Live", ReplayStep::Live},
};

// Minutes into the match: from the clock, else from "67'" / "90'+3'".
double play_minute(const Play &play)
{
	if (play.clock_seconds >= 0)
		return play.clock_seconds / 60.0;
	double minute = 0, extra = 0;
	size_t i = 0;
	const std::string &m = play.minute;
	while (i < m.size() && m[i] >= '0' && m[i] <= '9')
		minute = minute * 10 + (m[i++] - '0');
	const size_t plus = m.find('+', i);
	if (plus != std::string::npos) {
		for (size_t j = plus + 1; j < m.size() && m[j] >= '0' && m[j] <= '9'; ++j)
			extra = extra * 10 + (m[j] - '0');
	}
	return minute + extra;
}

QString kind_word(PlayKind kind)
{
	switch (kind) {
	case PlayKind::Goal:
		return text("Panel.Play.Goal");
	case PlayKind::ShotOnTarget:
		return text("Panel.Play.ShotOn");
	case PlayKind::ShotOffTarget:
		return text("Panel.Play.ShotOff");
	case PlayKind::ShotBlocked:
		return text("Panel.Play.ShotBlocked");
	case PlayKind::Woodwork:
		return text("Panel.Play.Woodwork");
	case PlayKind::Corner:
		return text("Panel.Play.Corner");
	case PlayKind::Foul:
		return text("Panel.Play.Foul");
	case PlayKind::Offside:
		return text("Panel.Play.Offside");
	case PlayKind::YellowCard:
		return text("Panel.Play.YellowCard");
	case PlayKind::RedCard:
		return text("Panel.Play.RedCard");
	case PlayKind::Substitution:
		return text("Panel.Play.Substitution");
	case PlayKind::Other:
		break;
	}
	return {};
}

QString kind_symbol(PlayKind kind)
{
	switch (kind) {
	case PlayKind::Goal:
		return QStringLiteral("⚽");
	case PlayKind::ShotOnTarget:
	case PlayKind::ShotOffTarget:
	case PlayKind::ShotBlocked:
	case PlayKind::Woodwork:
		return QStringLiteral("🎯");
	case PlayKind::Corner:
		return QStringLiteral("🚩");
	case PlayKind::YellowCard:
		return QStringLiteral("🟨");
	case PlayKind::RedCard:
		return QStringLiteral("🟥");
	default:
		return QStringLiteral("•");
	}
}

QColor team_color(const Team &team, QColor fallback)
{
	if (!team.color)
		return fallback;
	const QColor c = QColor::fromRgb(*team.color);
	// Near-white and near-black kits vanish on the dock: keep the fallback.
	return c.lightness() > 235 || c.lightness() < 25 ? fallback : c;
}

QString pill_style(const QColor &background, const QColor &ink)
{
	return QStringLiteral("QLabel { background-color: %1; color: %2; border-radius: 9px; padding: 2px 9px; "
			      "font-weight: bold; }")
		.arg(background.name(), ink.name());
}

// The match's minute line with its goals and cards; a click picks the nearest play.
class TimelineBar : public QWidget {
public:
	struct Mark {
		double minute = 0;
		PlayKind kind = PlayKind::Other;
		bool home = true;
	};

	std::function<void(double)> clicked;

	explicit TimelineBar(QWidget *parent) : QWidget(parent)
	{
		setMinimumHeight(34);
		setCursor(Qt::PointingHandCursor);
		setToolTip(text("Replay.Timeline.Help"));
	}

	void set(std::vector<Mark> marks, double end, double cursor, bool replay, QColor home, QColor away)
	{
		marks_ = std::move(marks);
		end_ = std::max(90.0, end);
		cursor_ = cursor;
		replay_ = replay;
		home_ = home;
		away_ = away;
		update();
	}

protected:
	QSize sizeHint() const override { return {260, 34}; }

	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF line = track();
		const QColor base = palette().color(QPalette::Mid);
		p.setPen(Qt::NoPen);
		p.setBrush(base);
		p.drawRoundedRect(QRectF(line.left(), line.center().y() - 2, line.width(), 4), 2, 2);
		// Half time.
		p.setBrush(palette().color(QPalette::WindowText));
		p.setOpacity(0.35);
		p.drawRect(QRectF(x_of(45) - 0.5, line.top() + 4, 1, line.height() - 8));
		p.setOpacity(1);
		if (cursor_ >= 0) {
			p.setBrush(replay_ ? kAmber : kRed);
			p.setOpacity(0.5);
			p.drawRoundedRect(QRectF(line.left(), line.center().y() - 2, x_of(cursor_) - line.left(), 4), 2, 2);
			p.setOpacity(1);
		}
		for (const Mark &m : marks_) {
			const qreal x = x_of(m.minute);
			const qreal y = line.center().y();
			if (m.kind == PlayKind::Goal) {
				p.setPen(QPen(Qt::white, 1.5));
				p.setBrush(m.home ? home_ : away_);
				p.drawEllipse(QPointF(x, y), 6, 6);
			} else {
				p.setPen(Qt::NoPen);
				p.setBrush(m.kind == PlayKind::RedCard ? QColor(220, 38, 38) : QColor(250, 204, 21));
				const qreal dy = m.home ? -11 : 4;
				p.drawRoundedRect(QRectF(x - 3, y + dy, 6, 8), 1, 1);
			}
		}
		if (cursor_ >= 0) {
			const qreal x = x_of(cursor_);
			p.setPen(QPen(palette().color(QPalette::Window), 2));
			p.setBrush(replay_ ? kAmber : kRed);
			p.drawEllipse(QPointF(x, line.center().y()), 5, 5);
		}
	}

	void mousePressEvent(QMouseEvent *event) override
	{
		if (!clicked || event->button() != Qt::LeftButton)
			return;
		const QRectF line = track();
		const double at = (event->position().x() - line.left()) / std::max(1.0, line.width()) * end_;
		clicked(std::clamp(at, 0.0, end_));
	}

private:
	QRectF track() const { return QRectF(8, 0, std::max(10, width() - 16), height()); }
	qreal x_of(double minute) const
	{
		const QRectF line = track();
		return line.left() + line.width() * std::clamp(minute / end_, 0.0, 1.0);
	}

	std::vector<Mark> marks_;
	double end_ = 90, cursor_ = -1;
	bool replay_ = false;
	QColor home_ = QColor(37, 99, 235), away_ = QColor(220, 38, 38);
};

class ReplayDock : public QWidget {
public:
	explicit ReplayDock(QWidget *parent);

	void go(ReplayStep step);
	void shutdown();

private:
	QToolButton *transport(QStyle::StandardPixmap icon, const char *tip_key, ReplayStep step);
	void poll();
	void follow(const Selection &selection);
	void show_play(int sequence, bool automatic);
	void rebuild_list();
	void refresh_status();
	void refresh_bar();
	QPixmap crest(const std::string &url);
	const MatchDetails *details() const { return data_.details.get(); }
	std::string event_id() const { return snapshot_.match ? snapshot_.match->id : std::string(); }
	void load_options();
	void save_options() const;
	void publish_options() const;

	QWidget *body_ = nullptr;
	QLabel *message_ = nullptr;
	QLabel *home_crest_ = nullptr, *away_crest_ = nullptr, *score_ = nullptr, *status_ = nullptr;
	TimelineBar *bar_ = nullptr;
	QListWidget *list_ = nullptr;
	QButtonGroup *filters_ = nullptr;
	QComboBox *speed_ = nullptr;
	QCheckBox *auto_goals_ = nullptr, *badge_ = nullptr;
	QTimer *timer_ = nullptr, *auto_return_ = nullptr;

	Selection following_;
	uint64_t seen_selection_ = std::numeric_limits<uint64_t>::max();
	std::unique_ptr<MatchFeed> feed_;
	std::unique_ptr<DetailsFeed> details_feed_;
	FeedSnapshot snapshot_;
	uint64_t seen_feed_ = std::numeric_limits<uint64_t>::max();
	DetailsFeed::Snapshot data_;
	uint64_t seen_data_ = std::numeric_limits<uint64_t>::max();
	uint64_t seen_images_ = std::numeric_limits<uint64_t>::max();
	std::string shown_event_;
	ReplayFilter filter_ = ReplayFilter::All;
	int current_ = -1; // the play on replay, -1 live
	GoalReplayWatch goals_;
	uint64_t auto_revision_ = 0; // the cursor revision of the automatic replay running
	std::unordered_map<std::string, QPixmap> crests_;
	bool stopped_ = false;
};

ReplayDock::ReplayDock(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(260);
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(8, 8, 8, 8);
	outer->setSpacing(8);

	message_ = new QLabel(text("Replay.NoMatch"), this);
	message_->setWordWrap(true);
	message_->setAlignment(Qt::AlignCenter);
	message_->setMargin(12);
	outer->addWidget(message_);

	body_ = new QWidget(this);
	auto *layout = new QVBoxLayout(body_);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);
	outer->addWidget(body_, 1);

	// Score and status.
	auto *score_row = new QHBoxLayout();
	score_row->setSpacing(6);
	home_crest_ = new QLabel(body_);
	away_crest_ = new QLabel(body_);
	for (QLabel *crest : {home_crest_, away_crest_})
		crest->setFixedSize(24, 24);
	score_ = new QLabel(body_);
	QFont score_font = score_->font();
	score_font.setBold(true);
	score_font.setPointSizeF(score_font.pointSizeF() * 1.25);
	score_->setFont(score_font);
	status_ = new QLabel(body_);
	score_row->addWidget(home_crest_);
	score_row->addWidget(score_);
	score_row->addWidget(away_crest_);
	score_row->addStretch(1);
	score_row->addWidget(status_);
	layout->addLayout(score_row);

	// Transport.
	auto *buttons = new QHBoxLayout();
	buttons->setSpacing(4);
	buttons->addWidget(transport(QStyle::SP_MediaSkipBackward, "Replay.First", ReplayStep::First));
	buttons->addWidget(transport(QStyle::SP_MediaSeekBackward, "Replay.Previous", ReplayStep::Previous));
	QToolButton *repeat = transport(QStyle::SP_BrowserReload, "Replay.Repeat", ReplayStep::Repeat);
	repeat->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	repeat->setText(text("Replay.RepeatShort"));
	repeat->setStyleSheet(QStringLiteral("QToolButton { background-color: %1; color: #1B1D24; font-weight: bold; "
					     "border-radius: 6px; padding: 4px 10px; }")
				      .arg(kAmber.name()));
	buttons->addWidget(repeat, 1);
	buttons->addWidget(transport(QStyle::SP_MediaSeekForward, "Replay.Next", ReplayStep::Next));
	auto *live = new QToolButton(body_);
	live->setText(QStringLiteral("● ") + text("Replay.LiveShort"));
	live->setToolTip(text("Replay.Live"));
	live->setMinimumHeight(30);
	live->setStyleSheet(QStringLiteral("QToolButton { background-color: %1; color: white; font-weight: bold; "
					   "border-radius: 6px; padding: 4px 10px; }")
				    .arg(kRed.name()));
	connect(live, &QToolButton::clicked, this, [this] { go(ReplayStep::Live); });
	buttons->addWidget(live);
	layout->addLayout(buttons);

	bar_ = new TimelineBar(body_);
	bar_->clicked = [this](double minute) {
		const MatchDetails *d = details();
		if (!d)
			return;
		int best = -1;
		double distance = 1e9;
		for (const int sequence : replay_list(d->plays, filter_)) {
			const Play *play = find_play(d->plays, sequence);
			const double off = std::abs(play_minute(*play) - minute);
			if (off < distance) {
				distance = off;
				best = sequence;
			}
		}
		if (best >= 0)
			show_play(best, false);
	};
	layout->addWidget(bar_);

	// Filters.
	auto *filter_row = new QHBoxLayout();
	filter_row->setSpacing(4);
	filters_ = new QButtonGroup(this);
	filters_->setExclusive(true);
	const char *filter_keys[] = {"Replay.Filter.All", "Replay.Filter.Goals", "Replay.Filter.Shots",
				     "Replay.Filter.Cards"};
	for (int i = 0; i < 4; ++i) {
		auto *button = new QPushButton(text(filter_keys[i]), body_);
		button->setCheckable(true);
		button->setCursor(Qt::PointingHandCursor);
		filters_->addButton(button, i);
		filter_row->addWidget(button);
	}
	connect(filters_, &QButtonGroup::idClicked, this, [this](int id) {
		filter_ = static_cast<ReplayFilter>(id);
		rebuild_list();
		refresh_bar();
		save_options();
	});
	layout->addLayout(filter_row);

	list_ = new QListWidget(body_);
	list_->setWordWrap(true);
	list_->setUniformItemSizes(false);
	list_->setTextElideMode(Qt::ElideNone);
	list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	list_->setSpacing(1);
	connect(list_, &QListWidget::itemClicked, this,
		[this](QListWidgetItem *item) { show_play(item->data(Qt::UserRole).toInt(), false); });
	layout->addWidget(list_, 1);

	// Options.
	auto *speed_row = new QHBoxLayout();
	speed_row->addWidget(new QLabel(text("Replay.Speed"), body_));
	speed_ = new QComboBox(body_);
	speed_->addItem(text("Replay.Speed.Slow"), 0.5);
	speed_->addItem(text("Replay.Speed.Normal"), 1.0);
	speed_->addItem(text("Replay.Speed.Fast"), 2.0);
	speed_row->addWidget(speed_, 1);
	layout->addLayout(speed_row);
	auto_goals_ = new QCheckBox(text("Replay.AutoGoals"), body_);
	auto_goals_->setToolTip(text("Replay.AutoGoals.Help"));
	badge_ = new QCheckBox(text("Replay.ShowBadge"), body_);
	layout->addWidget(auto_goals_);
	layout->addWidget(badge_);
	auto *hint = new QLabel(text("Replay.Hint"), body_);
	hint->setWordWrap(true);
	QFont small = hint->font();
	small.setPointSizeF(small.pointSizeF() * 0.9);
	hint->setFont(small);
	hint->setEnabled(false);
	layout->addWidget(hint);

	auto *kofi = new QPushButton(QStringLiteral("☕  ") + text("Replay.Kofi"), this);
	kofi->setCursor(Qt::PointingHandCursor);
	kofi->setMinimumHeight(30);
	kofi->setToolTip(text("Support.Title"));
	kofi->setStyleSheet("QPushButton { background-color: #FF5E5B; color: white; font-weight: bold; border: none; "
			    "border-radius: 6px; padding: 5px 12px; } QPushButton:hover { background-color: #FF7472; }");
	connect(kofi, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kKofiUrl))); });
	outer->addWidget(kofi);

	load_options();
	connect(speed_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
		publish_options();
		save_options();
	});
	connect(badge_, &QCheckBox::toggled, this, [this](bool) {
		publish_options();
		save_options();
	});
	connect(auto_goals_, &QCheckBox::toggled, this, [this](bool) { save_options(); });
	publish_options();

	auto_return_ = new QTimer(this);
	auto_return_->setSingleShot(true);
	connect(auto_return_, &QTimer::timeout, this, [this] {
		// Back to live unless someone pressed a button meanwhile.
		if (replay_channel().get().revision == auto_revision_)
			show_play(-1, true);
	});
	timer_ = new QTimer(this);
	connect(timer_, &QTimer::timeout, this, [this] { poll(); });
	timer_->start(kPollMs);
	body_->setVisible(false);
	poll();
}

QToolButton *ReplayDock::transport(QStyle::StandardPixmap icon, const char *tip_key, ReplayStep step)
{
	auto *button = new QToolButton(body_);
	button->setIcon(style()->standardIcon(icon));
	button->setToolTip(text(tip_key));
	button->setMinimumSize(30, 30);
	button->setCursor(Qt::PointingHandCursor);
	connect(button, &QToolButton::clicked, this, [this, step] { go(step); });
	return button;
}

void ReplayDock::go(ReplayStep step)
{
	if (stopped_)
		return;
	const MatchDetails *d = details();
	const int next = d ? replay_step(d->plays, filter_, current_, step) : -1;
	show_play(next, false);
}

void ReplayDock::show_play(int sequence, bool automatic)
{
	const std::string event = event_id();
	if (event.empty())
		return;
	if (!automatic)
		auto_return_->stop();
	current_ = sequence;
	replay_channel().show(event, sequence);
	if (automatic && sequence >= 0) {
		auto_revision_ = replay_channel().get().revision;
		auto_return_->start(kAutoReturnMs);
	}
	refresh_status();
	refresh_bar();
	for (int row = 0; row < list_->count(); ++row) {
		QListWidgetItem *item = list_->item(row);
		if (item->data(Qt::UserRole).toInt() == sequence && sequence >= 0) {
			list_->setCurrentItem(item);
			list_->scrollToItem(item);
			return;
		}
	}
	list_->clearSelection();
	list_->setCurrentItem(nullptr);
}

void ReplayDock::follow(const Selection &selection)
{
	// Leave the old match live before following another one.
	if (current_ >= 0 && !shown_event_.empty())
		replay_channel().show(shown_event_, -1);
	current_ = -1;
	auto_return_->stop();
	following_ = selection;
	feed_.reset();
	details_feed_.reset();
	snapshot_ = FeedSnapshot{};
	data_ = DetailsFeed::Snapshot{};
	seen_feed_ = seen_data_ = std::numeric_limits<uint64_t>::max();
	if (!selection.league.empty()) {
		feed_ = std::make_unique<MatchFeed>(FeedConfig{selection.league, selection.match}, http_get(), now_seconds);
		details_feed_ = std::make_unique<DetailsFeed>(http_get(), now_seconds);
	}
}

void ReplayDock::poll()
{
	if (stopped_)
		return;
	const SharedSelection::Value shared = shared_selection().get();
	if (shared.revision != seen_selection_) {
		seen_selection_ = shared.revision;
		if (shared.revision != 0 && shared.selection != following_)
			follow(shared.selection);
	}
	bool changed = false;
	if (feed_) {
		FeedSnapshot snapshot = feed_->snapshot();
		if (snapshot.revision != seen_feed_) {
			seen_feed_ = snapshot.revision;
			snapshot_ = std::move(snapshot);
			changed = true;
		}
	}
	const std::string event = event_id();
	if (event != shown_event_) {
		// Automatic mode moved on to another match.
		if (current_ >= 0 && !shown_event_.empty())
			replay_channel().show(shown_event_, -1);
		current_ = -1;
		shown_event_ = event;
		data_ = DetailsFeed::Snapshot{};
		seen_data_ = std::numeric_limits<uint64_t>::max();
		changed = true;
	}
	if (details_feed_) {
		DetailsTarget target;
		if (snapshot_.match) {
			const Match &match = *snapshot_.match;
			std::string league = following_.league;
			if (league == kAllLeagues) {
				const League *own = find_league_by_espn_id(match.league_id);
				league = own ? own->slug : std::string();
			}
			target.summary_league = league.empty() ? following_.league : league;
			target.event_id = match.id;
			target.live = match.in_progress();
		}
		details_feed_->set_target(target);
		DetailsFeed::Snapshot fresh = details_feed_->snapshot();
		if (fresh.revision != seen_data_) {
			seen_data_ = fresh.revision;
			data_ = std::move(fresh);
			changed = true;
			if (const MatchDetails *d = details()) {
				// The dock's own play is gone from a corrected summary: back to live.
				if (current_ >= 0 && !find_play(d->plays, current_))
					show_play(-1, true);
				const std::optional<int> goal = goals_.update(event, d->plays);
				const bool live = snapshot_.match && snapshot_.match->in_progress();
				if (goal && live && auto_goals_->isChecked() && current_ < 0)
					show_play(*goal, true);
			}
		}
	}
	if (ImageStore *store = image_store()) {
		if (store->generation() != seen_images_) {
			seen_images_ = store->generation();
			changed = true;
		}
	}
	if (!changed)
		return;

	const bool has_match = snapshot_.match.has_value();
	message_->setVisible(!has_match);
	body_->setVisible(has_match);
	if (!has_match) {
		message_->setText(following_.league.empty() ? text("Replay.NoMatch")
				  : snapshot_.status == FeedStatus::Loading ? text("Football.Message.Loading")
									  : text("Football.Message.NoMatches"));
		return;
	}
	const Match &match = *snapshot_.match;
	home_crest_->setPixmap(crest(match.home.logo_url));
	away_crest_->setPixmap(crest(match.away.logo_url));
	const QString score = match.started() ? QStringLiteral("%1  %2 - %3  %4")
							.arg(QString::fromStdString(match.home.abbreviation))
							.arg(match.home.score.value_or(0))
							.arg(match.away.score.value_or(0))
							.arg(QString::fromStdString(match.away.abbreviation))
					      : QStringLiteral("%1  vs  %2")
							.arg(QString::fromStdString(match.home.abbreviation),
							     QString::fromStdString(match.away.abbreviation));
	score_->setText(score);
	score_->setToolTip(QString::fromStdString(match.home.name + " - " + match.away.name));
	rebuild_list();
	refresh_status();
	refresh_bar();
}

QPixmap ReplayDock::crest(const std::string &url)
{
	if (url.empty())
		return {};
	if (const auto it = crests_.find(url); it != crests_.end())
		return it->second;
	ImageStore *store = image_store();
	if (!store)
		return {};
	const auto bytes = store->get(url);
	if (!bytes)
		return {};
	QPixmap pixmap;
	pixmap.loadFromData(reinterpret_cast<const uchar *>(bytes->data()), static_cast<uint>(bytes->size()));
	const qreal ratio = devicePixelRatioF();
	if (!pixmap.isNull()) {
		pixmap = pixmap.scaled(QSize(24, 24) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		pixmap.setDevicePixelRatio(ratio);
	}
	crests_[url] = pixmap;
	return pixmap;
}

void ReplayDock::refresh_status()
{
	if (!snapshot_.match)
		return;
	const Match &match = *snapshot_.match;
	const MatchDetails *d = details();
	const Play *on = d && current_ >= 0 ? find_play(d->plays, current_) : nullptr;
	if (on) {
		QString label = text("Replay.Badge");
		if (!on->minute.empty())
			label += QStringLiteral(" · ") + QString::fromStdString(on->minute);
		status_->setText(label);
		status_->setStyleSheet(pill_style(kAmber, QColor(27, 29, 36)));
		return;
	}
	QString label;
	switch (match.state) {
	case MatchState::Live:
		label = QStringLiteral("● ") + text("Football.Status.Live");
		if (!match.clock.empty())
			label += QStringLiteral(" ") + QString::fromStdString(match.clock);
		break;
	case MatchState::HalfTime:
		label = text("Football.Status.HalfTime");
		break;
	case MatchState::Interrupted:
		label = text("Football.Status.Interrupted");
		break;
	case MatchState::Finished:
		label = text("Football.Status.FullTime");
		break;
	case MatchState::Postponed:
		label = text("Football.Status.Postponed");
		break;
	case MatchState::Scheduled:
		label = day_label(match.start_utc, obs_locale()) + QStringLiteral(" ") +
			kickoff_time(match.start_utc, obs_locale());
		break;
	}
	status_->setText(label);
	status_->setStyleSheet(match.in_progress() ? pill_style(kRed, Qt::white)
						   : pill_style(QColor(72, 78, 94), Qt::white));
}

void ReplayDock::refresh_bar()
{
	if (!snapshot_.match)
		return;
	const Match &match = *snapshot_.match;
	std::vector<TimelineBar::Mark> marks;
	double end = 90, cursor = -1;
	const MatchDetails *d = details();
	if (d) {
		for (const Play &play : d->plays) {
			const double minute = play_minute(play);
			end = std::max(end, minute);
			if (play.kind != PlayKind::Goal && play.kind != PlayKind::YellowCard && play.kind != PlayKind::RedCard)
				continue;
			marks.push_back({minute, play.kind, !play.side || *play.side == TeamSide::Home});
		}
		if (const Play *on = current_ >= 0 ? find_play(d->plays, current_) : nullptr)
			cursor = play_minute(*on);
	}
	if (cursor < 0 && match.in_progress()) {
		Play clock;
		clock.minute = match.clock;
		cursor = play_minute(clock);
	} else if (cursor < 0 && match.state == MatchState::Finished) {
		cursor = end;
	}
	bar_->set(std::move(marks), end, cursor, current_ >= 0, team_color(match.home, QColor(37, 99, 235)),
		  team_color(match.away, QColor(220, 38, 38)));
}

void ReplayDock::rebuild_list()
{
	if (filters_->checkedId() != static_cast<int>(filter_)) {
		if (QAbstractButton *button = filters_->button(static_cast<int>(filter_)))
			button->setChecked(true);
	}
	const int scroll = list_->verticalScrollBar()->value();
	list_->clear();
	const MatchDetails *d = details();
	if (!d || !snapshot_.match) {
		auto *item = new QListWidgetItem(text("Football.Message.Loading"), list_);
		item->setFlags(Qt::NoItemFlags);
		return;
	}
	const Match &match = *snapshot_.match;
	const bool es = spanish();
	const std::vector<int> sequences = replay_list(d->plays, filter_);
	if (sequences.empty()) {
		auto *item = new QListWidgetItem(text(match.started() ? "Replay.NoPlays" : "Panel.NotStarted"), list_);
		item->setFlags(Qt::NoItemFlags);
		return;
	}
	QListWidgetItem *selected = nullptr;
	// Newest first: the latest play is the one people want back.
	for (auto it = sequences.rbegin(); it != sequences.rend(); ++it) {
		const Play *play = find_play(d->plays, *it);
		const bool home = !play->side || *play->side == TeamSide::Home;
		QString head = QString::fromStdString(play->minute) + QStringLiteral("  ") + kind_symbol(play->kind) +
			       QStringLiteral(" ") + kind_word(play->kind).toUpper();
		if (play->side)
			head += QStringLiteral(" · ") +
				QString::fromStdString(home ? match.home.abbreviation : match.away.abbreviation);
		std::string sentence = play->text;
		for (const CommentaryLine &line : d->commentary) {
			if (line.sequence == play->sequence) {
				sentence = line.text;
				break;
			}
		}
		if (es) {
			if (auto told = narrate_es(sentence))
				sentence = *told;
		}
		QString body = QString::fromStdString(sentence);
		if (body.isEmpty())
			body = QString::fromStdString(play->player);
		auto *item = new QListWidgetItem(head + QStringLiteral("\n") + body, list_);
		item->setData(Qt::UserRole, play->sequence);
		item->setToolTip(QString::fromStdString(sentence));
		if (play->kind == PlayKind::Goal) {
			QFont bold = item->font();
			bold.setBold(true);
			item->setFont(bold);
		}
		if (play->sequence == current_)
			selected = item;
	}
	if (selected)
		list_->setCurrentItem(selected);
	list_->verticalScrollBar()->setValue(scroll);
}

void ReplayDock::load_options()
{
	char *path = obs_module_config_path(kOptionsFile);
	obs_data_t *data = path ? obs_data_create_from_json_file_safe(path, "bak") : nullptr;
	bfree(path);
	double speed = 1.0;
	bool auto_goals = true, badge = true;
	int filter = 0;
	if (data) {
		obs_data_set_default_double(data, "speed", 1.0);
		obs_data_set_default_bool(data, "auto_goals", true);
		obs_data_set_default_bool(data, "badge", true);
		speed = obs_data_get_double(data, "speed");
		auto_goals = obs_data_get_bool(data, "auto_goals");
		badge = obs_data_get_bool(data, "badge");
		filter = static_cast<int>(std::clamp<long long>(obs_data_get_int(data, "filter"), 0, 3));
		obs_data_release(data);
	}
	int index = speed_->findData(speed);
	speed_->setCurrentIndex(index >= 0 ? index : 1);
	auto_goals_->setChecked(auto_goals);
	badge_->setChecked(badge);
	filter_ = static_cast<ReplayFilter>(filter);
	if (QAbstractButton *button = filters_->button(filter))
		button->setChecked(true);
}

void ReplayDock::save_options() const
{
	char *dir = obs_module_config_path("");
	if (dir)
		os_mkdirs(dir);
	bfree(dir);
	char *path = obs_module_config_path(kOptionsFile);
	if (!path)
		return;
	obs_data_t *data = obs_data_create();
	obs_data_set_double(data, "speed", speed_->currentData().toDouble());
	obs_data_set_bool(data, "auto_goals", auto_goals_->isChecked());
	obs_data_set_bool(data, "badge", badge_->isChecked());
	obs_data_set_int(data, "filter", static_cast<int>(filter_));
	obs_data_save_json_safe(data, path, "tmp", "bak");
	obs_data_release(data);
	bfree(path);
}

void ReplayDock::publish_options() const
{
	replay_channel().set_options(speed_->currentData().toDouble(), badge_->isChecked());
}

void ReplayDock::shutdown()
{
	stopped_ = true;
	timer_->stop();
	auto_return_->stop();
	if (current_ >= 0 && !shown_event_.empty())
		replay_channel().show(shown_event_, -1);
	feed_.reset(); // joins the workers
	details_feed_.reset();
}

// ---------------------------------------------------------------------------
// Dock and hotkeys

std::mutex dock_mutex; // the hotkey thread reads `dock`
ReplayDock *dock = nullptr;
obs_hotkey_id hotkey_ids[std::size(kHotkeys)];

void hotkey_pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const ReplayStep step = static_cast<const HotkeyDef *>(data)->step;
	std::lock_guard lock(dock_mutex);
	if (!dock)
		return;
	QPointer<ReplayDock> target(dock);
	QMetaObject::invokeMethod(
		dock,
		[target, step] {
			if (target)
				target->go(step);
		},
		Qt::QueuedConnection);
}

// The hotkeys' keys live in the scene collection, like OBS's own.
void save_hotkeys(obs_data_t *save_data, bool saving, void *)
{
	for (size_t i = 0; i < std::size(kHotkeys); ++i) {
		if (hotkey_ids[i] == OBS_INVALID_HOTKEY_ID)
			continue;
		if (saving) {
			obs_data_array_t *keys = obs_hotkey_save(hotkey_ids[i]);
			obs_data_set_array(save_data, kHotkeys[i].name, keys);
			obs_data_array_release(keys);
		} else {
			obs_data_array_t *keys = obs_data_get_array(save_data, kHotkeys[i].name);
			if (keys) {
				obs_hotkey_load(hotkey_ids[i], keys);
				obs_data_array_release(keys);
			}
		}
	}
}

bool hotkeys_registered = false;

} // namespace

void create_replay_dock()
{
	auto *main_window = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!main_window)
		return; // OBS without its window (headless): no dock
	auto *widget = new ReplayDock(main_window);
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Replay.DockTitle"), widget)) {
		obs_log(LOG_WARNING, "could not add the replay dock");
		widget->shutdown();
		delete widget;
		return;
	}
	{
		std::lock_guard lock(dock_mutex);
		dock = widget;
	}
	for (size_t i = 0; i < std::size(kHotkeys); ++i) {
		hotkey_ids[i] = obs_hotkey_register_frontend(kHotkeys[i].name, obs_module_text(kHotkeys[i].text_key),
							     hotkey_pressed, const_cast<HotkeyDef *>(&kHotkeys[i]));
	}
	obs_frontend_add_save_callback(save_hotkeys, nullptr);
	hotkeys_registered = true;
}

void shutdown_replay_dock()
{
	if (hotkeys_registered) {
		obs_frontend_remove_save_callback(save_hotkeys, nullptr);
		for (obs_hotkey_id &id : hotkey_ids) {
			if (id != OBS_INVALID_HOTKEY_ID)
				obs_hotkey_unregister(id);
			id = OBS_INVALID_HOTKEY_ID;
		}
		hotkeys_registered = false;
	}
	ReplayDock *widget = nullptr;
	{
		std::lock_guard lock(dock_mutex);
		widget = dock;
		dock = nullptr;
	}
	if (widget)
		widget->shutdown(); // OBS deletes the widget with its dock
}

} // namespace fscore::obs
