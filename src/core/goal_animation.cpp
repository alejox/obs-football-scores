#include "core/goal_animation.hpp"

#include <algorithm>
#include <cmath>

namespace fscore {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSlideIn = 0.45;  // seconds
constexpr double kSlideOut = 0.55; // seconds

double ease_out_cubic(double t)
{
	t = std::clamp(t, 0.0, 1.0);
	const double u = 1.0 - t;
	return 1.0 - u * u * u;
}

double ease_in_cubic(double t)
{
	t = std::clamp(t, 0.0, 1.0);
	return t * t * t;
}

} // namespace

GoalAnimationFrame goal_animation_frame(double t)
{
	GoalAnimationFrame frame;
	if (!(t >= 0.0) || t >= kGoalAnimationSeconds)
		return frame;
	frame.active = true;

	const double out_start = kGoalAnimationSeconds - kSlideOut;
	if (t < kSlideIn)
		frame.banner = ease_out_cubic(t / kSlideIn);
	else if (t < out_start)
		frame.banner = 1.0;
	else
		frame.banner = 1.0 - ease_in_cubic((t - out_start) / kSlideOut);

	frame.flash = std::max(0.0, 1.0 - t / 0.35);
	frame.text = std::clamp((t - kSlideIn * 0.6) / 0.25, 0.0, 1.0) * frame.banner;
	// Two beats per second while the banner is fully up.
	if (t >= kSlideIn && t < out_start)
		frame.pulse = 0.5 - 0.5 * std::cos((t - kSlideIn) * 2.0 * kPi * 2.0);
	return frame;
}

} // namespace fscore
