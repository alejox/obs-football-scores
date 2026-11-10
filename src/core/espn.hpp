// Pure core: no OBS, no Qt. ESPN's public soccer API (site.api.espn.com).
//
// The API is not documented. What the plugin relies on:
//   /apis/site/v2/sports/soccer/<league>/scoreboard?dates=YYYYMM    one month
//   /apis/site/v2/sports/soccer/<league>/scoreboard?dates=YYYYMMDD  one day
// Days are grouped by US Eastern time, so a match at 01:00 UTC is listed on the
// previous day; date ranges are refused.
#pragma once

#include "core/match.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fscore::espn {

inline constexpr const char *kApiBase = "https://site.api.espn.com/apis/site/v2/sports/soccer/";

std::string month_url(const std::string &league, int year, int month);
std::string day_url(const std::string &league, const std::string &yyyymmdd);

// The days whose scoreboard may list a match starting at `start_utc`, most
// likely first: the US Eastern date (standard time, then daylight time), then
// the UTC date. Duplicates are removed.
std::vector<std::string> candidate_days(int64_t start_utc);

// Parses a scoreboard answer. Returns nullopt when the body is not a
// scoreboard (bad JSON, an error object, ...). Events that cannot be read are
// skipped rather than failing the whole board.
std::optional<Scoreboard> parse_scoreboard(const std::string &body);

// ESPN logos sometimes come as http:// URLs; the plugin only fetches https.
std::string https_url(const std::string &url);

} // namespace fscore::espn
