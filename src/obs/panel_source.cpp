#include "obs/panel_source.hpp"

#include "core/league_catalog.hpp"
#include "core/replay.hpp"
#include "feed/details_feed.hpp"
#include "feed/match_feed.hpp"
#include "obs/match_picker.hpp"
#include "obs/services.hpp"
#include "render/panel_builder.hpp"
#include "render/panel_painter.hpp"
#include "ui/support_dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QImage>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace fscore::obs {
namespace {

using render::PanelContent;

// The 0.3.0 test build had one source with a content selector; it still loads,
// hidden from the Add menu.
constexpr const char *kLegacySourceId = "football_panel_source";
constexpr const char *kContentKey = "content";
constexpr const char *kViewKey = "view";
constexpr const char *kBallKey = "ball";
constexpr const char *kBallTestKey = "ball_test";
// How long the ball takes from one play to the next.
constexpr double kBallSeconds = 2.4;
constexpr const char *kRowsKey = "rows";
constexpr const char *kLegacyRowsKey = "table_rows";
constexpr const char *kBoardKey = "board";
constexpr const char *kWidthKey = "width";
constexpr const char *kDefaultLeague = "esp.1";
// The latest play's ring pulses this often; the frame is repainted ~15 times a second.
constexpr double kPulseSeconds = 1.6;
constexpr double kAnimationStep = 1.0 / 15.0;

// One OBS source type per panel.
struct PanelType {
	PanelContent content;
	const char *source_id;
	const char *name_key;
	const char *content_id; // for the legacy selector
	long long default_width;
	long long default_rows; // 0: no rows setting
};

const PanelType kTypes[] = {
	{PanelContent::LivePitch, "football_pitch_source", "Panel.Source.Pitch", "pitch", 960, 0},
	{PanelContent::Lineups, "football_lineups_source", "Panel.Source.Lineups", "lineups", 960, 0},
	{PanelContent::Stats, "football_stats_source", "Panel.Source.Stats", "stats", 800, 0},
	{PanelContent::Table, "football_table_source", "Panel.Source.Table", "table", 600, 10},
	{PanelContent::Feed, "football_feed_source", "Panel.Source.Feed", "feed", 520, 6},
	{PanelContent::Momentum, "football_momentum_source", "Panel.Source.Momentum", "momentum", 960, 0},
	{PanelContent::Timeline, "football_timeline_source", "Panel.Source.Timeline", "timeline", 560, 8},
	{PanelContent::Preview, "football_preview_source", "Panel.Source.Preview", "preview", 960, 0},
	{PanelContent::Scorers, "football_scorers_source", "Panel.Source.Scorers", "scorers", 560, 10},
	{PanelContent::Round, "football_round_source", "Panel.Source.Round", "round", 560, 8},
	{PanelContent::Goals, "football_goals_source", "Panel.Source.Goals", "goals", 960, 4},
};

const PanelType &type_of(PanelContent content)
{
	for (const PanelType &type : kTypes) {
		if (type.content == content)
			return type;
	}
	return kTypes[0];
}

PanelContent content_from_id(const std::string &id)
{
	for (const PanelType &type : kTypes) {
		if (id == type.content_id)
			return type.content;
	}
	return PanelContent::LivePitch;
}

bool uses_match(PanelContent c)
{
	return c != PanelContent::Table && c != PanelContent::Scorers && c != PanelContent::Round;
}

// The panels that go back in time with the dock's replays.
bool follows_replay(PanelContent c)
{
	return c == PanelContent::LivePitch || c == PanelContent::Feed || c == PanelContent::Timeline ||
	       c == PanelContent::Momentum;
}

bool has_view(PanelContent c)
{
	return c == PanelContent::LivePitch || c == PanelContent::Lineups;
}

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

struct PanelSource {
	obs_source_t *source = nullptr;
	const PanelType *fixed = nullptr; // null for the legacy selector source

	std::mutex update_mutex; // update (UI), tick (video)
	PanelContent content = PanelContent::LivePitch;
	render::PitchView view = render::PitchView::TopDown;
	std::string league;
	std::string match_id;
	LinkState link;
	size_t rows = 10;
	bool assists = false;
	std::unique_ptr<MatchFeed> feed;
	std::unique_ptr<DetailsFeed> details;

	FeedSnapshot snapshot;
	uint64_t seen_revision = std::numeric_limits<uint64_t>::max();
	DetailsFeed::Snapshot data;
	uint64_t seen_data = std::numeric_limits<uint64_t>::max();
	uint64_t seen_images = std::numeric_limits<uint64_t>::max();
	std::unordered_map<std::string, QImage> images;
	double time = 0, since_paint = 0;
	bool ball = true;        // draw the ball's route between plays
	int ball_sequence = -1;  // the latest play the ball went to
	double ball_started = -1e9;
	double ball_test_started = -1e9; // "test the ball": replays the route from this moment
	ReplayCursor replay;             // the dock's, as last read
	bool replay_restart = false;     // a button was pressed: the ball sets off again
	std::shared_ptr<const MatchDetails> replay_details; // the match cut at the replayed play
	uint64_t replay_details_data = 0, replay_details_revision = 0;
	bool dirty = true;

	std::atomic<uint32_t> width{960};
	std::atomic<uint32_t> height{810};

	std::mutex image_mutex;
	QImage frame;
	bool frame_dirty = false;

	gs_texture_t *texture = nullptr;
	uint32_t texture_width = 0, texture_height = 0;
};

uint32_t height_for(PanelContent content, uint32_t width, size_t rows)
{
	const QSize natural = render::panel_natural_size(content, rows);
	return static_cast<uint32_t>(std::clamp<long long>(
		std::llround(static_cast<double>(width) * natural.height() / natural.width()), 16, 4096));
}

// Caller holds update_mutex.
QImage image_for(PanelSource &self, const std::string &url)
{
	if (url.empty())
		return {};
	if (const auto it = self.images.find(url); it != self.images.end())
		return it->second;
	ImageStore *store = image_store();
	if (!store)
		return {};
	const auto bytes = store->get(url);
	if (!bytes)
		return {};
	QImage image = QImage::fromData(reinterpret_cast<const uchar *>(bytes->data()), static_cast<int>(bytes->size()));
	if (!image.isNull())
		image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	self.images[url] = image;
	return image;
}

render::PanelLabels labels()
{
	render::PanelLabels l;
	l.pressure = text("Panel.Pressure");
	l.no_plays = text("Panel.NoPlays");
	l.goal = text("Panel.Play.Goal");
	l.shot_on = text("Panel.Play.ShotOn");
	l.shot_off = text("Panel.Play.ShotOff");
	l.shot_blocked = text("Panel.Play.ShotBlocked");
	l.woodwork = text("Panel.Play.Woodwork");
	l.corner = text("Panel.Play.Corner");
	l.foul = text("Panel.Play.Foul");
	l.offside = text("Panel.Play.Offside");
	l.possession = text("Panel.Stat.Possession");
	l.shots = text("Panel.Stat.Shots");
	l.shots_on_target = text("Panel.Stat.ShotsOnTarget");
	l.corners = text("Panel.Stat.Corners");
	l.fouls = text("Panel.Stat.Fouls");
	l.offsides = text("Panel.Stat.Offsides");
	l.saves = text("Panel.Stat.Saves");
	l.passes = text("Panel.Stat.Passes");
	l.yellow_cards = text("Panel.Stat.YellowCards");
	const char *headers[6] = {"Panel.Table.Played", "Panel.Table.Won", "Panel.Table.Drawn",
				  "Panel.Table.Lost", "Panel.Table.GoalDiff", "Panel.Table.Points"};
	for (int i = 0; i < 6; ++i)
		l.table_header[i] = text(headers[i]);
	l.momentum = text("Panel.Momentum");
	l.yellow_card = text("Panel.Play.YellowCard");
	l.red_card = text("Panel.Play.RedCard");
	l.substitution = text("Panel.Play.Substitution");
	l.sub_off = text("Panel.Play.SubOff");
	l.form = text("Panel.Preview.Form");
	l.meetings = text("Panel.Preview.Meetings");
	l.meetings_leads = text("Panel.Preview.Leads");
	l.meetings_tied = text("Panel.Preview.Tied");
	l.venue_attendance = text("Panel.Preview.Venue");
	const QString results = text("Panel.Preview.Results"); // "GEP" / "WDL"
	l.win = results.size() > 0 ? results[0] : QChar('W');
	l.draw = results.size() > 1 ? results[1] : QChar('D');
	l.loss = results.size() > 2 ? results[2] : QChar('L');
	l.scorers = text("Panel.Scorers.Goals");
	l.assists = text("Panel.Scorers.Assists");
	l.goals_header = text("Panel.Scorers.GoalsHeader");
	l.assists_header = text("Panel.Scorers.AssistsHeader");
	l.matches_header = text("Panel.Table.Played");
	l.round_title = text("Panel.Round.Title");
	l.spanish = QString::fromUtf8(obs_get_locale()).startsWith(QStringLiteral("es"));
	return l;
}

QString status_text(const Match &match)
{
	const QLocale locale = obs_locale();
	switch (match.state) {
	case MatchState::Live:
		return match.clock.empty() ? text("Football.Status.Live") : QString::fromStdString(match.clock);
	case MatchState::HalfTime:
		return text("Football.Status.HalfTime");
	case MatchState::Interrupted:
		return text("Football.Status.Interrupted");
	case MatchState::Finished:
		return text("Football.Status.FullTime");
	case MatchState::Postponed:
		return text("Football.Status.Postponed");
	case MatchState::Scheduled:
		return day_label(match.start_utc, locale) + QStringLiteral(" · ") + kickoff_time(match.start_utc, locale);
	}
	return {};
}

// The catalog league a match belongs to: the chosen one, or its own with "all leagues".
std::string league_of(const PanelSource &self, const std::optional<Match> &match)
{
	if (self.league != kAllLeagues)
		return self.league;
	if (!match)
		return {};
	const League *own = find_league_by_espn_id(match->league_id);
	return own ? own->slug : std::string();
}

QString league_name(const std::string &slug)
{
	const League *league = find_league(slug);
	return QString::fromStdString(league ? league->name : slug);
}

QString waiting_message(const PanelSource &self)
{
	switch (self.snapshot.status) {
	case FeedStatus::NoMatches:
		return text("Football.Message.NoMatches");
	case FeedStatus::NotFound:
		return text("Football.Message.NotFound");
	case FeedStatus::Error:
		return text("Football.Message.Error");
	default:
		return text("Football.Message.Loading");
	}
}

// The route the test uses when the match has no plays yet: out from the back, across
// midfield and into the box.
const PitchPoint kDemoRoute[] = {{0.12, 0.55}, {0.30, 0.22}, {0.52, 0.48}, {0.70, 0.78}, {0.86, 0.40}, {0.955, 0.50}};

bool ball_test_running(const PanelSource &self, size_t stops)
{
	return stops >= 2 && self.time - self.ball_test_started < kBallSeconds * double(stops - 1);
}

// "Test the ball": the ball goes over the route stop by stop, whatever the match state.
// Caller holds update_mutex.
void apply_ball_test(const PanelSource &self, render::PanelView &view)
{
	std::vector<PitchPoint> route = view.ball_route;
	if (route.size() < 2)
		route.assign(std::begin(kDemoRoute), std::end(kDemoRoute));
	if (!ball_test_running(self, route.size()))
		return;
	const double at = (self.time - self.ball_test_started) / kBallSeconds;
	const size_t leg = static_cast<size_t>(at);
	route.resize(leg + 2);
	view.ball_route = std::move(route);
	view.ball_progress = at - double(leg);
}

// The match as it stood at the replayed play. Caller holds update_mutex.
const MatchDetails &replayed_details(PanelSource &self)
{
	if (!self.replay_details || self.replay_details_data != self.data.revision ||
	    self.replay_details_revision != self.replay.revision) {
		self.replay_details =
			std::make_shared<const MatchDetails>(details_until(*self.data.details, self.replay.sequence));
		self.replay_details_data = self.data.revision;
		self.replay_details_revision = self.replay.revision;
	}
	return *self.replay_details;
}

// Caller holds update_mutex.
render::PanelView build_view(PanelSource &self)
{
	render::PanelView view;
	view.content = self.content;
	view.pitch_view = self.view;
	if (self.league.empty()) {
		view.message = text("Football.Message.ChooseLeague");
		return view;
	}
	const std::optional<Match> &match = self.snapshot.match;
	const Match *match_ptr = match ? &*match : nullptr;
	auto lookup = [&self](const std::string &url) { return image_for(self, url); };
	const render::PanelLabels l = labels();
	const std::string own_league = league_of(self, match);

	// League panels: no match needed.
	switch (self.content) {
	case PanelContent::Table: {
		if (!self.data.standings || self.data.standings->empty()) {
			view.message = own_league.empty() && self.league == kAllLeagues ? waiting_message(self)
											 : text("Football.Message.Loading");
			return view;
		}
		const StandingsGroup *group = group_for_teams(*self.data.standings, match ? match->home.id : "",
							      match ? match->away.id : "");
		if (!group) {
			view.message = text("Panel.NoTable");
			return view;
		}
		view.has_data = true;
		render::fill_table(view, *group, match_ptr, self.rows, l, lookup);
		view.rows = self.rows;
		return view;
	}
	case PanelContent::Scorers:
		if (!self.data.leaders) {
			view.message = text("Football.Message.Loading");
			return view;
		}
		if ((self.assists ? self.data.leaders->assists : self.data.leaders->goals).empty()) {
			view.message = text("Panel.NoScorers");
			return view;
		}
		view.has_data = true;
		render::fill_scorers(view, *self.data.leaders, self.assists, match_ptr, self.rows, league_name(own_league), l,
				     lookup);
		return view;
	case PanelContent::Round:
		if (!self.data.round) {
			view.message = text("Football.Message.Loading");
			return view;
		}
		if (self.data.round->matches.empty()) {
			view.message = text("Panel.NoRound");
			return view;
		}
		view.has_data = true;
		render::fill_round(
			view, *self.data.round, match_ptr, self.rows, league_name(own_league),
			[](const Match &m) { return status_text(m); },
			[](const Match &m) {
				return m.started() ? score_text(m) : kickoff_time(m.start_utc, obs_locale());
			},
			l, lookup);
		return view;
	default:
		break;
	}

	if (!match) {
		view.message = waiting_message(self);
		return view;
	}
	view.has_data = true;
	render::fill_teams(view, *match, lookup);
	view.status = status_text(*match);
	view.live = match->in_progress();
	view.blink = !view.live || std::fmod(self.time, 1.0) < 0.6 ? 1.0 : 0.25;
	view.pulse = std::fmod(self.time, kPulseSeconds) / kPulseSeconds;
	view.pressure_label = l.pressure;
	view.no_plays = l.no_plays;
	// Advertising boards of the perspective stadium.
	view.table_title = league_name(own_league).toUpper() + QStringLiteral("   ·   AC FOOTBALL SCORES");
	// The goals come with the scoreboard: no match summary needed.
	if (self.content == PanelContent::Goals) {
		for (const GoalInfo &goal : match->goals) {
			QString line = QString::fromStdString(goal.scorer);
			if (goal.own_goal)
				line += QStringLiteral(" ") + text("Football.OwnGoal");
			else if (goal.penalty)
				line += QStringLiteral(" ") + text("Football.Penalty");
			if (!goal.minute.empty())
				line += QStringLiteral("  ") + QString::fromStdString(goal.minute);
			(goal.team_id == match->away.id ? view.away_goals : view.home_goals).push_back(line.trimmed());
		}
		view.no_goals = match->started() ? text("Panel.NoGoals") : text("Panel.NotStarted");
		view.rows = self.rows;
		return view;
	}
	if (!self.data.details) {
		if (self.content != PanelContent::LivePitch) {
			view.has_data = false;
			view.message = text("Football.Message.Loading");
		} else {
			apply_ball_test(self, view);
		}
		return view;
	}
	const bool replay = follows_replay(self.content) && self.replay.replaying(match->id) &&
			    find_play(self.data.details->plays, self.replay.sequence) != nullptr;
	const MatchDetails &d = replay ? replayed_details(self) : *self.data.details;
	switch (self.content) {
	case PanelContent::LivePitch:
		render::fill_live_pitch(view, d, l);
		// A replay always shows the ball, the setting is for the live match.
		if (!self.ball && !replay) {
			view.ball_route.clear();
		} else {
			// A new play: the ball sets off towards it. Not on the first load.
			if (view.ball_sequence != self.ball_sequence) {
				self.ball_started = self.ball_sequence < 0 ? -1e9 : self.time;
				self.ball_sequence = view.ball_sequence;
			}
			// A dock button: off again, even towards the same play ("repeat").
			if (self.replay_restart) {
				self.ball_started = self.time;
				self.ball_sequence = view.ball_sequence;
			}
			const double seconds = kBallSeconds / (replay ? self.replay.speed : 1.0);
			view.ball_progress = std::clamp((self.time - self.ball_started) / seconds, 0.0, 1.0);
		}
		self.replay_restart = false;
		apply_ball_test(self, view);
		if (match->state == MatchState::Scheduled)
			view.momentum.clear();
		break;
	case PanelContent::Lineups:
		render::fill_lineups(view, d);
		if (view.players.empty()) {
			view.has_data = false;
			view.message = text("Panel.NoLineups");
		}
		break;
	case PanelContent::Stats:
		render::fill_stats(view, d, l);
		if (view.stats.empty()) {
			view.has_data = false;
			view.message = text("Panel.NoStats");
		}
		break;
	case PanelContent::Feed:
		render::fill_feed(view, d, self.rows, l);
		if (view.feed.empty()) {
			view.has_data = false;
			view.message = text("Panel.NoFeed");
		}
		break;
	case PanelContent::Momentum:
		render::fill_momentum(view, d, *match, l);
		break;
	case PanelContent::Timeline:
		render::fill_timeline(view, d, self.rows, l);
		if (view.timeline.empty()) {
			view.has_data = false;
			view.message = text("Panel.NoTimeline");
		}
		break;
	case PanelContent::Preview:
		render::fill_preview(view, d, l, lookup);
		break;
	default:
		break;
	}
	if (replay) {
		const Play *on = find_play(d.plays, self.replay.sequence);
		// The score as it stood then, when the plays account for the whole score (an own
		// goal may be filed under the other team: then the live score stays).
		auto goals = [](const std::vector<Play> &plays, int &home, int &away) {
			for (const Play &play : plays) {
				if (play.kind == PlayKind::Goal && play.side)
					++(*play.side == TeamSide::Home ? home : away);
			}
		};
		int home_all = 0, away_all = 0, home_then = 0, away_then = 0;
		goals(self.data.details->plays, home_all, away_all);
		goals(d.plays, home_then, away_then);
		if (match->started() && match->home.score.value_or(-1) == home_all &&
		    match->away.score.value_or(-1) == away_all) {
			view.home.score = QString::number(home_then);
			view.away.score = QString::number(away_then);
		}
		if (self.content == PanelContent::Momentum)
			view.now_minute = on && on->clock_seconds >= 0 ? on->clock_seconds / 60.0 : -1;
		if (self.replay.badge) {
			view.replay = true;
			view.status = text("Replay.Badge");
			if (on && !on->minute.empty())
				view.status += QStringLiteral(" · ") + QString::fromStdString(on->minute);
		}
	}
	return view;
}

// Caller holds update_mutex.
void repaint_locked(PanelSource &self)
{
	const render::PanelView view = build_view(self);
	QImage image = render::paint_panel(view, QSize(int(self.width), int(self.height)));
	std::lock_guard lock(self.image_mutex);
	self.frame = std::move(image);
	self.frame_dirty = true;
}

// ---------------------------------------------------------------------------
// Source callbacks

void panel_update(void *data, obs_data_t *settings)
{
	auto *self = static_cast<PanelSource *>(data);
	std::unique_ptr<MatchFeed> old_feed;
	{
		std::lock_guard lock(self->update_mutex);
		self->content = self->fixed ? self->fixed->content
					    : content_from_id(obs_data_get_string(settings, kContentKey));
		const PanelType &type = type_of(self->content);
		const Selection chosen =
			sync_on_update(settings, self->link, {self->league, self->match_id}, true);
		const std::string &league = chosen.league;
		const std::string &match_id = chosen.match;
		self->ball = obs_data_get_bool(settings, kBallKey);
		self->view = std::string(obs_data_get_string(settings, kViewKey)) == "perspective"
				     ? render::PitchView::Perspective
				     : render::PitchView::TopDown;
		long long rows = obs_data_get_int(settings, kRowsKey);
		if (!self->fixed && obs_data_has_user_value(settings, kLegacyRowsKey))
			rows = obs_data_get_int(settings, kLegacyRowsKey);
		self->rows = static_cast<size_t>(std::clamp<long long>(rows > 0 ? rows : std::max(type.default_rows, 1LL), 1, 40));
		self->assists = std::string(obs_data_get_string(settings, kBoardKey)) == "assists";
		const long long width = obs_data_get_int(settings, kWidthKey);
		self->width = static_cast<uint32_t>(std::clamp(width > 0 ? width : type.default_width, 160LL, 4096LL));
		self->height = height_for(self->content, self->width, type.default_rows ? self->rows : 0);
		if (!self->feed || league != self->league || match_id != self->match_id) {
			self->league = league;
			self->match_id = match_id;
			old_feed = std::move(self->feed);
			if (!league.empty())
				self->feed = std::make_unique<MatchFeed>(FeedConfig{league, match_id}, http_get(), now_seconds);
			self->snapshot = FeedSnapshot{};
			self->seen_revision = std::numeric_limits<uint64_t>::max();
			self->ball_sequence = -1; // another match: no flight from the old one
		}
		self->dirty = true;
	}
	old_feed.reset();
}

void panel_tick(void *data, float seconds)
{
	auto *self = static_cast<PanelSource *>(data);
	std::lock_guard lock(self->update_mutex);
	self->time += seconds;
	self->since_paint += seconds;
	sync_on_tick(self->source, self->link, {self->league, self->match_id}, true);

	if (self->feed) {
		FeedSnapshot snapshot = self->feed->snapshot();
		if (snapshot.revision != self->seen_revision) {
			self->seen_revision = snapshot.revision;
			self->snapshot = std::move(snapshot);
			self->dirty = true;
		}
	}

	// What the details feed follows depends on the panel.
	DetailsTarget target;
	const std::string own = league_of(*self, self->snapshot.match);
	// The goals panel reads the scoreboard alone, no summary to download.
	if (uses_match(self->content) && self->content != PanelContent::Goals && self->snapshot.match) {
		const Match &match = *self->snapshot.match;
		target.summary_league = own.empty() ? self->league : own;
		target.event_id = match.id;
		target.live = match.in_progress() ||
			      (match.state == MatchState::Scheduled && match.start_utc - now_seconds() < 600);
	}
	if (self->content == PanelContent::Table)
		target.standings_league = own;
	if (self->content == PanelContent::Scorers)
		target.leaders_league = own;
	if (self->content == PanelContent::Round)
		target.round_league = own;
	self->details->set_target(target);
	DetailsFeed::Snapshot fresh = self->details->snapshot();
	if (fresh.revision != self->seen_data) {
		self->seen_data = fresh.revision;
		self->data = std::move(fresh);
		self->dirty = true;
	}

	// The dock's replays.
	const ReplayCursor cursor = replay_channel().get();
	if (cursor.revision != self->replay.revision || cursor.speed != self->replay.speed ||
	    cursor.badge != self->replay.badge) {
		if (cursor.revision != self->replay.revision)
			self->replay_restart = true;
		self->replay = cursor;
		self->dirty = true;
	}
	const bool replaying = self->snapshot.match && follows_replay(self->content) &&
			       self->replay.replaying(self->snapshot.match->id);

	if (ImageStore *store = image_store()) {
		const uint64_t generation = store->generation();
		if (generation != self->seen_images) {
			self->seen_images = generation;
			self->dirty = true;
		}
	}

	// The live pitch and the blinking dots animate while the match is on.
	// The ball test runs over at most the demo route's stops; keep painting while it may.
	const bool testing = self->content == PanelContent::LivePitch &&
			     ball_test_running(*self, std::size(kDemoRoute));
	const bool animated = testing || (replaying && self->content == PanelContent::LivePitch) ||
			      (self->snapshot.match && self->snapshot.match->in_progress() &&
					  uses_match(self->content));
	if (animated && self->since_paint >= kAnimationStep)
		self->dirty = true;

	if (self->dirty) {
		self->dirty = false;
		self->since_paint = 0;
		repaint_locked(*self);
	}
}

void upload_if_dirty(PanelSource &self)
{
	std::lock_guard lock(self.image_mutex);
	if (!self.frame_dirty || self.frame.isNull())
		return;
	self.frame_dirty = false;
	const auto width = static_cast<uint32_t>(self.frame.width());
	const auto height = static_cast<uint32_t>(self.frame.height());
	const uint8_t *bits = self.frame.constBits();
	const auto line_size = static_cast<uint32_t>(self.frame.bytesPerLine());
	if (self.texture && self.texture_width == width && self.texture_height == height) {
		gs_texture_set_image(self.texture, bits, line_size, false);
		return;
	}
	gs_texture_destroy(self.texture);
	self.texture = gs_texture_create(width, height, GS_BGRA, 1, &bits, GS_DYNAMIC);
	self.texture_width = self.texture ? width : 0;
	self.texture_height = self.texture ? height : 0;
}

void panel_render(void *data, gs_effect_t *effect)
{
	auto *self = static_cast<PanelSource *>(data);
	upload_if_dirty(*self);
	if (!self->texture)
		return;
	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), self->texture);
	gs_draw_sprite(self->texture, 0, self->texture_width, self->texture_height);
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);
}

uint32_t panel_width(void *data)
{
	return static_cast<PanelSource *>(data)->width;
}

uint32_t panel_height(void *data)
{
	return static_cast<PanelSource *>(data)->height;
}

void *panel_create(obs_data_t *settings, obs_source_t *source)
{
	auto *self = new PanelSource();
	self->source = source;
	self->fixed = static_cast<const PanelType *>(obs_source_get_type_data(source));
	self->details = std::make_unique<DetailsFeed>(http_get(), now_seconds);
	panel_update(self, settings);
	return self;
}

void panel_destroy(void *data)
{
	auto *self = static_cast<PanelSource *>(data);
	std::unique_ptr<MatchFeed> feed;
	std::unique_ptr<DetailsFeed> details;
	{
		std::lock_guard lock(self->update_mutex);
		feed = std::move(self->feed);
		details = std::move(self->details);
	}
	feed.reset();
	details.reset(); // joins the workers
	obs_enter_graphics();
	gs_texture_destroy(self->texture);
	obs_leave_graphics();
	delete self;
}

void panel_defaults(void *type_data, obs_data_t *settings)
{
	const PanelType *type = static_cast<const PanelType *>(type_data);
	const PanelType &t = type ? *type : kTypes[0];
	obs_data_set_default_string(settings, kContentKey, "pitch");
	obs_data_set_default_string(settings, kLeagueKey, kDefaultLeague);
	obs_data_set_default_bool(settings, kLinkedKey, true);
	obs_data_set_default_string(settings, kMatchKey, "");
	obs_data_set_default_string(settings, kViewKey, "topdown");
	obs_data_set_default_bool(settings, kBallKey, true);
	obs_data_set_default_string(settings, kBoardKey, "goals");
	obs_data_set_default_int(settings, kRowsKey, t.default_rows);
	obs_data_set_default_int(settings, kWidthKey, t.default_width);
}

// The legacy selector shows the settings of the chosen panel only.
bool content_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const PanelContent content = content_from_id(obs_data_get_string(settings, kContentKey));
	obs_property_set_visible(obs_properties_get(props, kViewKey), has_view(content));
	obs_property_set_visible(obs_properties_get(props, kBallKey), content == PanelContent::LivePitch);
	if (obs_property_t *test = obs_properties_get(props, kBallTestKey))
		obs_property_set_visible(test, content == PanelContent::LivePitch);
	obs_property_set_visible(obs_properties_get(props, kRowsKey), type_of(content).default_rows > 0);
	obs_property_set_visible(obs_properties_get(props, kBoardKey), content == PanelContent::Scorers);
	return true;
}

bool ball_test_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	if (auto *self = static_cast<PanelSource *>(data)) {
		std::lock_guard lock(self->update_mutex);
		self->ball_test_started = self->time;
		self->dirty = true;
	}
	return false;
}

bool support_clicked(obs_properties_t *, obs_property_t *, void *)
{
	ui::run_support_dialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
	return false;
}

obs_properties_t *panel_properties(void *data, void *type_data)
{
	auto *self = static_cast<PanelSource *>(data);
	const PanelType *fixed = static_cast<const PanelType *>(type_data);
	std::string league = kDefaultLeague;
	std::string selected;
	if (self) {
		obs_data_t *settings = obs_source_get_settings(self->source);
		league = obs_data_get_string(settings, kLeagueKey);
		selected = obs_data_get_string(settings, kMatchKey);
		obs_data_release(settings);
	}
	obs_properties_t *props = obs_properties_create();
	if (!fixed) {
		obs_property_t *content = obs_properties_add_list(props, kContentKey, obs_module_text("Panel.Content"),
								  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		for (const PanelType &type : kTypes)
			obs_property_list_add_string(content, obs_module_text(type.name_key), type.content_id);
		obs_property_set_modified_callback(content, content_changed);
	}

	add_league_and_match(props, league, selected);
	if (fixed && !uses_match(fixed->content)) {
		// League panels: the match only picks which teams to highlight.
		obs_property_set_long_description(obs_properties_get(props, kMatchKey),
						  obs_module_text("Panel.MatchHighlight"));
	}

	if (!fixed || has_view(fixed->content)) {
		obs_property_t *view = obs_properties_add_list(props, kViewKey, obs_module_text("Panel.View"),
							       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(view, obs_module_text("Panel.View.TopDown"), "topdown");
		obs_property_list_add_string(view, obs_module_text("Panel.View.Perspective"), "perspective");
	}
	if (!fixed || fixed->content == PanelContent::LivePitch) {
		obs_property_t *ball = obs_properties_add_bool(props, kBallKey, obs_module_text("Panel.Ball"));
		obs_property_set_long_description(ball, obs_module_text("Panel.Ball.Help"));
		if (self)
			obs_properties_add_button2(props, kBallTestKey, obs_module_text("Panel.Ball.Test"), ball_test_clicked,
						   self);
	}
	if (!fixed || fixed->content == PanelContent::Scorers) {
		obs_property_t *board = obs_properties_add_list(props, kBoardKey, obs_module_text("Panel.Scorers.Board"),
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(board, obs_module_text("Panel.Scorers.Goals"), "goals");
		obs_property_list_add_string(board, obs_module_text("Panel.Scorers.Assists"), "assists");
	}
	if (!fixed || fixed->default_rows > 0)
		obs_properties_add_int(props, kRowsKey, obs_module_text("Panel.Rows"), 3, 30, 1);
	obs_properties_add_int(props, kWidthKey, obs_module_text("Football.Width"), 160, 4096, 10);
	if (!fixed || fixed->content == PanelContent::LivePitch)
		obs_properties_add_text(props, "panel_help", obs_module_text("Panel.Help"), OBS_TEXT_INFO);
	const std::string support = std::string("☕ ") + obs_module_text("Support.Link");
	obs_properties_add_button2(props, "support", support.c_str(), support_clicked, nullptr);
	return props;
}

const char *panel_name(void *type_data)
{
	const PanelType *type = static_cast<const PanelType *>(type_data);
	return obs_module_text(type ? type->name_key : "Panel.Source");
}

void register_one(const char *id, const PanelType *type, uint32_t caps)
{
	obs_source_info info = {};
	info.id = id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB | caps;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	info.type_data = const_cast<PanelType *>(type);
	info.get_name = panel_name;
	info.create = panel_create;
	info.destroy = panel_destroy;
	info.update = panel_update;
	info.get_defaults2 = panel_defaults;
	info.get_properties2 = panel_properties;
	info.get_width = panel_width;
	info.get_height = panel_height;
	info.video_tick = panel_tick;
	info.video_render = panel_render;
	obs_register_source(&info);
}

} // namespace

void register_panel_source()
{
	for (const PanelType &type : kTypes)
		register_one(type.source_id, &type, 0);
	register_one(kLegacySourceId, nullptr, OBS_SOURCE_CAP_DISABLED);
}

} // namespace fscore::obs
