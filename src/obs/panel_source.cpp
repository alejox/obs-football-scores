#include "obs/panel_source.hpp"

#include "core/league_catalog.hpp"
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

constexpr const char *kSourceId = "football_panel_source";
constexpr const char *kContentKey = "content";
constexpr const char *kRowsKey = "table_rows";
constexpr const char *kWidthKey = "width";
constexpr const char *kDefaultLeague = "esp.1";
constexpr long long kDefaultWidth = 960;
constexpr long long kDefaultRows = 10;
// The latest play's ring pulses this often; the frame is repainted ~15 times a second.
constexpr double kPulseSeconds = 1.6;
constexpr double kAnimationStep = 1.0 / 15.0;

render::PanelContent content_from_id(const std::string &id)
{
	if (id == "lineups")
		return render::PanelContent::Lineups;
	if (id == "stats")
		return render::PanelContent::Stats;
	if (id == "table")
		return render::PanelContent::Table;
	return render::PanelContent::LivePitch;
}

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

struct PanelSource {
	obs_source_t *source = nullptr;

	std::mutex update_mutex; // update (UI), tick (video)
	render::PanelContent content = render::PanelContent::LivePitch;
	std::string league;
	std::string match_id;
	size_t rows = kDefaultRows;
	std::unique_ptr<MatchFeed> feed;
	std::unique_ptr<DetailsFeed> details;

	FeedSnapshot snapshot;
	uint64_t seen_revision = std::numeric_limits<uint64_t>::max();
	DetailsFeed::Snapshot data;
	uint64_t seen_data = std::numeric_limits<uint64_t>::max();
	uint64_t seen_images = std::numeric_limits<uint64_t>::max();
	std::unordered_map<std::string, QImage> images;
	double time = 0, since_paint = 0;
	bool dirty = true;

	std::atomic<uint32_t> width{static_cast<uint32_t>(kDefaultWidth)};
	std::atomic<uint32_t> height{810};

	std::mutex image_mutex;
	QImage frame;
	bool frame_dirty = false;

	gs_texture_t *texture = nullptr;
	uint32_t texture_width = 0, texture_height = 0;
};

uint32_t height_for(render::PanelContent content, uint32_t width, size_t rows)
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
std::string league_of(const PanelSource &self, const Match &match)
{
	if (self.league != kAllLeagues)
		return self.league;
	const League *own = find_league_by_espn_id(match.league_id);
	return own ? own->slug : std::string();
}

// Caller holds update_mutex.
render::PanelView build_view(PanelSource &self)
{
	render::PanelView view;
	view.content = self.content;
	if (self.league.empty()) {
		view.message = text("Football.Message.ChooseLeague");
		return view;
	}
	const std::optional<Match> &match = self.snapshot.match;
	auto lookup = [&self](const std::string &url) { return image_for(self, url); };
	const render::PanelLabels l = labels();

	if (self.content == render::PanelContent::Table) {
		if (!self.data.standings || self.data.standings->empty()) {
			view.message = text(self.snapshot.status == FeedStatus::Error ? "Football.Message.Error"
										  : "Football.Message.Loading");
			return view;
		}
		const StandingsGroup *group = group_for_teams(*self.data.standings, match ? match->home.id : "",
							      match ? match->away.id : "");
		if (!group) {
			view.message = text("Panel.NoTable");
			return view;
		}
		view.has_data = true;
		render::fill_table(view, *group, match ? &*match : nullptr, self.rows, l, lookup);
		return view;
	}

	if (!match) {
		switch (self.snapshot.status) {
		case FeedStatus::NoMatches:
			view.message = text("Football.Message.NoMatches");
			break;
		case FeedStatus::NotFound:
			view.message = text("Football.Message.NotFound");
			break;
		case FeedStatus::Error:
			view.message = text("Football.Message.Error");
			break;
		default:
			view.message = text("Football.Message.Loading");
			break;
		}
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
	if (self.data.details) {
		const MatchDetails &d = *self.data.details;
		switch (self.content) {
		case render::PanelContent::LivePitch:
			render::fill_live_pitch(view, d, l);
			break;
		case render::PanelContent::Lineups:
			render::fill_lineups(view, d);
			if (view.players.empty()) {
				view.has_data = false;
				view.message = text("Panel.NoLineups");
			}
			break;
		case render::PanelContent::Stats:
			render::fill_stats(view, d, l);
			if (view.stats.empty()) {
				view.has_data = false;
				view.message = text("Panel.NoStats");
			}
			break;
		case render::PanelContent::Table:
			break;
		}
	} else if (self.content != render::PanelContent::LivePitch) {
		view.has_data = false;
		view.message = text("Football.Message.Loading");
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

const char *panel_name(void *)
{
	return obs_module_text("Panel.Source");
}

void panel_update(void *data, obs_data_t *settings)
{
	auto *self = static_cast<PanelSource *>(data);
	const std::string league = obs_data_get_string(settings, kLeagueKey);
	const std::string match_id = obs_data_get_string(settings, kMatchKey);
	std::unique_ptr<MatchFeed> old_feed;
	{
		std::lock_guard lock(self->update_mutex);
		self->content = content_from_id(obs_data_get_string(settings, kContentKey));
		self->rows = static_cast<size_t>(std::clamp<long long>(obs_data_get_int(settings, kRowsKey), 4, 40));
		const long long width = obs_data_get_int(settings, kWidthKey);
		self->width = static_cast<uint32_t>(std::clamp(width > 0 ? width : kDefaultWidth, 160LL, 4096LL));
		self->height = height_for(self->content, self->width, self->rows);
		if (!self->feed || league != self->league || match_id != self->match_id) {
			self->league = league;
			self->match_id = match_id;
			old_feed = std::move(self->feed);
			if (!league.empty())
				self->feed = std::make_unique<MatchFeed>(FeedConfig{league, match_id}, http_get(), now_seconds);
			self->snapshot = FeedSnapshot{};
			self->seen_revision = std::numeric_limits<uint64_t>::max();
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

	if (self->feed) {
		FeedSnapshot snapshot = self->feed->snapshot();
		if (snapshot.revision != self->seen_revision) {
			self->seen_revision = snapshot.revision;
			self->snapshot = std::move(snapshot);
			self->dirty = true;
		}
	}

	// Follow the shown match (and its league's table) with the details feed.
	DetailsTarget target;
	if (self->snapshot.match) {
		const Match &match = *self->snapshot.match;
		const std::string own = league_of(*self, match);
		target.summary_league = own.empty() ? self->league : own;
		target.event_id = match.id;
		target.live = match.in_progress() ||
			      (match.state == MatchState::Scheduled && match.start_utc - now_seconds() < 600);
		if (self->content == render::PanelContent::Table)
			target.standings_league = own;
	} else if (self->content == render::PanelContent::Table && self->league != kAllLeagues) {
		target.standings_league = self->league; // a table needs no match
	}
	self->details->set_target(target);
	DetailsFeed::Snapshot fresh = self->details->snapshot();
	if (fresh.revision != self->seen_data) {
		self->seen_data = fresh.revision;
		self->data = std::move(fresh);
		self->dirty = true;
	}

	if (ImageStore *store = image_store()) {
		const uint64_t generation = store->generation();
		if (generation != self->seen_images) {
			self->seen_images = generation;
			self->dirty = true;
		}
	}

	// The live pitch and the blinking dot animate while the match is on.
	const bool animated = self->snapshot.match && self->snapshot.match->in_progress() &&
			      self->content != render::PanelContent::Table;
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

void panel_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, kContentKey, "pitch");
	obs_data_set_default_string(settings, kLeagueKey, kDefaultLeague);
	obs_data_set_default_string(settings, kMatchKey, "");
	obs_data_set_default_int(settings, kRowsKey, kDefaultRows);
	obs_data_set_default_int(settings, kWidthKey, kDefaultWidth);
}

bool content_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool table = content_from_id(obs_data_get_string(settings, kContentKey)) == render::PanelContent::Table;
	obs_property_set_visible(obs_properties_get(props, kRowsKey), table);
	return true;
}

bool support_clicked(obs_properties_t *, obs_property_t *, void *)
{
	ui::run_support_dialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
	return false;
}

obs_properties_t *panel_properties(void *data)
{
	auto *self = static_cast<PanelSource *>(data);
	std::string league = kDefaultLeague;
	std::string selected;
	if (self) {
		obs_data_t *settings = obs_source_get_settings(self->source);
		league = obs_data_get_string(settings, kLeagueKey);
		selected = obs_data_get_string(settings, kMatchKey);
		obs_data_release(settings);
	}
	obs_properties_t *props = obs_properties_create();
	obs_property_t *content = obs_properties_add_list(props, kContentKey, obs_module_text("Panel.Content"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(content, obs_module_text("Panel.Content.Pitch"), "pitch");
	obs_property_list_add_string(content, obs_module_text("Panel.Content.Lineups"), "lineups");
	obs_property_list_add_string(content, obs_module_text("Panel.Content.Stats"), "stats");
	obs_property_list_add_string(content, obs_module_text("Panel.Content.Table"), "table");
	obs_property_set_modified_callback(content, content_changed);

	add_league_and_match(props, league, selected);

	obs_properties_add_int(props, kRowsKey, obs_module_text("Panel.TableRows"), 4, 40, 1);
	obs_properties_add_int(props, kWidthKey, obs_module_text("Football.Width"), 160, 4096, 10);
	obs_properties_add_text(props, "panel_help", obs_module_text("Panel.Help"), OBS_TEXT_INFO);
	const std::string support = std::string("☕ ") + obs_module_text("Support.Link");
	obs_properties_add_button2(props, "support", support.c_str(), support_clicked, nullptr);
	return props;
}

} // namespace

void register_panel_source()
{
	obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	info.get_name = panel_name;
	info.create = panel_create;
	info.destroy = panel_destroy;
	info.update = panel_update;
	info.get_defaults = panel_defaults;
	info.get_properties = panel_properties;
	info.get_width = panel_width;
	info.get_height = panel_height;
	info.video_tick = panel_tick;
	info.video_render = panel_render;
	obs_register_source(&info);
}

} // namespace fscore::obs
