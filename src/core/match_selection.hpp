// Pure core: no OBS, no Qt.
#pragma once

#include "core/match.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fscore {

// Matches worth offering in the "Match" list at `now`: live ones first, then
// the upcoming ones (within `ahead_days`, but at least the next ten) in
// kick-off order. Finished matches are left out, and so is everything but the
// live ones with `live_only`. Duplicated ids are dropped. A postponed match is
// listed only while its date is ahead.
std::vector<Match> listable_matches(const std::vector<Match> &all, int64_t now, bool live_only = false,
				    int ahead_days = 10);

// The match "Automatic" shows: the first live one, else one that finished in
// the last hours (its final score stays up), else the next one to kick off,
// else the most recently finished. nullopt for an empty list.
std::optional<Match> automatic_match(const std::vector<Match> &all, int64_t now);

// Seconds until the next refresh of a followed match at `now`.
int poll_interval_seconds(const Match &match, int64_t now);

// Seconds until the next refresh when there is no match yet (empty or failed
// listing): retries sooner after an error.
int idle_poll_interval_seconds(bool last_failed);

} // namespace fscore
