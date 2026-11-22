// Pure core: no OBS, no Qt. What one match's ESPN summary holds beyond the score:
// team stats, lineups and the plays placed on the pitch.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fscore {

enum class TeamSide { Home, Away };

struct LineupPlayer {
	std::string name;       // "Jan Oblak"
	std::string short_name; // "J. Oblak"
	std::string jersey;     // "13"
	std::string position;   // ESPN abbreviation: "G", "CD-L", "RB", "CM-R", "CF", "SUB"
	int formation_place = 0; // 1-11 for starters, 0 for substitutes
	bool starter = false;
	bool subbed_in = false;
	bool subbed_out = false;
};

struct Lineup {
	std::string formation; // "4-4-2"; empty when the feed has none yet
	std::vector<LineupPlayer> starters;
	std::vector<LineupPlayer> substitutes;
	bool empty() const { return starters.empty(); }
};

enum class PlayKind {
	Goal,
	ShotOnTarget,
	ShotOffTarget,
	ShotBlocked,
	Woodwork,
	Corner,
	Foul,
	Offside,
	YellowCard,
	RedCard,
	Substitution,
	Other,
};

// One play from the live commentary. Pitch coordinates are 0-100 from the acting
// team's point of view: x runs from its own goal (0) to the opponent's (100), y across.
struct Play {
	int sequence = 0;
	PlayKind kind = PlayKind::Other;
	std::optional<TeamSide> side;
	std::string minute; // "67'", "90'+3'"
	double clock_seconds = -1;
	std::string player;
	std::string text; // the feed's sentence, in English
	bool has_position = false;
	double x = 0, y = 0;
	bool has_end = false; // where a shot ended
	double end_x = 0, end_y = 0;
};

// ESPN boxscore stat names -> value, per team ("possessionPct" -> 63).
using TeamStats = std::map<std::string, double>;

struct MatchDetails {
	std::string home_id, away_id;
	std::string home_name, away_name;
	TeamStats home_stats, away_stats;
	Lineup home_lineup, away_lineup;
	std::vector<Play> plays; // in match order
};

// Parses /apis/site/v2/sports/soccer/<league>/summary?event=<id>. nullopt for a body
// that is not a summary. Missing parts (no lineups yet, no commentary) stay empty.
std::optional<MatchDetails> parse_summary(const std::string &body);

// The summary URL of one match.
std::string summary_url(const std::string &league, const std::string &event_id);

} // namespace fscore
