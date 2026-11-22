#include "obs/match_picker.hpp"

#include "core/league_catalog.hpp"
#include "core/match_selection.hpp"
#include "obs/services.hpp"

#include <QDateTime>

#include <vector>

namespace fscore::obs {
namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

bool league_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings);

} // namespace

QLocale obs_locale()
{
	QString name = QString::fromUtf8(obs_get_locale());
	name.replace('-', '_');
	return QLocale(name);
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
	case MatchState::Interrupted:
		label += text("Football.Status.Interrupted") + QStringLiteral(" ") + score_text(match);
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

namespace {
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
} // namespace

namespace {
SharedSelection &shared_selection()
{
	static SharedSelection shared;
	return shared;
}
} // namespace

Selection sync_on_update(obs_data_t *settings, LinkState &state, const Selection &previous, bool has_match)
{
	const Selection own{obs_data_get_string(settings, kLeagueKey), obs_data_get_string(settings, kMatchKey)};
	const Selection next = link_on_update(shared_selection(), state, obs_data_get_bool(settings, kLinkedKey), own,
					      previous, has_match);
	if (next.league != own.league)
		obs_data_set_string(settings, kLeagueKey, next.league.c_str());
	if (next.match != own.match)
		obs_data_set_string(settings, kMatchKey, next.match.c_str());
	return next;
}

void sync_on_tick(obs_source_t *source, LinkState &state, const Selection &current, bool has_match)
{
	const std::optional<Selection> next = link_on_tick(shared_selection(), state, current, has_match);
	if (!next)
		return;
	obs_data_t *change = obs_data_create();
	obs_data_set_string(change, kLeagueKey, next->league.c_str());
	obs_data_set_string(change, kMatchKey, next->match.c_str());
	obs_source_update(source, change); // deferred to the next tick for video sources
	obs_data_release(change);
}

void add_league_and_match(obs_properties_t *props, const std::string &league, const std::string &selected)
{
	obs_property_t *linked = obs_properties_add_bool(props, kLinkedKey, obs_module_text("Football.Linked"));
	obs_property_set_long_description(linked, obs_module_text("Football.Linked.Help"));
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
}

} // namespace fscore::obs
