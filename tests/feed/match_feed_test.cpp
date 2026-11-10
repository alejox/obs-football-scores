#include "feed/match_feed.hpp"

#include "core/espn.hpp"
#include "fake_http.hpp"
#include "test_harness.hpp"

#include <string>

using namespace fscore;
using test_harness::FakeHttp;

namespace {

constexpr int64_t kNow = 1794142800; // 2026-11-08T13:00Z, mid month

const std::string kMonth = espn::month_url("esp.1", 2026, 11);
const std::string kDay = espn::day_url("esp.1", "20261108");

std::string board(const std::string &id, const std::string &state, int home, int away,
		  const std::string &date = "2026-11-08T12:00Z")
{
	const std::string name = state == "in" ? "STATUS_SECOND_HALF" : state == "pre" ? "STATUS_SCHEDULED" : "STATUS_FULL_TIME";
	return R"({"leagues":[{"name":"Spanish LALIGA","logos":[{"href":"https://a.espncdn.com/l.png"}]}],"events":[{"id":")" +
	       id + R"(","date":")" + date + R"(","status":{"displayClock":"60'","type":{"name":")" + name +
	       R"(","state":")" + state + R"("}},"competitions":[{"competitors":[{"homeAway":"home","score":")" +
	       std::to_string(home) +
	       R"(","team":{"id":"1","displayName":"Real Madrid"}},{"homeAway":"away","score":")" +
	       std::to_string(away) + R"(","team":{"id":"2","displayName":"Barcelona"}}]}]}]})";
}

} // namespace

TEST_CASE(automatic_lists_the_month_then_polls_the_day)
{
	FakeHttp http;
	http.set(kMonth, board("7", "in", 1, 0));
	http.set(kDay, board("7", "in", 2, 0));
	MatchFeed feed({"esp.1", ""}, http.get(), [] { return kNow; }, false);
	CHECK(feed.snapshot().status == FeedStatus::Loading);

	CHECK_EQ(feed.poll_once(), 15);
	FeedSnapshot snap = feed.snapshot();
	CHECK(snap.status == FeedStatus::Ok);
	REQUIRE(snap.match.has_value());
	CHECK_EQ(snap.match->home.score.value_or(-1), 1);
	CHECK_EQ(snap.league_name, std::string("Spanish LALIGA"));
	CHECK_EQ(snap.league_logo_url, std::string("https://a.espncdn.com/l.png"));

	http.clear_requests();
	feed.poll_once();
	snap = feed.snapshot();
	CHECK_EQ(snap.match->home.score.value_or(-1), 2);
	REQUIRE(http.requests().size() == 1u);
	CHECK_EQ(http.requests()[0], kDay);
}

TEST_CASE(a_chosen_match_is_found_by_id)
{
	FakeHttp http;
	http.set(kMonth, board("7", "pre", 0, 0, "2026-11-22T20:00Z"));
	MatchFeed feed({"esp.1", "7"}, http.get(), [] { return kNow; }, false);
	feed.poll_once();
	CHECK(feed.snapshot().status == FeedStatus::Ok);
	CHECK_EQ(feed.snapshot().match->id, std::string("7"));

	MatchFeed missing({"esp.1", "8"}, http.get(), [] { return kNow; }, false);
	missing.poll_once();
	CHECK(missing.snapshot().status == FeedStatus::NotFound);
	CHECK(!missing.snapshot().match.has_value());
}

TEST_CASE(a_failed_read_keeps_the_last_data)
{
	FakeHttp http;
	http.set(kMonth, board("7", "in", 1, 0));
	MatchFeed feed({"esp.1", ""}, http.get(), [] { return kNow; }, false);
	feed.poll_once();
	http.fail(kDay);
	const int wait = feed.poll_once();
	CHECK(wait <= 30);
	const FeedSnapshot snap = feed.snapshot();
	CHECK(snap.status == FeedStatus::Error);
	REQUIRE(snap.match.has_value());
	CHECK_EQ(snap.match->home.score.value_or(-1), 1);
}

TEST_CASE(an_empty_league_reports_no_matches)
{
	FakeHttp http;
	http.set(kMonth, R"({"events":[]})");
	MatchFeed feed({"esp.1", ""}, http.get(), [] { return kNow; }, false);
	CHECK_EQ(feed.poll_once(), 300);
	CHECK(feed.snapshot().status == FeedStatus::NoMatches);
}

TEST_CASE(offline_at_start_is_an_error_and_retries_soon)
{
	FakeHttp http;
	http.fail(kMonth);
	MatchFeed feed({"esp.1", ""}, http.get(), [] { return kNow; }, false);
	CHECK_EQ(feed.poll_once(), 30);
	CHECK(feed.snapshot().status == FeedStatus::Error);
}

TEST_CASE(the_worker_thread_publishes_and_stops)
{
	FakeHttp http;
	http.set(kMonth, board("7", "post", 3, 1));
	{
		MatchFeed feed({"esp.1", ""}, http.get(), [] { return kNow; });
		for (int i = 0; i < 200 && feed.snapshot().status == FeedStatus::Loading; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		CHECK(feed.snapshot().status == FeedStatus::Ok);
	} // the destructor joins while the worker waits for 600 s
}
