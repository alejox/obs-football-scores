#include "core/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <string>

namespace fscore {
namespace {

std::string base_of(const std::string &abbr)
{
	if (abbr.size() > 2 && (abbr.compare(abbr.size() - 2, 2, "-L") == 0 || abbr.compare(abbr.size() - 2, 2, "-R") == 0))
		return abbr.substr(0, abbr.size() - 2);
	return abbr;
}

// How far up the pitch a role plays: 1 defence ... 6 centre forward.
double depth_of(const LineupPlayer &player)
{
	const std::string base = base_of(player.position);
	static const struct {
		const char *abbr;
		double depth;
	} roles[] = {
		{"CD", 1.0},  {"CB", 1.0},  {"D", 1.0},   {"SW", 1.0},  {"LB", 1.1},  {"RB", 1.1},
		{"LWB", 1.6}, {"RWB", 1.6}, {"WB", 1.6},  {"DM", 2.0},  {"CDM", 2.0}, {"CM", 3.0},
		{"M", 3.0},   {"LM", 3.0},  {"RM", 3.0},  {"AM", 4.0},  {"CAM", 4.0}, {"LAM", 4.0},
		{"RAM", 4.0}, {"LW", 5.0},  {"RW", 5.0},  {"W", 5.0},   {"LF", 5.5},  {"RF", 5.5},
		{"F", 6.0},   {"CF", 6.0},  {"ST", 6.0},  {"FW", 6.0},  {"S", 6.0},
	};
	for (const auto &role : roles) {
		if (base == role.abbr)
			return role.depth;
	}
	return 3.0;
}

// The player's own left (-1) to right (1).
double lateral_of(const LineupPlayer &player)
{
	const std::string &abbr = player.position;
	if (abbr.size() > 2 && abbr.compare(abbr.size() - 2, 2, "-L") == 0)
		return -0.5;
	if (abbr.size() > 2 && abbr.compare(abbr.size() - 2, 2, "-R") == 0)
		return 0.5;
	const std::string base = base_of(abbr);
	if (base.size() >= 2 && base.front() == 'L')
		return -1.0;
	if (base.size() >= 2 && base.front() == 'R')
		return 1.0;
	return 0.0;
}

bool is_goalkeeper(const LineupPlayer &player)
{
	return player.position == "G" || player.position == "GK";
}

std::vector<int> formation_lines(const std::string &formation)
{
	std::vector<int> lines;
	int current = 0;
	bool any = false;
	for (const char c : formation) {
		if (c >= '0' && c <= '9') {
			current = current * 10 + (c - '0');
			any = true;
		} else if (c == '-') {
			if (any)
				lines.push_back(current);
			current = 0;
			any = false;
		} else {
			return {};
		}
	}
	if (any)
		lines.push_back(current);
	for (const int n : lines) {
		if (n <= 0 || n > 6)
			return {};
	}
	return lines;
}

} // namespace

std::vector<PlacedPlayer> place_lineup(const Lineup &lineup, TeamSide side)
{
	std::vector<PlacedPlayer> placed;
	std::vector<const LineupPlayer *> keepers, outfield;
	for (const LineupPlayer &player : lineup.starters)
		(is_goalkeeper(player) ? keepers : outfield).push_back(&player);
	if (keepers.empty() && !outfield.empty()) {
		// No "G": the lowest formation place is the keeper.
		auto it = std::min_element(outfield.begin(), outfield.end(), [](auto *a, auto *b) {
			return a->formation_place < b->formation_place;
		});
		keepers.push_back(*it);
		outfield.erase(it);
	}

	std::stable_sort(outfield.begin(), outfield.end(), [](const LineupPlayer *a, const LineupPlayer *b) {
		const double da = depth_of(*a), db = depth_of(*b);
		if (da != db)
			return da < db;
		return a->formation_place < b->formation_place;
	});

	std::vector<int> lines = formation_lines(lineup.formation);
	if (std::accumulate(lines.begin(), lines.end(), 0) != static_cast<int>(outfield.size())) {
		// No usable formation: group by role.
		lines.clear();
		int def = 0, mid = 0, att = 0;
		for (const LineupPlayer *player : outfield) {
			const double depth = depth_of(*player);
			(depth <= 1.6 ? def : depth <= 4.0 ? mid : att)++;
		}
		for (const int n : {def, mid, att}) {
			if (n > 0)
				lines.push_back(n);
		}
	}

	const bool home = side == TeamSide::Home;
	auto put = [&](const LineupPlayer &player, double x, double y) {
		// Home attacks right, so its left is the top; the away side is turned around.
		PitchPoint at{home ? x : 1.0 - x, home ? y : 1.0 - y};
		placed.push_back({player, at});
	};

	for (const LineupPlayer *keeper : keepers)
		put(*keeper, 0.05, 0.5);

	const double first = 0.17, last = 0.45;
	size_t next = 0;
	for (size_t line = 0; line < lines.size(); ++line) {
		const double x = lines.size() == 1 ? 0.3 : first + (last - first) * line / (lines.size() - 1);
		std::vector<const LineupPlayer *> row(outfield.begin() + static_cast<long>(next),
						      outfield.begin() + static_cast<long>(next + lines[line]));
		next += lines[line];
		std::stable_sort(row.begin(), row.end(), [](const LineupPlayer *a, const LineupPlayer *b) {
			const double la = lateral_of(*a), lb = lateral_of(*b);
			if (la != lb)
				return la < lb;
			return a->formation_place < b->formation_place;
		});
		for (size_t i = 0; i < row.size(); ++i) {
			const double y = 0.12 + 0.76 * (i + 1) / (row.size() + 1);
			put(*row[i], x, y);
		}
	}
	return placed;
}

PitchPoint play_point(const Play &play)
{
	const bool away = play.side && *play.side == TeamSide::Away;
	const double x = std::clamp(play.x / 100.0, 0.0, 1.0);
	const double y = std::clamp(play.y / 100.0, 0.0, 1.0);
	return away ? PitchPoint{1.0 - x, 1.0 - y} : PitchPoint{x, y};
}

PitchPoint play_end_point(const Play &play)
{
	Play end = play;
	end.x = play.end_x;
	end.y = play.end_y;
	return play_point(end);
}

double pressure(const std::vector<Play> &plays, double now_seconds)
{
	if (now_seconds < 0) {
		for (const Play &play : plays)
			now_seconds = std::max(now_seconds, play.clock_seconds);
	}
	if (now_seconds < 0)
		return 0.0;

	double home = 0, away = 0;
	for (const Play &play : plays) {
		if (!play.side || play.clock_seconds < 0)
			continue;
		const double age = now_seconds - play.clock_seconds;
		if (age < 0 || age > 900)
			continue;
		double weight = 0;
		switch (play.kind) {
		case PlayKind::Goal:
			weight = 5;
			break;
		case PlayKind::ShotOnTarget:
		case PlayKind::Woodwork:
			weight = 3;
			break;
		case PlayKind::ShotOffTarget:
			weight = 2;
			break;
		case PlayKind::ShotBlocked:
		case PlayKind::Corner:
			weight = 1.5;
			break;
		case PlayKind::Offside:
			weight = 0.5;
			break;
		default:
			break;
		}
		if (weight == 0)
			continue;
		weight *= std::exp(-age / 300.0);
		(*play.side == TeamSide::Home ? home : away) += weight;
	}
	// The +2 keeps a lone early shot from swinging the bar all the way.
	return std::clamp((home - away) / (home + away + 2.0), -1.0, 1.0);
}

bool is_shot(PlayKind kind)
{
	return kind == PlayKind::Goal || kind == PlayKind::ShotOnTarget || kind == PlayKind::ShotOffTarget ||
	       kind == PlayKind::ShotBlocked || kind == PlayKind::Woodwork;
}

std::vector<TrackedPlay> tracked_plays(const std::vector<Play> &plays, size_t recent)
{
	std::vector<TrackedPlay> out;
	int rank = 0;
	for (auto it = plays.rbegin(); it != plays.rend(); ++it) {
		if (!it->has_position)
			continue;
		const bool keep = static_cast<size_t>(rank) < recent || is_shot(it->kind);
		if (keep)
			out.push_back({&*it, rank});
		++rank;
	}
	return out;
}

} // namespace fscore
