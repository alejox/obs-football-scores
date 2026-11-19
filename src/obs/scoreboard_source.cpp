#include "obs/scoreboard_source.hpp"

#include "core/board_design.hpp"
#include "core/goal_animation.hpp"
#include "core/goal_watch.hpp"
#include "core/league_catalog.hpp"
#include "core/match.hpp"
#include "core/match_selection.hpp"
#include "core/style_preset.hpp"
#include "feed/match_feed.hpp"
#include "obs/services.hpp"
#include "render/scoreboard_painter.hpp"
#include "ui/support_dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QImage>
#include <QLocale>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace fscore::obs {
namespace {

constexpr const char *kSourceId = "football_scoreboard_source";

constexpr const char *kLeagueKey = "league";
constexpr const char *kMatchKey = "match";
constexpr const char *kRefreshKey = "refresh_matches";
constexpr const char *kNamesKey = "team_names";
constexpr const char *kShowLeagueKey = "show_league";
constexpr const char *kGoalAnimationKey = "goal_animation";
constexpr const char *kTestGoalKey = "test_goal";
constexpr const char *kStyleKey = "style";
constexpr const char *kDesignKey = "design";
constexpr const char *kAccentKey = "accent_color";
constexpr const char *kTextColorKey = "text_color";
constexpr const char *kWidthKey = "width";
constexpr const char *kHeightKey = "height";
constexpr const char *kTestHotkeyName = "football_scores.test_goal";

constexpr const char *kDefaultLeague = "esp.1";
constexpr BoardDesign kDefaultDesign = BoardDesign::Stadium;
constexpr long long kDefaultWidth = 960;
constexpr long long kDefaultHeight = 120;
// OBS colors are 0xAABBGGRR.
constexpr long long kDefaultAccent = 0xFF6E3DFF; // #FF3D6E
constexpr long long kDefaultText = 0xFFFFFFFF;

enum class NameMode { Full, Short, Abbreviation };

NameMode name_mode_from_id(const std::string &id)
{
	if (id == "short")
		return NameMode::Short;
	if (id == "abbreviation")
		return NameMode::Abbreviation;
	return NameMode::Full;
}

struct ScoreboardSource {
	obs_source_t *source = nullptr;

	// Guards everything below up to the graphics hand-off; taken by update
	// (UI thread), video_tick (video thread) and the hotkey thread.
	std::mutex update_mutex;
	std::string league;
	std::string match_id;
	NameMode names = NameMode::Full;
	bool goal_animation = true;
	render::BoardStyle style;
	std::unique_ptr<MatchFeed> feed;

	// Video thread state, also under update_mutex.
	FeedSnapshot snapshot;
	uint64_t seen_revision = std::numeric_limits<uint64_t>::max();
	GoalWatch goal_watch;
	std::optional<GoalEvent> goal;
	double goal_elapsed = 0.0;
	double time = 0.0;
	bool blink_on = true;
	uint64_t seen_images = std::numeric_limits<uint64_t>::max();
	std::unordered_map<std::string, QImage> images; // decoded crests and flags, by URL
	bool dirty = true;
	std::atomic<bool> test_goal_requested{false};

	std::atomic<uint32_t> width{static_cast<uint32_t>(kDefaultWidth)};
	std::atomic<uint32_t> height{static_cast<uint32_t>(kDefaultHeight)};

	// Hand-off to the graphics thread.
	std::mutex image_mutex;
	QImage frame;
	bool frame_dirty = false;

	// Graphics thread only.
	gs_texture_t *texture = nullptr;
	uint32_t texture_width = 0;
	uint32_t texture_height = 0;

	obs_hotkey_id test_hotkey = OBS_INVALID_HOTKEY_ID;
};

QColor color_from_obs(long long value)
{
	const auto color = static_cast<uint32_t>(value);
	return QColor(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, (color >> 24) & 0xFF);
}

uint32_t read_dimension(obs_data_t *settings, const char *key, long long fallback)
{
	const long long value = obs_data_get_int(settings, key);
	return static_cast<uint32_t>(std::clamp(value > 0 ? value : fallback, 16LL, 4096LL));
}

QLocale obs_locale()
{
	QString name = QString::fromUtf8(obs_get_locale());
	name.replace('-', '_');
	return QLocale(name);
}

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// ---------------------------------------------------------------------------
// View model

QString team_name(const Team &team, NameMode mode)
{
	std::string name;
	switch (mode) {
	case NameMode::Full:
		name = team.name;
		break;
	case NameMode::Short:
		name = team.short_name;
		break;
	case NameMode::Abbreviation:
		name = team.abbreviation;
		break;
	}
	if (name.empty())
		name = team.name;
	return QString::fromStdString(name);
}

QColor team_color(const Team &team)
{
	if (!team.color)
		return {};
	const uint32_t rgb = *team.color;
	return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// "TODAY", "TOMORROW" or "SAT 22 NOV", in local time.
QString day_label(int64_t start_utc, const QLocale &locale)
{
	const QDate day = QDateTime::fromSecsSinceEpoch(start_utc).toLocalTime().date();
	const QDate today = QDate::currentDate();
	if (day == today)
		return text("Football.Status.Today");
	if (day == today.addDays(1))
		return text("Football.Status.Tomorrow");
	return locale.toString(day, QStringLiteral("ddd d MMM")).remove('.').toUpper();
}

QString kickoff_time(int64_t start_utc, const QLocale &locale)
{
	return locale.toString(QDateTime::fromSecsSinceEpoch(start_utc).toLocalTime().time(), QStringLiteral("HH:mm"));
}

QString score_text(const Match &match)
{
	return QStringLiteral("%1 - %2").arg(match.home.score.value_or(0)).arg(match.away.score.value_or(0));
}

void fill_center_and_status(const Match &match, render::ScoreView &view, const QLocale &locale)
{
	switch (match.state) {
	case MatchState::Scheduled:
		view.center = kickoff_time(match.start_utc, locale);
		view.status = day_label(match.start_utc, locale);
		break;
	case MatchState::Live:
		view.center = score_text(match);
		view.status = match.clock.empty() ? text("Football.Status.Live") : QString::fromStdString(match.clock);
		view.live = true;
		break;
	case MatchState::HalfTime:
		view.center = score_text(match);
		view.status = text("Football.Status.HalfTime");
		view.live = true;
		break;
	case MatchState::Finished:
		view.center = score_text(match);
		view.status = text("Football.Status.FullTime");
		break;
	case MatchState::Postponed:
		view.center = QStringLiteral("vs");
		view.status = text("Football.Status.Postponed");
		break;
	}
}

QString goal_detail(const GoalEvent &goal)
{
	QString detail = QString::fromStdString(goal.scorer);
	if (detail.isEmpty())
		return {};
	if (goal.own_goal)
		detail += QStringLiteral(" ") + text("Football.OwnGoal");
	else if (goal.penalty)
		detail += QStringLiteral(" ") + text("Football.Penalty");
	if (!goal.minute.empty())
		detail += QStringLiteral(" ") + QString::fromStdString(goal.minute);
	return detail;
}

// The decoded image for `url`, or a null image while it downloads.
// Caller holds update_mutex.
QImage image_for(ScoreboardSource &self, const std::string &url)
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
	if (image.isNull())
		obs_log(LOG_WARNING, "could not decode image %s", url.c_str());
	else
		image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	self.images[url] = image; // a broken image is not decoded again
	return image;
}

// Caller holds update_mutex.
render::ScoreView build_view(ScoreboardSource &self)
{
	render::ScoreView view;
	const std::optional<Match> &match = self.snapshot.match;
	// The match's own league first: with "all leagues" each match has a different one.
	const League *league = match ? find_league_by_espn_id(match->league_id) : nullptr;
	if (!league)
		league = find_league(self.league);
	// The catalog's short name ("LaLiga") reads better than the feed's ("Spanish LALIGA").
	if (league) {
		view.league_name = QString::fromStdString(league->name);
		view.league_logo = image_for(self, league->logo);
		view.league_flag = image_for(self, country_flag_url(league->country));
	} else if (self.league != kAllLeagues) {
		view.league_name = QString::fromStdString(self.snapshot.league_name);
		view.league_logo = image_for(self, self.snapshot.league_logo_url);
	} else if (match) {
		view.league_name = QString::fromStdString(match->competition); // a league outside the catalog
	}

	if (self.league.empty()) {
		view.message = text("Football.Message.ChooseLeague");
		return view;
	}
	if (!match) {
		switch (self.snapshot.status) {
		case FeedStatus::Loading:
		case FeedStatus::Ok:
			view.message = text("Football.Message.Loading");
			break;
		case FeedStatus::NoMatches:
			view.message = text("Football.Message.NoMatches");
			break;
		case FeedStatus::NotFound:
			view.message = text("Football.Message.NotFound");
			break;
		case FeedStatus::Error:
			view.message = text("Football.Message.Error");
			break;
		}
		return view;
	}

	view.has_match = true;
	view.home = {team_name(match->home, self.names), team_name(match->home, NameMode::Abbreviation),
		     image_for(self, match->home.logo_url), team_color(match->home)};
	view.away = {team_name(match->away, self.names), team_name(match->away, NameMode::Abbreviation),
		     image_for(self, match->away.logo_url), team_color(match->away)};
	view.has_score = match->started();
	view.score_home = QString::number(match->home.score.value_or(0));
	view.score_away = QString::number(match->away.score.value_or(0));
	fill_center_and_status(*match, view, obs_locale());
	view.blink = self.blink_on ? 1.0 : 0.25;

	if (self.goal) {
		view.goal = goal_animation_frame(self.goal_elapsed);
		view.goal_home = self.goal->side == Side::Home;
		view.goal_title = text("Football.Goal");
		view.goal_detail = goal_detail(*self.goal);
		view.goal_photo = image_for(self, self.goal->scorer_photo_url);
	}
	return view;
}

// Paints the current state and hands the frame to the graphics thread.
// Caller holds update_mutex.
void repaint_locked(ScoreboardSource &self)
{
	const render::ScoreView view = build_view(self);
	QImage image = render::ScoreboardPainter::paint(view, self.style, QSize(int(self.width), int(self.height)));
	std::lock_guard image_lock(self.image_mutex);
	self.frame = std::move(image);
	self.frame_dirty = true;
}

// Starts the goal animation. Caller holds update_mutex.
void start_goal_locked(ScoreboardSource &self, GoalEvent event)
{
	self.goal = std::move(event);
	self.goal_elapsed = 0.0;
	self.dirty = true;
}

// A test goal for the home team, with the scorer of the last real goal if any.
GoalEvent test_goal_event(const ScoreboardSource &self)
{
	GoalEvent event;
	const std::optional<Match> &match = self.snapshot.match;
	if (match && !match->goals.empty()) {
		const GoalInfo &last = match->goals.back();
		event.side = last.team_id == match->away.id ? Side::Away : Side::Home;
		event.scorer = last.scorer;
		event.scorer_photo_url = last.scorer_photo_url;
		event.minute = last.minute;
		event.own_goal = last.own_goal;
		event.penalty = last.penalty;
	}
	return event;
}

// ---------------------------------------------------------------------------
// Source callbacks

const char *scoreboard_name(void *)
{
	return obs_module_text("Football.Source");
}

void scoreboard_update(void *data, obs_data_t *settings)
{
	auto *self = static_cast<ScoreboardSource *>(data);
	const std::string league = obs_data_get_string(settings, kLeagueKey);
	const std::string match_id = obs_data_get_string(settings, kMatchKey);

	std::unique_ptr<MatchFeed> old_feed;
	{
		std::lock_guard lock(self->update_mutex);
		self->names = name_mode_from_id(obs_data_get_string(settings, kNamesKey));
		self->goal_animation = obs_data_get_bool(settings, kGoalAnimationKey);
		self->style.design = board_design_from_id(obs_data_get_string(settings, kDesignKey));
		self->style.preset = style_preset_from_id(obs_data_get_string(settings, kStyleKey));
		self->style.accent = color_from_obs(obs_data_get_int(settings, kAccentKey));
		self->style.text = color_from_obs(obs_data_get_int(settings, kTextColorKey));
		self->style.show_league = obs_data_get_bool(settings, kShowLeagueKey);
		self->width = read_dimension(settings, kWidthKey, kDefaultWidth);
		// The large-crest designs keep their proportions: the height follows the width.
		self->height = design_height_for_width(self->style.design, self->width,
						       read_dimension(settings, kHeightKey, kDefaultHeight));

		if (!self->feed || league != self->league || match_id != self->match_id) {
			self->league = league;
			self->match_id = match_id;
			old_feed = std::move(self->feed);
			if (!league.empty())
				self->feed = std::make_unique<MatchFeed>(FeedConfig{league, match_id}, http_get(), now_seconds);
			self->snapshot = FeedSnapshot{};
			self->seen_revision = std::numeric_limits<uint64_t>::max();
			self->goal_watch.reset();
			self->goal.reset();
		}
		self->dirty = true;
	}
	old_feed.reset(); // joins the old worker outside the lock
}

void scoreboard_tick(void *data, float seconds)
{
	auto *self = static_cast<ScoreboardSource *>(data);
	std::lock_guard lock(self->update_mutex);
	self->time += seconds;

	if (self->feed) {
		FeedSnapshot snapshot = self->feed->snapshot();
		if (snapshot.revision != self->seen_revision) {
			self->seen_revision = snapshot.revision;
			self->snapshot = std::move(snapshot);
			self->dirty = true;
			if (self->snapshot.status == FeedStatus::Ok && self->snapshot.match) {
				if (auto goal = self->goal_watch.observe(*self->snapshot.match)) {
					obs_log(LOG_INFO, "goal: %s %d - %d %s", self->snapshot.match->home.name.c_str(),
						self->snapshot.match->home.score.value_or(0),
						self->snapshot.match->away.score.value_or(0),
						self->snapshot.match->away.name.c_str());
					if (self->goal_animation)
						start_goal_locked(*self, std::move(*goal));
				}
			}
		}
	}

	if (self->test_goal_requested.exchange(false) && self->snapshot.match)
		start_goal_locked(*self, test_goal_event(*self));

	if (self->goal) {
		self->goal_elapsed += seconds;
		if (self->goal_elapsed >= kGoalAnimationSeconds)
			self->goal.reset();
		self->dirty = true;
	}

	// The live dot blinks once per second.
	const bool live = self->snapshot.match && self->snapshot.match->in_progress();
	const bool blink_on = !live || std::fmod(self->time, 1.0) < 0.6;
	if (blink_on != self->blink_on) {
		self->blink_on = blink_on;
		self->dirty = true;
	}

	if (ImageStore *store = image_store()) {
		const uint64_t generation = store->generation();
		if (generation != self->seen_images) {
			self->seen_images = generation;
			self->dirty = true;
		}
	}

	if (self->dirty) {
		self->dirty = false;
		repaint_locked(*self);
	}
}

// Graphics thread: uploads the latest painted frame, if any.
void upload_if_dirty(ScoreboardSource &self)
{
	std::lock_guard image_lock(self.image_mutex);
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
	// QImage ARGB32 is 0xAARRGGBB per pixel: B, G, R, A in memory on little-endian targets.
	self.texture = gs_texture_create(width, height, GS_BGRA, 1, &bits, GS_DYNAMIC);
	self.texture_width = self.texture ? width : 0;
	self.texture_height = self.texture ? height : 0;
}

void scoreboard_render(void *data, gs_effect_t *effect)
{
	auto *self = static_cast<ScoreboardSource *>(data);
	upload_if_dirty(*self);
	if (!self->texture)
		return;

	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_blend_state_push();
	// The painted image has premultiplied alpha.
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), self->texture);
	gs_draw_sprite(self->texture, 0, self->texture_width, self->texture_height);

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);
}

uint32_t scoreboard_width(void *data)
{
	return static_cast<ScoreboardSource *>(data)->width;
}

uint32_t scoreboard_height(void *data)
{
	return static_cast<ScoreboardSource *>(data)->height;
}

// Hotkey thread. The hotkey is unregistered before the source goes away.
void test_hotkey_pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		static_cast<ScoreboardSource *>(data)->test_goal_requested = true;
}

void *scoreboard_create(obs_data_t *settings, obs_source_t *source)
{
	auto *self = new ScoreboardSource();
	self->source = source;
	scoreboard_update(self, settings);
	self->test_hotkey = obs_hotkey_register_source(source, kTestHotkeyName, obs_module_text("Football.TestGoal"),
						       test_hotkey_pressed, self);
	return self;
}

void scoreboard_destroy(void *data)
{
	auto *self = static_cast<ScoreboardSource *>(data);
	if (self->test_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(self->test_hotkey);
	std::unique_ptr<MatchFeed> feed;
	{
		std::lock_guard lock(self->update_mutex);
		feed = std::move(self->feed);
	}
	feed.reset(); // joins the worker
	obs_enter_graphics();
	gs_texture_destroy(self->texture);
	obs_leave_graphics();
	delete self;
}

void scoreboard_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, kLeagueKey, kDefaultLeague);
	obs_data_set_default_string(settings, kMatchKey, "");
	obs_data_set_default_string(settings, kNamesKey, "full");
	obs_data_set_default_bool(settings, kShowLeagueKey, true);
	obs_data_set_default_bool(settings, kGoalAnimationKey, true);
	obs_data_set_default_string(settings, kDesignKey, std::string(board_design_id(kDefaultDesign)).c_str());
	obs_data_set_default_string(settings, kStyleKey, std::string(style_preset_id(StylePreset::Bold)).c_str());
	obs_data_set_default_int(settings, kAccentKey, kDefaultAccent);
	obs_data_set_default_int(settings, kTextColorKey, kDefaultText);
	obs_data_set_default_int(settings, kWidthKey, kDefaultWidth);
	obs_data_set_default_int(settings, kHeightKey, kDefaultHeight);
}

// ---------------------------------------------------------------------------
// Properties

// "Real Madrid vs Barcelona · LIVE 67'", "... · Sat 22 Nov 20:00", "... · FT 2 - 1"
std::string match_label(const Match &match, const QLocale &locale)
{
	QString label = QString::fromStdString(match.home.name) + QStringLiteral(" vs ") +
			QString::fromStdString(match.away.name) + QStringLiteral("  ·  ");
	switch (match.state) {
	case MatchState::Live:
		label += text("Football.Status.Live") + QStringLiteral(" ") + QString::fromStdString(match.clock) +
			 QStringLiteral(" ") + score_text(match);
		break;
	case MatchState::HalfTime:
		label += text("Football.Status.HalfTime") + QStringLiteral(" ") + score_text(match);
		break;
	case MatchState::Finished:
		label += text("Football.Status.FullTime") + QStringLiteral(" ") + score_text(match);
		break;
	case MatchState::Postponed:
		label += text("Football.Status.Postponed");
		break;
	case MatchState::Scheduled: {
		const QDateTime start = QDateTime::fromSecsSinceEpoch(match.start_utc).toLocalTime();
		label += locale.toString(start, QStringLiteral("ddd d MMM, HH:mm"));
		break;
	}
	}
	return label.toStdString();
}

void fill_match_list(obs_property_t *list, const std::string &league, const std::string &selected, bool force)
{
	obs_property_list_clear(list);
	obs_property_list_add_string(list, obs_module_text("Football.Match.Automatic"), "");
	if (league.empty())
		return;

	const auto board = league_matches(league, force);
	if (!board) {
		const size_t index = obs_property_list_add_string(list, obs_module_text("Football.Match.LoadError"), "-");
		obs_property_list_item_disable(list, index, true);
		if (!selected.empty())
			obs_property_list_add_string(list, obs_module_text("Football.Match.Saved"), selected.c_str());
		return;
	}

	const QLocale locale = obs_locale();
	const bool all_leagues = league == kAllLeagues;
	bool selected_listed = selected.empty();
	// Only live and upcoming matches; with "all leagues", only the live ones.
	const std::vector<Match> listed = listable_matches(board->matches, now_seconds(), all_leagues);
	for (const Match &match : listed) {
		std::string label = match_label(match, locale);
		if (all_leagues) {
			const League *own = find_league_by_espn_id(match.league_id);
			const std::string name = own ? own->name
						     : !match.competition.empty() ? match.competition
						     : obs_module_text("Football.League.Other");
			label = name + "  ·  " + label;
		}
		obs_property_list_add_string(list, label.c_str(), match.id.c_str());
		selected_listed = selected_listed || match.id == selected;
	}
	if (listed.empty()) {
		const size_t index = obs_property_list_add_string(
			list, obs_module_text(all_leagues ? "Football.Match.NoneLive" : "Football.Match.None"), "-");
		obs_property_list_item_disable(list, index, true);
	}
	// Keep a saved choice selectable even once it drops out of the window.
	if (!selected_listed) {
		std::string label = obs_module_text("Football.Match.Saved");
		for (const Match &match : board->matches) {
			if (match.id == selected)
				label = match_label(match, locale);
		}
		obs_property_list_add_string(list, label.c_str(), selected.c_str());
	}
}

bool league_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const std::string league = obs_data_get_string(settings, kLeagueKey);
	std::string selected = obs_data_get_string(settings, kMatchKey);
	// A match id only means something inside its league.
	const auto board = league_matches(league);
	bool known = selected.empty();
	if (board) {
		for (const Match &match : board->matches)
			known = known || match.id == selected;
	}
	if (!known && board) {
		selected.clear();
		obs_data_set_string(settings, kMatchKey, "");
	}
	fill_match_list(obs_properties_get(props, kMatchKey), league, selected, false);
	return true;
}

bool refresh_clicked(obs_properties_t *props, obs_property_t *, void *data)
{
	auto *self = static_cast<ScoreboardSource *>(data);
	std::string league = kDefaultLeague;
	std::string selected;
	if (self) {
		obs_data_t *settings = obs_source_get_settings(self->source);
		league = obs_data_get_string(settings, kLeagueKey);
		selected = obs_data_get_string(settings, kMatchKey);
		obs_data_release(settings);
		std::lock_guard lock(self->update_mutex);
		if (self->feed)
			self->feed->refresh_now();
	}
	fill_match_list(obs_properties_get(props, kMatchKey), league, selected, true);
	return true;
}

// Last in the properties, low-key: the Ko-fi support message. Only opens on click.
bool support_clicked(obs_properties_t *, obs_property_t *, void *)
{
	ui::run_support_dialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
	return false;
}

// Style presets and a free height only apply to the Classic bar.
bool design_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool classic = board_design_from_id(obs_data_get_string(settings, kDesignKey)) == BoardDesign::Classic;
	obs_property_set_visible(obs_properties_get(props, kStyleKey), classic);
	obs_property_set_visible(obs_properties_get(props, kHeightKey), classic);
	return true;
}

bool test_goal_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	if (auto *self = static_cast<ScoreboardSource *>(data))
		self->test_goal_requested = true;
	return false;
}

obs_properties_t *scoreboard_properties(void *data)
{
	auto *self = static_cast<ScoreboardSource *>(data);
	std::string league = kDefaultLeague;
	std::string selected;
	if (self) {
		obs_data_t *settings = obs_source_get_settings(self->source);
		league = obs_data_get_string(settings, kLeagueKey);
		selected = obs_data_get_string(settings, kMatchKey);
		obs_data_release(settings);
	}

	obs_properties_t *props = obs_properties_create();

	obs_property_t *leagues = obs_properties_add_list(props, kLeagueKey, obs_module_text("Football.League"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(leagues, obs_module_text("Football.League.Choose"), "");
	obs_property_list_add_string(leagues, obs_module_text("Football.League.AllLive"), kAllLeagues);
	for (const League &entry : league_catalog())
		obs_property_list_add_string(leagues, entry.name, entry.slug);
	obs_property_set_modified_callback(leagues, league_changed);

	obs_property_t *matches = obs_properties_add_list(props, kMatchKey, obs_module_text("Football.Match"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_long_description(matches, obs_module_text("Football.Match.Help"));
	fill_match_list(matches, league, selected, false);
	obs_properties_add_button2(props, kRefreshKey, obs_module_text("Football.Refresh"), refresh_clicked, self);

	obs_property_t *names = obs_properties_add_list(props, kNamesKey, obs_module_text("Football.Names"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(names, obs_module_text("Football.Names.Full"), "full");
	obs_property_list_add_string(names, obs_module_text("Football.Names.Short"), "short");
	obs_property_list_add_string(names, obs_module_text("Football.Names.Abbreviation"), "abbreviation");

	obs_properties_add_bool(props, kShowLeagueKey, obs_module_text("Football.ShowLeague"));
	obs_properties_add_bool(props, kGoalAnimationKey, obs_module_text("Football.GoalAnimation"));
	obs_properties_add_button2(props, kTestGoalKey, obs_module_text("Football.TestGoal"), test_goal_clicked, self);

	obs_property_t *design = obs_properties_add_list(props, kDesignKey, obs_module_text("Football.Design"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const BoardDesign entry : kBoardDesigns) {
		const std::string id(board_design_id(entry));
		const std::string key = "Football.Design." + id;
		obs_property_list_add_string(design, obs_module_text(key.c_str()), id.c_str());
	}
	obs_property_set_modified_callback(design, design_changed);

	obs_property_t *style = obs_properties_add_list(props, kStyleKey, obs_module_text("Football.Style"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(style, obs_module_text("Style.Minimal"),
				     std::string(style_preset_id(StylePreset::Minimal)).c_str());
	obs_property_list_add_string(style, obs_module_text("Style.Bold"),
				     std::string(style_preset_id(StylePreset::Bold)).c_str());
	obs_property_list_add_string(style, obs_module_text("Style.Neon"),
				     std::string(style_preset_id(StylePreset::Neon)).c_str());

	obs_properties_add_color_alpha(props, kAccentKey, obs_module_text("Football.AccentColor"));
	obs_properties_add_color_alpha(props, kTextColorKey, obs_module_text("Football.TextColor"));
	obs_properties_add_int(props, kWidthKey, obs_module_text("Football.Width"), 16, 4096, 1);
	obs_properties_add_int(props, kHeightKey, obs_module_text("Football.Height"), 16, 4096, 1);

	const std::string support = std::string("☕ ") + obs_module_text("Support.Link");
	obs_properties_add_button2(props, "support", support.c_str(), support_clicked, nullptr);
	return props;
}

} // namespace

void register_scoreboard_source()
{
	obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	info.get_name = scoreboard_name;
	info.create = scoreboard_create;
	info.destroy = scoreboard_destroy;
	info.update = scoreboard_update;
	info.get_defaults = scoreboard_defaults;
	info.get_properties = scoreboard_properties;
	info.get_width = scoreboard_width;
	info.get_height = scoreboard_height;
	info.video_tick = scoreboard_tick;
	info.video_render = scoreboard_render;
	obs_register_source(&info);
}

} // namespace fscore::obs
