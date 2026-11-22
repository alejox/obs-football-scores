#include "core/espn.hpp"

#include "core/time_util.hpp"

#include <json.hpp>

#include <algorithm>
#include <cstdlib>

namespace fscore::espn {
namespace {

using nlohmann::json;

std::string str(const json &object, const char *key)
{
	if (!object.is_object())
		return {};
	const auto it = object.find(key);
	if (it == object.end() || !it->is_string())
		return {};
	return it->get<std::string>();
}

const json &child(const json &object, const char *key)
{
	static const json empty;
	if (!object.is_object())
		return empty;
	const auto it = object.find(key);
	return it == object.end() ? empty : *it;
}

std::optional<uint32_t> parse_color(const std::string &hex)
{
	if (hex.size() != 6)
		return std::nullopt;
	char *end = nullptr;
	const unsigned long value = std::strtoul(hex.c_str(), &end, 16);
	if (end != hex.c_str() + hex.size())
		return std::nullopt;
	return static_cast<uint32_t>(value);
}

std::optional<int> parse_score(const json &competitor)
{
	const json &score = child(competitor, "score");
	if (score.is_number_integer())
		return score.get<int>();
	if (score.is_string()) {
		const std::string text = score.get<std::string>();
		if (text.empty() || text.size() > 3 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
			return std::nullopt;
		return std::atoi(text.c_str());
	}
	// The summary endpoint sends {"value": 2.0, "displayValue": "2"}.
	if (score.is_object()) {
		const json &value = child(score, "value");
		if (value.is_number())
			return static_cast<int>(value.get<double>());
	}
	return std::nullopt;
}

std::string team_logo(const json &team)
{
	std::string logo = str(team, "logo");
	if (logo.empty()) {
		const json &logos = child(team, "logos");
		if (logos.is_array() && !logos.empty())
			logo = str(logos.front(), "href");
	}
	return https_url(logo);
}

// The board is dark, so prefer the logo variant ESPN makes for dark backgrounds.
std::string league_logo(const json &logos)
{
	if (!logos.is_array() || logos.empty())
		return {};
	for (const json &logo : logos) {
		const json &rel = child(logo, "rel");
		if (rel.is_array() && std::find(rel.begin(), rel.end(), json("dark")) != rel.end())
			return https_url(str(logo, "href"));
	}
	return https_url(str(logos.front(), "href"));
}

Team parse_team(const json &competitor)
{
	const json &team = child(competitor, "team");
	Team out;
	out.id = str(team, "id");
	out.name = str(team, "displayName");
	out.short_name = str(team, "shortDisplayName");
	if (out.short_name.empty())
		out.short_name = out.name;
	out.abbreviation = str(team, "abbreviation");
	out.logo_url = team_logo(team);
	out.color = parse_color(str(team, "color"));
	out.alternate_color = parse_color(str(team, "alternateColor"));
	return out;
}

MatchState parse_state(const json &status_type)
{
	const std::string state = str(status_type, "state");
	const std::string name = str(status_type, "name");
	const bool break_status = name == "STATUS_HALFTIME" || name == "STATUS_HALFTIME_ET" ||
				  name == "STATUS_END_OF_REGULATION" || name == "STATUS_END_OF_EXTRATIME" ||
				  name == "STATUS_END_OF_EXTRA_TIME";
	const bool stopped_status = name == "STATUS_SUSPENDED" || name == "STATUS_DELAYED" ||
				    name == "STATUS_ABANDONED" || name == "STATUS_RAIN_DELAY";
	// "in" is ESPN's own word for a match on air: whatever the status name says, it
	// counts as live (a break, a stoppage, extra time, penalties...).
	if (state == "in") {
		if (break_status)
			return MatchState::HalfTime;
		if (stopped_status)
			return MatchState::Interrupted;
		return MatchState::Live;
	}
	// A break status without a state (older answers) is still a break.
	if (break_status && state.empty())
		return MatchState::HalfTime;
	if (state == "pre") {
		if (name == "STATUS_POSTPONED" || name == "STATUS_CANCELED" || name == "STATUS_FORFEIT")
			return MatchState::Postponed;
		return MatchState::Scheduled; // a delayed kick-off is still upcoming
	}
	if (name == "STATUS_POSTPONED" || name == "STATUS_CANCELED" || name == "STATUS_SUSPENDED" ||
	    name == "STATUS_ABANDONED" || name == "STATUS_DELAYED" || name == "STATUS_FORFEIT")
		return MatchState::Postponed;
	if (state == "post")
		return MatchState::Finished;
	return MatchState::Scheduled;
}

std::vector<GoalInfo> parse_goals(const json &competition)
{
	std::vector<GoalInfo> goals;
	const json &details = child(competition, "details");
	if (!details.is_array())
		return goals;
	for (const json &detail : details) {
		const json &scoring = child(detail, "scoringPlay");
		if (!scoring.is_boolean() || !scoring.get<bool>())
			continue;
		const json &shootout = child(detail, "shootout");
		if (shootout.is_boolean() && shootout.get<bool>())
			continue; // penalty shoot-out kicks are not goals of the match
		GoalInfo goal;
		goal.team_id = str(child(detail, "team"), "id");
		goal.minute = str(child(detail, "clock"), "displayValue");
		const json &athletes = child(detail, "athletesInvolved");
		if (athletes.is_array() && !athletes.empty()) {
			goal.scorer = str(athletes.front(), "displayName");
			goal.scorer_photo_url = https_url(str(athletes.front(), "headshot"));
		}
		const json &own = child(detail, "ownGoal");
		goal.own_goal = own.is_boolean() && own.get<bool>();
		const json &penalty = child(detail, "penaltyKick");
		goal.penalty = penalty.is_boolean() && penalty.get<bool>();
		goals.push_back(std::move(goal));
	}
	return goals;
}

// "s:600~l:740~e:401882814" -> "740"
std::string league_id_from_uid(const std::string &uid)
{
	const size_t start = uid.find("~l:");
	if (start == std::string::npos)
		return {};
	const size_t end = uid.find('~', start + 3);
	return uid.substr(start + 3, end == std::string::npos ? std::string::npos : end - start - 3);
}

std::optional<Match> parse_event(const json &event)
{
	const json &competitions = child(event, "competitions");
	if (!competitions.is_array() || competitions.empty())
		return std::nullopt;
	const json &competition = competitions.front();
	const json &competitors = child(competition, "competitors");
	if (!competitors.is_array() || competitors.size() < 2)
		return std::nullopt;

	Match match;
	match.id = str(event, "id");
	if (match.id.empty())
		return std::nullopt;
	match.league_id = league_id_from_uid(str(event, "uid"));
	std::string date = str(event, "date");
	if (date.empty())
		date = str(competition, "date");
	match.start_utc = parse_iso_utc(date).value_or(0);

	const json *home = nullptr;
	const json *away = nullptr;
	for (const json &competitor : competitors) {
		const std::string side = str(competitor, "homeAway");
		if (side == "home" && !home)
			home = &competitor;
		else if (side == "away" && !away)
			away = &competitor;
	}
	if (!home || !away) {
		home = &competitors[0];
		away = &competitors[1];
	}
	match.home = parse_team(*home);
	match.away = parse_team(*away);

	const json &status = child(event, "status").is_object() ? child(event, "status") : child(competition, "status");
	const json &type = child(status, "type");
	match.state = parse_state(type);
	match.detail = str(type, "shortDetail");
	match.clock = str(status, "displayClock");

	if (match.started()) {
		match.home.score = parse_score(*home);
		match.away.score = parse_score(*away);
	}
	match.goals = parse_goals(competition);
	// "FIFA U-17 World Cup, Group C" -> "FIFA U-17 World Cup"
	match.competition = str(competition, "altGameNote");
	if (const size_t comma = match.competition.find(','); comma != std::string::npos)
		match.competition.erase(comma);
	return match;
}

} // namespace

std::string month_url(const std::string &league, int year, int month)
{
	return std::string(kApiBase) + league + "/scoreboard?dates=" + yyyymm(year, month);
}

std::string day_url(const std::string &league, const std::string &day)
{
	return std::string(kApiBase) + league + "/scoreboard?dates=" + day;
}

std::vector<std::string> candidate_days(int64_t start_utc)
{
	std::vector<std::string> days;
	for (const int64_t offset : {-5 * 3600, -4 * 3600, 0}) {
		const std::string day = yyyymmdd(utc_date(start_utc + offset));
		if (std::find(days.begin(), days.end(), day) == days.end())
			days.push_back(day);
	}
	return days;
}

std::optional<Scoreboard> parse_scoreboard(const std::string &body)
{
	const json root = json::parse(body, nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return std::nullopt;
	const json &events = child(root, "events");
	if (!events.is_array())
		return std::nullopt;

	Scoreboard board;
	const json &leagues = child(root, "leagues");
	if (leagues.is_array() && !leagues.empty()) {
		const json &league = leagues.front();
		board.league_name = str(league, "name");
		board.league_logo_url = league_logo(child(league, "logos"));
	}
	for (const json &event : events) {
		if (auto match = parse_event(event))
			board.matches.push_back(std::move(*match));
	}
	return board;
}

std::string https_url(const std::string &url)
{
	if (url.rfind("http://", 0) == 0)
		return "https://" + url.substr(7);
	return url;
}

} // namespace fscore::espn
