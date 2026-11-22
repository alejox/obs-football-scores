// Pure core: no OBS, no Qt. League top scorers and assist leaders.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fscore {

struct LeaderEntry {
	std::string name;      // "Raphinha"
	std::string team;      // "BAR"
	std::string team_id;
	std::string team_logo; // for dark backgrounds when ESPN has one
	int value = 0;         // goals or assists
	int matches = 0;
};

struct LeaderBoards {
	std::vector<LeaderEntry> goals;
	std::vector<LeaderEntry> assists;
};

// Parses /apis/site/v2/sports/soccer/<league>/statistics. nullopt for a body that is not one.
std::optional<LeaderBoards> parse_leaders(const std::string &body);

std::string leaders_url(const std::string &league);

} // namespace fscore
