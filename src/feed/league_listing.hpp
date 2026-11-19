// Feed adapter: plain C++, the HTTP call is injected.
#pragma once

#include "core/http_types.hpp"
#include "core/match.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace fscore {

// Every match ESPN lists for `league` in the month of `now`, plus the previous
// month in its first days and the next month in its last days, so that the
// list always covers a few days on each side. nullopt when no month could be
// read (network down, unknown league, ...). For kAllLeagues: every match ESPN
// lists today, from all leagues.
std::optional<Scoreboard> fetch_league_matches(const std::string &league, int64_t now, const HttpGet &http,
					       CancelToken *cancel = nullptr);

} // namespace fscore
