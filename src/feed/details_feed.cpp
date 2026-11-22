#include "feed/details_feed.hpp"

#include "core/espn.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace fscore {

DetailsFeed::DetailsFeed(HttpGet http, Clock clock, bool start_thread) : http_(std::move(http)), clock_(std::move(clock))
{
	if (start_thread)
		worker_ = std::thread([this] { run(); });
}

DetailsFeed::~DetailsFeed()
{
	{
		std::lock_guard lock(mutex_);
		stop_ = true;
	}
	cancel_.cancel();
	wake_.notify_all();
	if (worker_.joinable())
		worker_.join();
}

void DetailsFeed::set_target(const DetailsTarget &target)
{
	std::lock_guard lock(mutex_);
	if (target == target_)
		return;
	const bool new_match = target.event_id != target_.event_id || target.summary_league != target_.summary_league;
	const bool new_table = target.standings_league != target_.standings_league;
	target_ = target;
	if (new_match) {
		summary_due_ = 0;
		if (target.event_id != summary_for_) {
			snapshot_.details.reset();
			++snapshot_.revision;
		}
	}
	if (new_table) {
		standings_due_ = 0;
		if (target.standings_league != standings_for_) {
			snapshot_.standings.reset();
			++snapshot_.revision;
		}
	}
	if (target.leaders_league != leaders_for_) {
		leaders_due_ = 0;
		if (snapshot_.leaders) {
			snapshot_.leaders.reset();
			++snapshot_.revision;
		}
	}
	if (target.round_league != round_for_) {
		round_due_ = 0;
		if (snapshot_.round) {
			snapshot_.round.reset();
			++snapshot_.revision;
		}
	}
	if (target.live && summary_due_ > clock_() + kLiveSeconds)
		summary_due_ = 0;
	wake_requested_ = true;
	wake_.notify_all();
}

DetailsFeed::Snapshot DetailsFeed::snapshot() const
{
	std::lock_guard lock(mutex_);
	return snapshot_;
}

int DetailsFeed::poll_once()
{
	DetailsTarget target;
	int64_t summary_due, standings_due, leaders_due, round_due;
	{
		std::lock_guard lock(mutex_);
		target = target_;
		summary_due = summary_due_;
		standings_due = standings_due_;
		leaders_due = leaders_due_;
		round_due = round_due_;
	}
	const int64_t now = clock_();

	if (!target.event_id.empty() && now >= summary_due) {
		const HttpResult result = http_(summary_url(target.summary_league, target.event_id), &cancel_);
		auto details = result.ok() ? parse_summary(result.body) : std::nullopt;
		std::lock_guard lock(mutex_);
		if (target_.event_id == target.event_id) {
			if (details) {
				snapshot_.details = std::make_shared<const MatchDetails>(std::move(*details));
				summary_for_ = target.event_id;
				++snapshot_.revision;
			}
			summary_due_ = now + (!details ? kRetrySeconds : target_.live ? kLiveSeconds : kIdleSeconds);
		}
	}
	if (!target.standings_league.empty() && now >= standings_due) {
		const HttpResult result = http_(standings_url(target.standings_league), &cancel_);
		auto groups = result.ok() ? parse_standings(result.body) : std::nullopt;
		std::lock_guard lock(mutex_);
		if (target_.standings_league == target.standings_league) {
			if (groups) {
				snapshot_.standings = std::make_shared<const std::vector<StandingsGroup>>(std::move(*groups));
				standings_for_ = target.standings_league;
				++snapshot_.revision;
			}
			standings_due_ = now + (groups ? kStandingsSeconds : kRetrySeconds);
		}
	}

	if (!target.leaders_league.empty() && now >= leaders_due) {
		const HttpResult result = http_(leaders_url(target.leaders_league), &cancel_);
		auto boards = result.ok() ? parse_leaders(result.body) : std::nullopt;
		std::lock_guard lock(mutex_);
		if (target_.leaders_league == target.leaders_league) {
			if (boards) {
				snapshot_.leaders = std::make_shared<const LeaderBoards>(std::move(*boards));
				leaders_for_ = target.leaders_league;
				++snapshot_.revision;
			}
			leaders_due_ = now + (boards ? kLeadersSeconds : kRetrySeconds);
		}
	}
	if (!target.round_league.empty() && now >= round_due) {
		// ESPN's day is the US Eastern one.
		const std::string day = espn::candidate_days(now).front();
		const HttpResult result = http_(espn::day_url(target.round_league, day), &cancel_);
		auto board = result.ok() ? espn::parse_scoreboard(result.body) : std::nullopt;
		std::lock_guard lock(mutex_);
		if (target_.round_league == target.round_league) {
			bool live = false;
			if (board) {
				for (const Match &match : board->matches)
					live = live || match.in_progress();
				snapshot_.round = std::make_shared<const Scoreboard>(std::move(*board));
				round_for_ = target.round_league;
				++snapshot_.revision;
			}
			round_due_ = now + (!board ? kRetrySeconds : live ? kRoundLiveSeconds : kRoundIdleSeconds);
		}
	}

	std::lock_guard lock(mutex_);
	int64_t next = now + kStandingsSeconds;
	if (!target_.leaders_league.empty())
		next = std::min(next, leaders_due_);
	if (!target_.round_league.empty())
		next = std::min(next, round_due_);
	if (!target_.event_id.empty())
		next = std::min(next, summary_due_);
	if (!target_.standings_league.empty())
		next = std::min(next, standings_due_);
	return static_cast<int>(std::clamp<int64_t>(next - now, 1, kStandingsSeconds));
}

void DetailsFeed::run()
{
	for (;;) {
		const int wait = poll_once();
		std::unique_lock lock(mutex_);
		wake_.wait_for(lock, std::chrono::seconds(wait), [this] { return stop_ || wake_requested_; });
		if (stop_)
			return;
		wake_requested_ = false;
	}
}

} // namespace fscore
