#include "core/standings.hpp"

#include "core/espn.hpp"

#include <json.hpp>

#include <algorithm>
#include <cstdlib>

namespace fscore {
namespace {

using nlohmann::json;

const json &child(const json &object, const char *key)
{
	static const json empty;
	if (!object.is_object())
		return empty;
	const auto it = object.find(key);
	return it == object.end() ? empty : *it;
}

std::string str(const json &object, const char *key)
{
	const json &value = child(object, key);
	return value.is_string() ? value.get<std::string>() : std::string();
}

int stat_int(const json &stats, const char *name)
{
	if (!stats.is_array())
		return 0;
	for (const json &stat : stats) {
		if (str(stat, "name") != name)
			continue;
		const json &value = child(stat, "value");
		if (value.is_number())
			return static_cast<int>(value.get<double>());
		return std::atoi(str(stat, "displayValue").c_str()); // "+36" reads as 36
	}
	return 0;
}

std::optional<uint32_t> hex_color(std::string text)
{
	if (!text.empty() && text.front() == '#')
		text.erase(0, 1);
	if (text.size() != 6)
		return std::nullopt;
	char *end = nullptr;
	const unsigned long value = std::strtoul(text.c_str(), &end, 16);
	if (end != text.c_str() + 6)
		return std::nullopt;
	return static_cast<uint32_t>(value);
}

void collect(const json &node, std::vector<StandingsGroup> &out)
{
	const json &entries = child(child(node, "standings"), "entries");
	if (entries.is_array() && !entries.empty()) {
		StandingsGroup group;
		group.name = str(node, "name");
		for (const json &entry : entries) {
			const json &team = child(entry, "team");
			const json &stats = child(entry, "stats");
			StandingRow row;
			row.team_id = str(team, "id");
			row.name = str(team, "displayName");
			row.short_name = str(team, "shortDisplayName");
			if (row.short_name.empty())
				row.short_name = row.name;
			row.abbreviation = str(team, "abbreviation");
			const json &logos = child(team, "logos");
			if (logos.is_array() && !logos.empty())
				row.logo_url = espn::https_url(str(logos.front(), "href"));
			row.rank = stat_int(stats, "rank");
			row.played = stat_int(stats, "gamesPlayed");
			row.wins = stat_int(stats, "wins");
			row.draws = stat_int(stats, "ties");
			row.losses = stat_int(stats, "losses");
			row.goal_difference = stat_int(stats, "pointDifferential");
			row.points = stat_int(stats, "points");
			const json &note = child(entry, "note");
			row.zone_color = hex_color(str(note, "color"));
			row.zone = str(note, "description");
			group.rows.push_back(std::move(row));
		}
		std::stable_sort(group.rows.begin(), group.rows.end(), [](const StandingRow &a, const StandingRow &b) {
			// Unranked rows (rank 0) go last.
			return (a.rank == 0 ? 1 << 20 : a.rank) < (b.rank == 0 ? 1 << 20 : b.rank);
		});
		out.push_back(std::move(group));
	}
	const json &children = child(node, "children");
	if (children.is_array()) {
		for (const json &sub : children)
			collect(sub, out);
	}
}

} // namespace

std::optional<std::vector<StandingsGroup>> parse_standings(const std::string &body)
{
	const json root = json::parse(body, nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return std::nullopt;
	if (!child(root, "children").is_array() && !child(root, "standings").is_object())
		return std::nullopt;
	std::vector<StandingsGroup> groups;
	collect(root, groups);
	return groups;
}

std::string standings_url(const std::string &league)
{
	return "https://site.api.espn.com/apis/v2/sports/soccer/" + league + "/standings";
}

const StandingsGroup *group_for_teams(const std::vector<StandingsGroup> &groups, const std::string &home_id,
				      const std::string &away_id)
{
	if (groups.empty())
		return nullptr;
	for (const StandingsGroup &group : groups) {
		for (const StandingRow &row : group.rows) {
			if (!row.team_id.empty() && (row.team_id == home_id || row.team_id == away_id))
				return &group;
		}
	}
	return &groups.front();
}

std::vector<StandingRow> visible_rows(const StandingsGroup &group, size_t max_rows, const std::string &home_id,
				      const std::string &away_id)
{
	const std::vector<StandingRow> &rows = group.rows;
	if (max_rows == 0 || rows.size() <= max_rows)
		return rows;
	// The lowest highlighted row decides how far down the window must reach.
	size_t lowest = 0;
	bool any = false;
	for (size_t i = 0; i < rows.size(); ++i) {
		if (!rows[i].team_id.empty() && (rows[i].team_id == home_id || rows[i].team_id == away_id)) {
			lowest = i;
			any = true;
		}
	}
	size_t start = 0;
	if (any && lowest >= max_rows) {
		size_t highest = lowest;
		for (size_t i = 0; i < rows.size(); ++i) {
			if (!rows[i].team_id.empty() && (rows[i].team_id == home_id || rows[i].team_id == away_id)) {
				highest = i;
				break;
			}
		}
		// The lowest team at the bottom, one row of context below it when both teams
		// still fit; the window never runs past the end of the table.
		start = lowest + 1 - max_rows;
		if (lowest + 1 < rows.size() && lowest + 2 - max_rows <= highest)
			start = lowest + 2 - max_rows;
		start = std::min(start, rows.size() - max_rows);
	}
	return std::vector<StandingRow>(rows.begin() + static_cast<long>(start),
					rows.begin() + static_cast<long>(start + max_rows));
}

} // namespace fscore
