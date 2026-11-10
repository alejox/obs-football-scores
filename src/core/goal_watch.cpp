#include "core/goal_watch.hpp"

namespace fscore {

std::optional<GoalEvent> GoalWatch::observe(const Match &match)
{
	const int home = match.home.score.value_or(0);
	const int away = match.away.score.value_or(0);

	if (!has_baseline_ || match.id != match_id_) {
		match_id_ = match.id;
		home_ = home;
		away_ = away;
		has_baseline_ = true;
		return std::nullopt;
	}

	const bool home_up = home > home_;
	const bool away_up = away > away_;
	const bool corrected = home < home_ || away < away_;
	home_ = home;
	away_ = away;
	if (corrected || (!home_up && !away_up))
		return std::nullopt;

	GoalEvent event;
	event.side = home_up ? Side::Home : Side::Away;

	// The feed lists goals in match order; credit the latest one, which also
	// decides the side when both scores rose between two snapshots.
	const std::string &home_id = match.home.id;
	const std::string &away_id = match.away.id;
	for (auto it = match.goals.rbegin(); it != match.goals.rend(); ++it) {
		const bool for_home = it->team_id == home_id;
		const bool for_away = it->team_id == away_id;
		if (!for_home && !for_away)
			continue;
		if (home_up && away_up)
			event.side = for_home ? Side::Home : Side::Away;
		else if ((event.side == Side::Home) != for_home)
			continue;
		event.scorer = it->scorer;
		event.scorer_photo_url = it->scorer_photo_url;
		event.minute = it->minute;
		event.own_goal = it->own_goal;
		event.penalty = it->penalty;
		break;
	}
	return event;
}

void GoalWatch::reset()
{
	match_id_.clear();
	home_ = away_ = 0;
	has_baseline_ = false;
}

} // namespace fscore
