#include "core/goal_animation.hpp"

#include "test_harness.hpp"

using namespace fscore;

TEST_CASE(inactive_outside_its_window)
{
	CHECK(!goal_animation_frame(-0.1).active);
	CHECK(!goal_animation_frame(kGoalAnimationSeconds).active);
	CHECK(goal_animation_frame(0.0).active);
}

TEST_CASE(slides_in_holds_and_slides_out)
{
	CHECK_NEAR(goal_animation_frame(0.0).banner, 0.0, 1e-9);
	CHECK_NEAR(goal_animation_frame(0.0).flash, 1.0, 1e-9);
	CHECK_NEAR(goal_animation_frame(3.0).banner, 1.0, 1e-9);
	CHECK_NEAR(goal_animation_frame(3.0).text, 1.0, 1e-9);
	CHECK_NEAR(goal_animation_frame(3.0).flash, 0.0, 1e-9);
	CHECK(goal_animation_frame(kGoalAnimationSeconds - 0.05).banner < 0.3);
	CHECK(goal_animation_frame(0.2).banner > 0.5);
}

TEST_CASE(pulse_stays_in_range)
{
	for (double t = 0.0; t < kGoalAnimationSeconds; t += 0.05) {
		const GoalAnimationFrame frame = goal_animation_frame(t);
		CHECK(frame.pulse >= 0.0 && frame.pulse <= 1.0);
		CHECK(frame.banner >= 0.0 && frame.banner <= 1.0);
		CHECK(frame.text >= 0.0 && frame.text <= 1.0);
	}
}
