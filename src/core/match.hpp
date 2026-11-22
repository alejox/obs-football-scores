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
	std::optional<uint32_t> alternate_color; // the away kit color, when the feed has one
	std::optional<int> score;      // unset before kick-off
};

enum class MatchState {
	Scheduled, // not started yet
	Live,      // in play
	HalfTime,  // break between halves (or before extra time / penalties)
	Interrupted, // in play but stopped: suspended, delayed or abandoned while ESPN still has it on
	Finished,
	Postponed, // postponed, suspended, cancelled or abandoned
};

struct GoalInfo {
	std::string team_id;
	std::string minute; // "67'", "90'+3'"
	std::string scorer;
	std::string scorer_photo_url; // ESPN has a headshot for some players only
	bool own_goal = false;
	bool penalty = false;
};

struct Match {
	std::string id;
	std::string league_id; // ESPN's numeric league id, from the event uid; empty when unknown
	std::string competition; // the feed's own competition name ("FIFA U-17 World Cup"); may be empty
	int64_t start_utc = 0; // seconds since the Unix epoch
	Team home;
	Team away;
	MatchState state = MatchState::Scheduled;
	std::string clock;  // "67'" while live
	std::string detail; // the feed's short status: "FT", "HT", "Postponed", ...
	std::vector<GoalInfo> goals;

	bool started() const { return in_progress() || state == MatchState::Finished; }
	// On air: playing, at a break, or stopped for a while. Every list of live matches
	// uses this, so none of these drops out while the broadcast goes on.
	bool in_progress() const
	{
		return state == MatchState::Live || state == MatchState::HalfTime || state == MatchState::Interrupted;
	}
};

// One league scoreboard as the feed returns it.
struct Scoreboard {
	std::string league_name;
	std::string league_logo_url;
	std::vector<Match> matches;
};

} // namespace fscore
