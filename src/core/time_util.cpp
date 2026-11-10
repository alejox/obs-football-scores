#include "core/time_util.hpp"

#include <cstdio>

namespace fscore {
namespace {

// Howard Hinnant's days_from_civil / civil_from_days: exact for the proleptic
// Gregorian calendar, no time zone database needed.
int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

CivilDate civil_from_days(int64_t z)
{
	z += 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = static_cast<unsigned>(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t y = static_cast<int64_t>(yoe) + era * 400;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	const unsigned d = doy - (153 * mp + 2) / 5 + 1;
	const unsigned m = mp < 10 ? mp + 3 : mp - 9;
	return CivilDate{static_cast<int>(y + (m <= 2)), static_cast<int>(m), static_cast<int>(d)};
}

bool read_int(const std::string &text, size_t pos, size_t count, int &out)
{
	if (pos + count > text.size())
		return false;
	int value = 0;
	for (size_t i = pos; i < pos + count; ++i) {
		const char c = text[i];
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + (c - '0');
	}
	out = value;
	return true;
}

} // namespace

std::optional<int64_t> parse_iso_utc(const std::string &text)
{
	// YYYY-MM-DDTHH:MM, then optional :SS and fraction, then Z.
	int year, month, day, hour, minute, second = 0;
	if (text.size() < 17 || text[4] != '-' || text[7] != '-' || (text[10] != 'T' && text[10] != ' ') ||
	    text[13] != ':')
		return std::nullopt;
	if (!read_int(text, 0, 4, year) || !read_int(text, 5, 2, month) || !read_int(text, 8, 2, day) ||
	    !read_int(text, 11, 2, hour) || !read_int(text, 14, 2, minute))
		return std::nullopt;
	size_t pos = 16;
	if (pos < text.size() && text[pos] == ':') {
		if (!read_int(text, pos + 1, 2, second))
			return std::nullopt;
		pos += 3;
		if (pos < text.size() && text[pos] == '.') {
			++pos;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
				++pos;
		}
	}
	if (pos >= text.size() || text[pos] != 'Z')
		return std::nullopt;
	if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
		return std::nullopt;
	return days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 +
	       minute * 60 + second;
}

CivilDate utc_date(int64_t epoch_seconds)
{
	int64_t days = epoch_seconds / 86400;
	if (epoch_seconds % 86400 < 0)
		--days;
	return civil_from_days(days);
}

std::string yyyymmdd(const CivilDate &date)
{
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d", date.year, date.month, date.day);
	return buffer;
}

std::string yyyymm(int year, int month)
{
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "%04d%02d", year, month);
	return buffer;
}

} // namespace fscore
