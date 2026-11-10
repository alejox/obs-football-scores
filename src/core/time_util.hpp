// Pure core: no OBS, no Qt.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace fscore {

struct CivilDate {
	int year = 1970;
	int month = 1; // 1-12
	int day = 1;   // 1-31
};

// Parses the feed's UTC timestamps: "2026-11-08T13:00Z", "2026-11-08T13:00:00Z",
// "2026-11-08T13:00:00.000Z". Returns seconds since the Unix epoch.
std::optional<int64_t> parse_iso_utc(const std::string &text);

// The UTC calendar date of an epoch time.
CivilDate utc_date(int64_t epoch_seconds);

// "20261108"
std::string yyyymmdd(const CivilDate &date);
// "202611"
std::string yyyymm(int year, int month);

} // namespace fscore
