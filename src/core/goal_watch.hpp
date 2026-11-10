// Pure core: no OBS, no Qt.
#pragma once

#include "core/match.hpp"

#include <optional>
#include <string>

namespace fscore {

enum class Side { Home, Away };

struct GoalEvent {
	Side side = Side::Home;   // the team whose score went up
	std::string scorer;       // empty when the feed has not named them yet
	std::string scorer_photo_url; // empty when ESPN has no headshot
	std::string minute;
	bool own_goal = false;
	bool penalty = false;
};

// Turns successive snapshots of one match into goal events. The first
// snapshot of a match is the baseline: opening the source mid-match does not
// celebrate the goals already scored. A score that goes down (a VAR
// correction) resets the baseline without an event.
class GoalWatch {
public:
	// Returns the goal to celebrate, if this snapshot brought one. When both
	// teams scored between two snapshots, the later goal in the feed wins.
	std::optional<GoalEvent> observe(const Match &match);

	void reset();

private:
	std::string match_id_;
	int home_ = 0;
	int away_ = 0;
	bool has_baseline_ = false;
};

} // namespace fscore
