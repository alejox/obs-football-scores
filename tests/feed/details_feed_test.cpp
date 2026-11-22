#include "feed/details_feed.hpp"

#include "core/espn.hpp"

#include "fake_http.hpp"
#include "fixtures.hpp"
#include "test_harness.hpp"

using namespace fscore;
using test_harness::FakeHttp;

namespace {

const std::string kSummary = summary_url("esp.1", "401882816");
const std::string kTable = standings_url("esp.1");

} // namespace

TEST_CASE(reads_the_summary_and_the_table)
{
	FakeHttp http;
	http.set(kSummary, test_harness::read_fixture("esp1_summary.json"));
	http.set(kTable, test_harness::read_fixture("esp1_standings.json"));
	int64_t now = 1000;
	DetailsFeed feed(http.get(), [&now] { return now; }, false);
	CHECK(!feed.snapshot().details);

	feed.set_target({"esp.1", "401882816", "esp.1", true});
	CHECK_EQ(feed.poll_once(), DetailsFeed::kLiveSeconds);
	const auto snap = feed.snapshot();
	REQUIRE(snap.details != nullptr);
	CHECK_EQ(snap.details->home_name, std::string("Atlético Madrid"));
	REQUIRE(snap.standings != nullptr);
	CHECK_EQ(snap.standings->front().rows.size(), 20u);

	// Nothing due yet: no request.
	http.clear_requests();
	now += 5;
	feed.poll_once();
	CHECK(http.requests().empty());
	// The summary again after 15 s, the table only after 10 min.
	now += 10;
	feed.poll_once();
	REQUIRE(http.requests().size() == 1u);
	CHECK_EQ(http.requests()[0], kSummary);
}

TEST_CASE(a_finished_match_is_read_rarely)
{
	FakeHttp http;
	http.set(kSummary, test_harness::read_fixture("esp1_summary.json"));
	int64_t now = 0;
	DetailsFeed feed(http.get(), [&now] { return now; }, false);
	feed.set_target({"esp.1", "401882816", "", false});
	CHECK_EQ(feed.poll_once(), DetailsFeed::kIdleSeconds);
}

TEST_CASE(a_new_match_drops_the_old_data_and_reads_at_once)
{
	FakeHttp http;
	http.set(kSummary, test_harness::read_fixture("esp1_summary.json"));
	int64_t now = 0;
	DetailsFeed feed(http.get(), [&now] { return now; }, false);
	feed.set_target({"esp.1", "401882816", "", true});
	feed.poll_once();
	REQUIRE(feed.snapshot().details != nullptr);
	feed.set_target({"esp.1", "999", "", true});
	CHECK(feed.snapshot().details == nullptr);
	http.clear_requests();
	feed.poll_once();
	REQUIRE(http.requests().size() == 1u);
	CHECK_EQ(http.requests()[0], summary_url("esp.1", "999"));
}

TEST_CASE(failures_retry_sooner_and_keep_nothing_stale)
{
	FakeHttp http;
	http.fail(kSummary);
	int64_t now = 0;
	DetailsFeed feed(http.get(), [&now] { return now; }, false);
	feed.set_target({"esp.1", "401882816", "", true});
	CHECK_EQ(feed.poll_once(), DetailsFeed::kRetrySeconds);
	CHECK(feed.snapshot().details == nullptr);
}

TEST_CASE(the_worker_thread_starts_and_stops)
{
	FakeHttp http;
	http.set(kSummary, test_harness::read_fixture("esp1_summary.json"));
	DetailsFeed feed(http.get(), [] { return int64_t(0); });
	feed.set_target({"esp.1", "401882816", "", false});
	for (int i = 0; i < 300 && !feed.snapshot().details; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	CHECK(feed.snapshot().details != nullptr);
}

TEST_CASE(reads_the_scorers_and_the_round)
{
	FakeHttp http;
	http.set(leaders_url("esp.1"), test_harness::read_fixture("esp1_leaders.json"));
	http.set(espn::day_url("esp.1", espn::candidate_days(1794142800).front()), test_harness::read_fixture("esp1_day.json"));
	int64_t now = 1794142800;
	DetailsFeed feed(http.get(), [&now] { return now; }, false);
	DetailsTarget target;
	target.leaders_league = "esp.1";
	target.round_league = "esp.1";
	feed.set_target(target);
	CHECK_EQ(feed.poll_once(), DetailsFeed::kRoundIdleSeconds); // no live match in the fixture
	const auto snap = feed.snapshot();
	REQUIRE(snap.leaders != nullptr);
	CHECK_EQ(snap.leaders->goals.front().name, std::string("Raphinha"));
	REQUIRE(snap.round != nullptr);
	CHECK_EQ(snap.round->matches.size(), 5u);
}
