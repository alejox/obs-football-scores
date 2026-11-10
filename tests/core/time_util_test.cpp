#include "core/time_util.hpp"

#include "test_harness.hpp"

using namespace fscore;

TEST_CASE(parses_the_feed_formats)
{
	CHECK_EQ(parse_iso_utc("1970-01-01T00:00Z").value_or(-1), 0);
	CHECK_EQ(parse_iso_utc("2026-11-08T13:00Z").value_or(-1), 1794142800);
	CHECK_EQ(parse_iso_utc("2026-11-08T13:00:30Z").value_or(-1), 1794142830);
	CHECK_EQ(parse_iso_utc("2026-11-08T13:00:30.250Z").value_or(-1), 1794142830);
}

TEST_CASE(rejects_malformed_timestamps)
{
	CHECK(!parse_iso_utc(""));
	CHECK(!parse_iso_utc("2026-11-08"));
	CHECK(!parse_iso_utc("2026-11-08T13:00"));        // no zone
	CHECK(!parse_iso_utc("2026-11-08T13:00+01:00")); // only UTC
	CHECK(!parse_iso_utc("2026-13-08T13:00Z"));
	CHECK(!parse_iso_utc("2026-11-08T25:00Z"));
	CHECK(!parse_iso_utc("20x6-11-08T13:00Z"));
}

TEST_CASE(utc_date_round_trips)
{
	const CivilDate date = utc_date(1794142800);
	CHECK_EQ(date.year, 2026);
	CHECK_EQ(date.month, 11);
	CHECK_EQ(date.day, 8);
	CHECK_EQ(yyyymmdd(date), std::string("20261108"));
	CHECK_EQ(yyyymmdd(utc_date(1794142800 - 13 * 3600 - 1)), std::string("20261107"));
	CHECK_EQ(yyyymmdd(utc_date(parse_iso_utc("2028-02-29T23:59Z").value())), std::string("20280229"));
	CHECK_EQ(yyyymm(2026, 3), std::string("202603"));
}
