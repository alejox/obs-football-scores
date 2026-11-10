#include "core/goal_watch.hpp"

#include "match_builders.hpp"
#include "test_harness.hpp"

using namespace fscore;
using test_harness::make_match;

namespace {

Match live(int home, int away)
{
	return make_match("m", 0, MatchState::Live, home, away);
}

GoalInfo goal_for(const Match &match, bool home, const std::string &scorer, const std::string &minute)
{
	GoalInfo goal;
	goal.team_id = home ? match.home.id : match.away.id;
	goal.scorer = scorer;
	goal.minute = minute;
	return goal;
}

} // namespace

TEST_CASE(the_first_snapshot_is_the_baseline)
{
	GoalWatch watch;
	CHECK(!watch.observe(live(2, 1)).has_value());
	CHECK(!watch.observe(live(2, 1)).has_value());
}

TEST_CASE(reports_who_scored)
{
	GoalWatch watch;
	watch.observe(live(0, 0));
	Match next = live(0, 1);
	next.goals.push_back(goal_for(next, false, "Lamine Yamal", "55'"));
	const auto event = watch.observe(next);
	REQUIRE(event.has_value());
	CHECK(event->side == Side::Away);
	CHECK_EQ(event->scorer, std::string("Lamine Yamal"));
	CHECK_EQ(event->minute, std::string("55'"));

	Match after = live(1, 1);
	after.goals = next.goals;
	after.goals.push_back(goal_for(after, true, "Mbappé", "70'"));
	const auto second = watch.observe(after);
	REQUIRE(second.has_value());
	CHECK(second->side == Side::Home);
	CHECK_EQ(second->scorer, std::string("Mbappé"));
}

TEST_CASE(a_goal_without_details_still_counts)
{
	GoalWatch watch;
	watch.observe(live(0, 0));
	const auto event = watch.observe(live(1, 0));
	REQUIRE(event.has_value());
	CHECK(event->side == Side::Home);
	CHECK(event->scorer.empty());
}

TEST_CASE(both_teams_scoring_between_snapshots_credits_the_latest)
{
	GoalWatch watch;
	watch.observe(live(0, 0));
	Match next = live(1, 1);
	next.goals.push_back(goal_for(next, true, "A", "10'"));
	next.goals.push_back(goal_for(next, false, "B", "12'"));
	const auto event = watch.observe(next);
	REQUIRE(event.has_value());
	CHECK(event->side == Side::Away);
	CHECK_EQ(event->scorer, std::string("B"));
}

TEST_CASE(a_disallowed_goal_is_not_an_event)
{
	GoalWatch watch;
	watch.observe(live(1, 0));
	CHECK(!watch.observe(live(0, 0)).has_value());
	// The baseline follows the correction.
	CHECK(watch.observe(live(1, 0)).has_value());
}

TEST_CASE(a_new_match_resets_the_baseline)
{
	GoalWatch watch;
	watch.observe(live(0, 0));
	Match other = make_match("other", 0, MatchState::Live, 3, 0);
	CHECK(!watch.observe(other).has_value());
	watch.reset();
	CHECK(!watch.observe(live(5, 5)).has_value());
}
