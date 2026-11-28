// Pure core: no OBS, no Qt. Replays: which plays the dock lists, how its buttons move
// between them, the match as it stood at one play, and the cursor every source reads.
#pragma once

#include "core/match_details.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace fscore {

enum class ReplayFilter { All, Goals, Shots, Cards };

// Plays worth replaying: every goal, shot, corner, foul, offside and card ("All"), or
// one family of them. Substitutions and unknown plays never.
bool replayable(const Play &play, ReplayFilter filter);

// The sequences of the plays that pass `filter`, in match order.
std::vector<int> replay_list(const std::vector<Play> &plays, ReplayFilter filter);

enum class ReplayStep { First, Previous, Repeat, Next, Live };

// Where a button takes the cursor. `current` is the play on replay, or -1 while live.
// Returns the play to replay, or -1 for live: "Previous" from live goes to the latest
// play, "Next" after the last one returns to live, and "Previous" on the first one
// stays there. With nothing to replay every step stays live.
int replay_step(const std::vector<Play> &plays, ReplayFilter filter, int current, ReplayStep step);

// The play with this sequence, or null.
const Play *find_play(const std::vector<Play> &plays, int sequence);

// The match as it stood right after the play `sequence`: its plays and commentary lines
// up to that one (lineups, stats and the rest as they are). The play itself when it is
// not there: the whole match.
MatchDetails details_until(const MatchDetails &details, int sequence);

// Spots a new goal while the match is on, so the dock can replay it. The first call for
// a match only learns the goals it already has.
class GoalReplayWatch {
public:
	// The sequence of the newest goal that was not there on the previous call, if any.
	std::optional<int> update(const std::string &event_id, const std::vector<Play> &plays);

private:
	std::string event_;
	int newest_ = -1;
	bool primed_ = false;
};

// What the dock is replaying, read by every source. Thread-safe.
struct ReplayCursor {
	std::string event_id; // the match being replayed
	int sequence = -1;    // the play on replay, -1: live
	uint64_t revision = 0; // changes with every button press, even on the same play
	double speed = 1.0;    // how fast the ball travels: 0.5 slow, 1 normal, 2 fast
	bool badge = true;     // sources mark the replay ("REPETICIÓN 63'")

	bool replaying(const std::string &event) const { return sequence >= 0 && !event.empty() && event == event_id; }
};

class ReplayChannel {
public:
	ReplayCursor get() const;
	// Replays `sequence` of `event_id` (-1: back to live). Bumps the revision.
	void show(const std::string &event_id, int sequence);
	// Leaves the revision alone: a replay in progress carries on.
	void set_options(double speed, bool badge);

private:
	mutable std::mutex mutex_;
	ReplayCursor cursor_;
};

// The one the dock drives and the sources read.
ReplayChannel &replay_channel();

} // namespace fscore
