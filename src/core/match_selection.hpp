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
// kick-off order, then the ones that
// finished in the last `behind_hours`, most recent first. Duplicated ids are
// dropped. A postponed match is listed only while its date is ahead.
std::vector<Match> listable_matches(const std::vector<Match> &all, int64_t now, int ahead_days = 10,
				    int behind_hours = 36);

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
