#include "core/match_selection.hpp"

#include <algorithm>
#include <unordered_set>

namespace fscore {
namespace {

// A match that kicked off less than this long ago and is over stays on screen.
constexpr int64_t kKeepFinishedSeconds = 4 * 3600;
// The match list always offers at least this many upcoming matches.
constexpr size_t kMinUpcoming = 10;

} // namespace

std::vector<Match> listable_matches(const std::vector<Match> &all, int64_t now, bool live_only, int ahead_days)
{
	std::vector<Match> live, upcoming;
	std::unordered_set<std::string> seen;
	const int64_t ahead_limit = now + static_cast<int64_t>(ahead_days) * 86400;
	// A scheduled match whose kick-off passed a little ago is about to start.
	const int64_t late_limit = now - 3 * 3600;

	for (const Match &match : all) {
		if (!seen.insert(match.id).second)
			continue;
		if (match.in_progress())
			live.push_back(match);
		else if (live_only || match.state == MatchState::Finished)
			continue;
		else if (match.state == MatchState::Postponed ? match.start_utc >= now : match.start_utc >= late_limit)
			upcoming.push_back(match);
	}

	const auto by_kickoff = [](const Match &a, const Match &b) { return a.start_utc < b.start_utc; };
	std::stable_sort(live.begin(), live.end(), by_kickoff);
	std::stable_sort(upcoming.begin(), upcoming.end(), by_kickoff);
	// Keep the window, but never less than the next few matches: after an
	// international break the next round can be weeks away.
	size_t keep = 0;
	while (keep < upcoming.size() && (upcoming[keep].start_utc <= ahead_limit || keep < kMinUpcoming))
		++keep;
	upcoming.resize(keep);

	std::vector<Match> result = std::move(live);
	result.insert(result.end(), upcoming.begin(), upcoming.end());
	return result;
}

std::optional<Match> automatic_match(const std::vector<Match> &all, int64_t now)
{
	const Match *live = nullptr;
	const Match *next = nullptr;
	const Match *last = nullptr;
	const Match *just_finished = nullptr;
	for (const Match &match : all) {
		if (match.in_progress()) {
			if (!live || match.start_utc < live->start_utc)
				live = &match;
		} else if (match.state == MatchState::Finished && match.start_utc >= now - kKeepFinishedSeconds) {
			// Leave the final score up for a while before moving on.
			if (!just_finished || match.start_utc > just_finished->start_utc)
				just_finished = &match;
			if (!last || match.start_utc > last->start_utc)
				last = &match;
		} else if (match.state == MatchState::Scheduled) {
			// A scheduled match whose kick-off just passed is about to start.
			if (match.start_utc >= now - 3 * 3600 && (!next || match.start_utc < next->start_utc))
				next = &match;
		} else if (match.state == MatchState::Finished) {
			if (!last || match.start_utc > last->start_utc)
				last = &match;
		}
	}
	if (live)
		return *live;
	if (just_finished)
		return *just_finished;
	if (next)
		return *next;
	if (last)
		return *last;
	return std::nullopt;
}

int poll_interval_seconds(const Match &match, int64_t now)
{
	switch (match.state) {
	case MatchState::Live:
	case MatchState::HalfTime:
	case MatchState::Interrupted:
		return 15;
	case MatchState::Scheduled: {
		const int64_t until = match.start_utc - now;
		if (until <= 15 * 60)
			return 20; // about to start, or late
		if (until <= 2 * 3600)
			return 120;
		return 600;
	}
	case MatchState::Finished:
	case MatchState::Postponed:
		return 600;
	}
	return 60;
}

int idle_poll_interval_seconds(bool last_failed)
{
	return last_failed ? 30 : 300;
}

} // namespace fscore
