#include "core/leaders.hpp"

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
	if (value.is_string())
		return value.get<std::string>();
	if (value.is_number_integer())
		return std::to_string(value.get<long long>());
	return {};
}

// "Matches: 11, Goals: 15" -> 11
int matches_in(const std::string &text)
{
	const size_t at = text.find("Matches: ");
	return at == std::string::npos ? 0 : std::atoi(text.c_str() + at + 9);
}

std::vector<LeaderEntry> parse_board(const json &category)
{
	std::vector<LeaderEntry> board;
	const json &leaders = child(category, "leaders");
	if (!leaders.is_array())
		return board;
	for (const json &leader : leaders) {
		const json &athlete = child(leader, "athlete");
		const json &team = child(athlete, "team");
		LeaderEntry entry;
		entry.name = str(athlete, "displayName");
		entry.team = str(team, "abbreviation");
		entry.team_id = str(team, "id");
		const json &logos = child(team, "logos");
		if (logos.is_array()) {
			for (const json &logo : logos) {
				const json &rel = child(logo, "rel");
				const bool dark = rel.is_array() && std::find(rel.begin(), rel.end(), json("dark")) != rel.end();
				if (entry.team_logo.empty() || dark)
					entry.team_logo = espn::https_url(str(logo, "href"));
			}
		}
		const json &value = child(leader, "value");
		entry.value = value.is_number() ? static_cast<int>(value.get<double>()) : 0;
		entry.matches = matches_in(str(leader, "displayValue"));
		if (!entry.name.empty())
			board.push_back(std::move(entry));
	}
	return board;
}

} // namespace

std::optional<LeaderBoards> parse_leaders(const std::string &body)
{
	const json root = json::parse(body, nullptr, false);
	if (root.is_discarded() || !root.is_object() || !child(root, "stats").is_array())
		return std::nullopt;
	LeaderBoards boards;
	for (const json &category : child(root, "stats")) {
		const std::string name = str(category, "name");
		if (name == "goalsLeaders")
			boards.goals = parse_board(category);
		else if (name == "assistsLeaders")
			boards.assists = parse_board(category);
	}
	return boards;
}

std::string leaders_url(const std::string &league)
{
	return std::string(espn::kApiBase) + league + "/statistics";
}

} // namespace fscore
