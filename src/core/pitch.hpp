// Pure core: no OBS, no Qt. Placing things on the pitch: players from a formation,
// plays from the feed's coordinates, and the pressure bar.
#pragma once

#include "core/match_details.hpp"

#include <vector>

namespace fscore {

// A point on a horizontal pitch, both axes 0..1: x from the home goal (left, 0) to the
// away goal (right, 1), y from the top touchline (0) to the bottom one (1). Home attacks
// to the right.
struct PitchPoint {
	double x = 0;
	double y = 0;
};

struct PlacedPlayer {
	LineupPlayer player;
	PitchPoint at;
};

// Places a team's starters in its half (home on the left, away on the right): the
// goalkeeper at the back, then one line per number of the formation ("4-2-3-1"), each
// line spread across the pitch in the players' own left-to-right order (read from
// ESPN's position abbreviations: LB, CD-L, CM-R, RW...). Without a usable formation
// the outfield players are split by role into defence, midfield and attack.
std::vector<PlacedPlayer> place_lineup(const Lineup &lineup, TeamSide side);

// A play's position on the horizontal pitch. The feed gives it from the acting team's
// point of view, attacking towards x = 100.
PitchPoint play_point(const Play &play);
PitchPoint play_end_point(const Play &play);

// A play to draw on the live tracker.
struct TrackedPlay {
	const Play *play = nullptr;
	int recency = 0; // 0 = the latest positioned play; shots keep their rank however old
};

// What the tracker shows: every shot and goal of the match, plus the last `recent`
// plays that have a position (fouls, corners, offsides...), newest first.
std::vector<TrackedPlay> tracked_plays(const std::vector<Play> &plays, size_t recent = 6);

bool is_shot(PlayKind kind);

// Who is on top right now, from -1 (all away) to 1 (all home): goals, shots, corners
// and plays in the last third over roughly the last ten minutes of play, the newest
// weighing the most. `now_seconds` is the match clock (-1: the latest play's clock).
double pressure(const std::vector<Play> &plays, double now_seconds = -1);

} // namespace fscore
