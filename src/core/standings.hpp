// Pure core: no OBS, no Qt. League tables from ESPN's standings endpoint.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fscore {

struct StandingRow {
	int rank = 0;
	std::string team_id;
	std::string name;         // "Barcelona"
	std::string short_name;   // "Barça" when the feed has one
	std::string abbreviation; // "BAR"
	std::string logo_url;
	int played = 0, wins = 0, draws = 0, losses = 0;
	int goal_difference = 0;
	int points = 0;
	std::optional<uint32_t> zone_color; // 0xRRGGBB of the qualification / relegation zone
	std::string zone;                   // "Champions League", "Relegation"
};

struct StandingsGroup {
	std::string name; // "2026-27 LALIGA", "Group A", "League Phase"
	std::vector<StandingRow> rows; // in rank order
};

// Parses /apis/v2/sports/soccer/<league>/standings. nullopt for a body that is not one.
std::optional<std::vector<StandingsGroup>> parse_standings(const std::string &body);

std::string standings_url(const std::string &league);

// The group holding either team (a World Cup group), else the first group.
// nullptr for no groups.
const StandingsGroup *group_for_teams(const std::vector<StandingsGroup> &groups, const std::string &home_id,
				      const std::string &away_id);

// The rows to show when only `max_rows` fit: all of them if they fit; otherwise a
// window that keeps the highlighted teams visible, starting from the top when it can.
std::vector<StandingRow> visible_rows(const StandingsGroup &group, size_t max_rows, const std::string &home_id,
				      const std::string &away_id);

} // namespace fscore
