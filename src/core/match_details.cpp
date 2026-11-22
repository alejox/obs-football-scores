#include "core/match_details.hpp"

#include "core/espn.hpp"

#include <json.hpp>

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

bool flag(const json &object, const char *key)
{
	const json &value = child(object, key);
	return value.is_boolean() && value.get<bool>();
}

std::optional<double> number(const json &value)
{
	if (value.is_number())
		return value.get<double>();
	if (value.is_string()) {
		const std::string text = value.get<std::string>();
		if (text.empty())
			return std::nullopt;
		char *end = nullptr;
		const double parsed = std::strtod(text.c_str(), &end);
		if (end != text.c_str())
			return parsed;
	}
	return std::nullopt;
}

PlayKind play_kind(const std::string &type)
{
	if (type == "goal" || type == "own-goal" || type == "penalty---scored")
		return PlayKind::Goal;
	if (type == "shot-on-target")
		return PlayKind::ShotOnTarget;
	if (type == "shot-off-target")
		return PlayKind::ShotOffTarget;
	if (type == "shot-blocked")
		return PlayKind::ShotBlocked;
	if (type == "shot-hit-woodwork")
		return PlayKind::Woodwork;
	if (type == "corner-awarded")
		return PlayKind::Corner;
	if (type == "foul")
		return PlayKind::Foul;
	if (type == "offside")
		return PlayKind::Offside;
	if (type == "yellow-card")
		return PlayKind::YellowCard;
	if (type == "red-card" || type == "second-yellow-card")
		return PlayKind::RedCard;
	if (type == "substitution")
		return PlayKind::Substitution;
	return PlayKind::Other;
}

Lineup parse_lineup(const json &roster)
{
	Lineup lineup;
	lineup.formation = str(roster, "formation");
	const json &players = child(roster, "roster");
	if (!players.is_array())
		return lineup;
	for (const json &entry : players) {
		const json &athlete = child(entry, "athlete");
		LineupPlayer player;
		player.name = str(athlete, "displayName");
		player.short_name = str(athlete, "shortName");
		if (player.short_name.empty())
			player.short_name = player.name;
		player.jersey = str(entry, "jersey");
		player.position = str(child(entry, "position"), "abbreviation");
		const json &place = child(entry, "formationPlace");
		player.formation_place = place.is_number_integer() ? place.get<int>() : std::atoi(str(entry, "formationPlace").c_str());
		player.starter = flag(entry, "starter");
		player.subbed_in = flag(entry, "subbedIn");
		player.subbed_out = flag(entry, "subbedOut");
		if (player.name.empty())
			continue;
		(player.starter ? lineup.starters : lineup.substitutes).push_back(std::move(player));
	}
	return lineup;
}

TeamStats parse_stats(const json &team)
{
	TeamStats stats;
	const json &list = child(team, "statistics");
	if (!list.is_array())
		return stats;
	for (const json &stat : list) {
		const std::string name = str(stat, "name");
		std::optional<double> value = number(child(stat, "displayValue"));
		if (!value)
			value = number(child(stat, "value"));
		if (!name.empty() && value)
			stats[name] = *value;
	}
	return stats;
}

} // namespace

std::optional<MatchDetails> parse_summary(const std::string &body)
{
	const json root = json::parse(body, nullptr, false);
	if (root.is_discarded() || !root.is_object() || !child(root, "header").is_object())
		return std::nullopt;

	MatchDetails details;
	const json &competitions = child(child(root, "header"), "competitions");
	if (competitions.is_array() && !competitions.empty()) {
		const json &competitors = child(competitions.front(), "competitors");
		if (competitors.is_array()) {
			for (const json &competitor : competitors) {
				const json &team = child(competitor, "team");
				const bool home = str(competitor, "homeAway") == "home";
				(home ? details.home_id : details.away_id) = str(team, "id");
				(home ? details.home_name : details.away_name) = str(team, "displayName");
			}
		}
	}
	auto side_of_id = [&details](const std::string &id) -> std::optional<TeamSide> {
		if (!id.empty() && id == details.home_id)
			return TeamSide::Home;
		if (!id.empty() && id == details.away_id)
			return TeamSide::Away;
		return std::nullopt;
	};
	auto side_of_name = [&details](const std::string &name) -> std::optional<TeamSide> {
		if (!name.empty() && name == details.home_name)
			return TeamSide::Home;
		if (!name.empty() && name == details.away_name)
			return TeamSide::Away;
		return std::nullopt;
	};

	const json &teams = child(child(root, "boxscore"), "teams");
	if (teams.is_array()) {
		for (const json &team : teams) {
			const auto side = side_of_id(str(child(team, "team"), "id"));
			if (side)
				(*side == TeamSide::Home ? details.home_stats : details.away_stats) = parse_stats(team);
		}
	}

	const json &rosters = child(root, "rosters");
	if (rosters.is_array()) {
		for (const json &roster : rosters) {
			std::optional<TeamSide> side = side_of_id(str(child(roster, "team"), "id"));
			if (!side)
				side = str(roster, "homeAway") == "away" ? TeamSide::Away : TeamSide::Home;
			(*side == TeamSide::Home ? details.home_lineup : details.away_lineup) = parse_lineup(roster);
		}
	}

	const json &commentary = child(root, "commentary");
	if (commentary.is_array()) {
		for (const json &entry : commentary) {
			const json &raw = child(entry, "play");
			if (!raw.is_object())
				continue;
			Play play;
			const json &sequence = child(entry, "sequence");
			play.sequence = sequence.is_number_integer() ? sequence.get<int>() : static_cast<int>(details.plays.size());
			play.kind = play_kind(str(child(raw, "type"), "type"));
			play.side = side_of_name(str(child(raw, "team"), "displayName"));
			const json &clock = child(raw, "clock");
			play.minute = str(clock, "displayValue");
			if (auto seconds = number(child(clock, "value")))
				play.clock_seconds = *seconds;
			play.text = str(raw, "text");
			const json &participants = child(raw, "participants");
			if (participants.is_array() && !participants.empty())
				play.player = str(child(participants.front(), "athlete"), "displayName");
			const auto x = number(child(raw, "fieldPositionX"));
			const auto y = number(child(raw, "fieldPositionY"));
			if (x && y) {
				play.has_position = true;
				play.x = *x;
				play.y = *y;
			}
			const auto x2 = number(child(raw, "fieldPosition2X"));
			const auto y2 = number(child(raw, "fieldPosition2Y"));
			if (x2 && y2) {
				play.has_end = true;
				play.end_x = *x2;
				play.end_y = *y2;
			}
			details.plays.push_back(std::move(play));
		}
	}
	return details;
}

std::string summary_url(const std::string &league, const std::string &event_id)
{
	return std::string(espn::kApiBase) + league + "/summary?event=" + event_id;
}

} // namespace fscore
