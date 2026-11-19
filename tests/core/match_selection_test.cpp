#include "core/match_selection.hpp"

#include "match_builders.hpp"
#include "test_harness.hpp"

using namespace fscore;
using test_harness::make_match;

namespace {

constexpr int64_t kNow = 1794142800; // 2026-11-08T13:00Z
constexpr int64_t kHour = 3600;
constexpr int64_t kDay = 86400;

} // namespace

TEST_CASE(lists_live_then_upcoming_and_drops_finished)
{
	const std::vector<Match> all = {
		make_match("old", kNow - 5 * kDay, MatchState::Finished, 1, 0),
		make_match("next2", kNow + 2 * kDay, MatchState::Scheduled),
		make_match("done", kNow - 20 * kHour, MatchState::Finished, 2, 2),
		make_match("live", kNow - kHour, MatchState::Live, 1, 0),
		make_match("next1", kNow + 3 * kHour, MatchState::Scheduled),
		make_match("far", kNow + 30 * kDay, MatchState::Scheduled),
		make_match("live", kNow - kHour, MatchState::Live, 1, 0), // duplicate from a second month
		make_match("ht", kNow - 2 * kHour, MatchState::HalfTime, 0, 0),
		make_match("postponed", kNow - 3 * kHour, MatchState::Postponed),
		make_match("stale", kNow - 10 * kHour, MatchState::Scheduled), // never started: dropped
	};
	const auto listed = listable_matches(all, kNow);
	REQUIRE(listed.size() == 5u);
	CHECK_EQ(listed[0].id, std::string("ht"));
	CHECK_EQ(listed[1].id, std::string("live"));
	CHECK_EQ(listed[2].id, std::string("next1"));
	CHECK_EQ(listed[3].id, std::string("next2"));
	CHECK_EQ(listed[4].id, std::string("far")); // fewer than ten upcoming: kept past the window
}

TEST_CASE(live_only_keeps_just_the_matches_in_play)
{
	const std::vector<Match> all = {
		make_match("next", kNow + kHour, MatchState::Scheduled),
		make_match("live", kNow - kHour, MatchState::Live, 1, 0),
		make_match("done", kNow - 2 * kHour, MatchState::Finished, 1, 1),
		make_match("ht", kNow - kHour, MatchState::HalfTime, 0, 0),
	};
	const auto listed = listable_matches(all, kNow, true);
	REQUIRE(listed.size() == 2u);
	CHECK_EQ(listed[0].id, std::string("live"));
	CHECK_EQ(listed[1].id, std::string("ht"));
	CHECK(listable_matches({make_match("done", kNow, MatchState::Finished)}, kNow, true).empty());
}

TEST_CASE(the_window_applies_once_there_are_enough_upcoming_matches)
{
	std::vector<Match> all;
	for (int i = 0; i < 12; ++i)
		all.push_back(make_match("far" + std::to_string(i), kNow + (20 + i) * kDay, MatchState::Scheduled));
	all.push_back(make_match("soon", kNow + kDay, MatchState::Scheduled));
	const auto listed = listable_matches(all, kNow);
	REQUIRE(listed.size() == 10u);
	CHECK_EQ(listed[0].id, std::string("soon"));
	CHECK_EQ(listed[9].id, std::string("far8"));
}

TEST_CASE(automatic_prefers_a_live_match)
{
	const std::vector<Match> all = {
		make_match("next", kNow + kHour, MatchState::Scheduled),
		make_match("live", kNow - kHour, MatchState::Live, 0, 0),
		make_match("just", kNow - 2 * kHour, MatchState::Finished, 1, 1),
	};
	CHECK_EQ(automatic_match(all, kNow)->id, std::string("live"));
}

TEST_CASE(automatic_keeps_a_fresh_result_before_the_next_match)
{
	const std::vector<Match> all = {
		make_match("next", kNow + kHour, MatchState::Scheduled),
		make_match("just", kNow - 2 * kHour, MatchState::Finished, 1, 1),
	};
	CHECK_EQ(automatic_match(all, kNow)->id, std::string("just"));
	// Hours later the next match takes over.
	CHECK_EQ(automatic_match(all, kNow + 3 * kHour - 1)->id, std::string("next"));
}

TEST_CASE(automatic_falls_back_to_the_last_result)
{
	const std::vector<Match> all = {
		make_match("a", kNow - 3 * kDay, MatchState::Finished, 1, 0),
		make_match("b", kNow - 2 * kDay, MatchState::Finished, 0, 3),
	};
	CHECK_EQ(automatic_match(all, kNow)->id, std::string("b"));
	CHECK(!automatic_match({}, kNow).has_value());
}

TEST_CASE(polls_faster_around_and_during_the_match)
{
	CHECK_EQ(poll_interval_seconds(make_match("l", kNow, MatchState::Live), kNow), 15);
	CHECK_EQ(poll_interval_seconds(make_match("h", kNow, MatchState::HalfTime), kNow), 15);
	CHECK_EQ(poll_interval_seconds(make_match("s", kNow + 10 * 60, MatchState::Scheduled), kNow), 20);
	CHECK_EQ(poll_interval_seconds(make_match("s", kNow - 10 * 60, MatchState::Scheduled), kNow), 20);
	CHECK_EQ(poll_interval_seconds(make_match("s", kNow + kHour, MatchState::Scheduled), kNow), 120);
	CHECK_EQ(poll_interval_seconds(make_match("s", kNow + kDay, MatchState::Scheduled), kNow), 600);
	CHECK_EQ(poll_interval_seconds(make_match("f", kNow, MatchState::Finished), kNow), 600);
	CHECK(idle_poll_interval_seconds(true) < idle_poll_interval_seconds(false));
}
