// Pure core: no OBS, no Qt.
#pragma once

namespace fscore {

inline constexpr double kGoalAnimationSeconds = 7.0;

// What the renderer draws at one moment of the goal animation.
struct GoalAnimationFrame {
	bool active = false;
	double banner = 0.0; // [0, 1]: how far the "GOAL" banner has slid over the board
	double flash = 0.0;  // [0, 1]: white flash strength, strongest right at the start
	double pulse = 0.0;  // [0, 1]: score box pulse, a few beats while the banner is up
	double text = 0.0;   // [0, 1]: opacity of the banner text
};

// Deterministic for a given time; inactive outside [0, kGoalAnimationSeconds).
GoalAnimationFrame goal_animation_frame(double elapsed_seconds);

} // namespace fscore
