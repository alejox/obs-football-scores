// Pure core: no OBS, no Qt.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fscore {

struct Team {
	std::string id;
	std::string name;         // "Real Madrid"
	std::string short_name;   // "Real Madrid" or a shorter form when the feed has one
	std::string abbreviation; // "RMA"
	std::string logo_url;     // crest, or the flag for national teams
	std::optional<uint32_t> color; // 0xRRGGBB, when the feed has one
	std::optional<int> score;      // unset before kick-off
};

enum class MatchState {
	Scheduled, // not started yet
	Live,      // in play
	HalfTime,  // break between halves (or before extra time)
	Finished,
	Postponed, // postponed, suspended, cancelled or abandoned
};

struct GoalInfo {
	std::string team_id;
	std::string minute; // "67'", "90'+3'"
	std::string scorer;
	bool own_goal = false;
	bool penalty = false;
};

struct Match {
	std::string id;
	int64_t start_utc = 0; // seconds since the Unix epoch
	Team home;
	Team away;
	MatchState state = MatchState::Scheduled;
	std::string clock;  // "67'" while live
	std::string detail; // the feed's short status: "FT", "HT", "Postponed", ...
	std::vector<GoalInfo> goals;

	bool started() const { return state == MatchState::Live || state == MatchState::HalfTime || state == MatchState::Finished; }
	bool in_progress() const { return state == MatchState::Live || state == MatchState::HalfTime; }
};

// One league scoreboard as the feed returns it.
struct Scoreboard {
	std::string league_name;
	std::string league_logo_url;
	std::vector<Match> matches;
};

} // namespace fscore
