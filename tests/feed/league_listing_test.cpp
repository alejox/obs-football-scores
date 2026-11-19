#include "feed/league_listing.hpp"

#include "core/espn.hpp"
#include "fake_http.hpp"
#include "test_harness.hpp"

using namespace fscore;
using test_harness::FakeHttp;

namespace {

std::string one_event(const std::string &id)
{
	return R"({"leagues":[{"name":"L"}],"events":[{"id":")" + id +
	       R"(","date":"2026-11-30T20:00Z","status":{"type":{"state":"pre"}},"competitions":[{"competitors":[{"homeAway":"home","team":{"id":"1"}},{"homeAway":"away","team":{"id":"2"}}]}]}]})";
}

} // namespace

TEST_CASE(reads_the_next_month_near_the_end)
{
	FakeHttp http;
	http.set(espn::month_url("esp.1", 2026, 12), one_event("dec"));
	http.set(espn::month_url("esp.1", 2027, 1), one_event("jan"));
	// 2026-12-28T12:00Z
	const auto board = fetch_league_matches("esp.1", 1798459200, http.get());
	REQUIRE(board.has_value());
	REQUIRE(board->matches.size() == 2u);
	CHECK_EQ(board->matches[0].id, std::string("dec"));
	CHECK_EQ(board->matches[1].id, std::string("jan"));
}

TEST_CASE(reads_the_previous_month_at_the_start)
{
	FakeHttp http;
	http.set(espn::month_url("esp.1", 2026, 12), one_event("dec"));
	// 2027-01-02T12:00Z: December and January; January fails.
	const auto board = fetch_league_matches("esp.1", 1798891200, http.get());
	REQUIRE(board.has_value());
	CHECK_EQ(board->matches.size(), 1u);
	const auto requests = http.requests();
	REQUIRE(requests.size() == 2u);
	CHECK_EQ(requests[0], espn::month_url("esp.1", 2026, 12));
	CHECK_EQ(requests[1], espn::month_url("esp.1", 2027, 1));
}

TEST_CASE(all_leagues_reads_the_day_boards)
{
	FakeHttp http;
	// 2026-11-08T13:00Z: one candidate day.
	http.set(espn::day_url("all", "20261108"), one_event("live-anywhere"));
	const auto board = fetch_league_matches("all", 1794142800, http.get());
	REQUIRE(board.has_value());
	REQUIRE(board->matches.size() == 1u);
	CHECK_EQ(board->matches[0].id, std::string("live-anywhere"));
	REQUIRE(http.requests().size() == 1u);
	CHECK_EQ(http.requests()[0], espn::day_url("all", "20261108"));
}

TEST_CASE(nothing_readable_is_nullopt)
{
	FakeHttp http;
	CHECK(!fetch_league_matches("esp.1", 1794142800, http.get()).has_value());
}
