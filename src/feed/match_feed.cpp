#include "feed/match_feed.hpp"

#include "core/espn.hpp"
#include "core/match_selection.hpp"
#include "feed/league_listing.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace fscore {
namespace {

// After a failed read the worker retries sooner than the normal cadence.
constexpr int kRetrySeconds = 30;

std::optional<Match> find_match(const std::vector<Match> &matches, const std::string &id)
{
	for (const Match &match : matches) {
		if (match.id == id)
			return match;
	}
	return std::nullopt;
}

} // namespace

MatchFeed::MatchFeed(FeedConfig config, HttpGet http, Clock clock, bool start_thread)
	: config_(std::move(config)),
	  http_(std::move(http)),
	  clock_(std::move(clock))
{
	if (config_.league.empty()) {
		snapshot_.status = FeedStatus::NoMatches;
		return; // nothing to follow
	}
	if (start_thread)
		worker_ = std::thread([this] { run(); });
}

MatchFeed::~MatchFeed()
{
	{
		std::lock_guard lock(wake_mutex_);
		stop_ = true;
	}
	cancel_.cancel();
	wake_.notify_all();
	if (worker_.joinable())
		worker_.join();
}

FeedSnapshot MatchFeed::snapshot() const
{
	std::lock_guard lock(mutex_);
	return snapshot_;
}

void MatchFeed::refresh_now()
{
	{
		std::lock_guard lock(wake_mutex_);
		wake_requested_ = true;
	}
	wake_.notify_all();
}

void MatchFeed::run()
{
	for (;;) {
		const int wait_seconds = poll_once();
		std::unique_lock lock(wake_mutex_);
		wake_.wait_for(lock, std::chrono::seconds(wait_seconds), [this] { return stop_ || wake_requested_; });
		if (stop_)
			return;
		wake_requested_ = false;
	}
}

int MatchFeed::poll_once()
{
	if (config_.league.empty())
		return idle_poll_interval_seconds(false);

	const int64_t now = clock_();
	const bool automatic = config_.match_id.empty();
	const bool ok = (!current_ || (automatic && now >= next_listing_at_)) ? refresh_listing(now) : refresh_day(now);
	last_failed_ = !ok;

	if (!current_)
		return idle_poll_interval_seconds(last_failed_);
	const int interval = poll_interval_seconds(*current_, now);
	return last_failed_ ? std::min(interval, kRetrySeconds) : interval;
}

bool MatchFeed::refresh_listing(int64_t now)
{
	const auto board = fetch_league_matches(config_.league, now, http_, &cancel_);
	if (!board) {
		publish(FeedStatus::Error, current_);
		return false;
	}
	{
		std::lock_guard lock(mutex_);
		snapshot_.league_name = board->league_name;
		snapshot_.league_logo_url = board->league_logo_url;
	}

	const bool automatic = config_.match_id.empty();
	std::optional<Match> chosen =
		automatic ? automatic_match(board->matches, now) : find_match(board->matches, config_.match_id);
	if (current_ && chosen && chosen->id != current_->id)
		day_hint_ = 0;
	current_ = std::move(chosen);
	// Automatic mode looks again from time to time: another match may start first.
	next_listing_at_ = now + ((current_ && current_->in_progress()) ? 30 * 60 : 10 * 60);

	if (!current_) {
		publish(automatic ? FeedStatus::NoMatches : FeedStatus::NotFound, std::nullopt);
		return true;
	}
	publish(FeedStatus::Ok, current_);
	return true;
}

bool MatchFeed::refresh_day(int64_t now)
{
	const std::vector<std::string> days = espn::candidate_days(current_->start_utc);
	bool any_read = false;
	for (size_t step = 0; step < days.size(); ++step) {
		const size_t index = (day_hint_ + step) % days.size();
		const HttpResult result = http_(espn::day_url(config_.league, days[index]), &cancel_);
		if (!result.ok())
			continue;
		const auto board = espn::parse_scoreboard(result.body);
		if (!board)
			continue;
		any_read = true;
		if (auto match = find_match(board->matches, current_->id)) {
			day_hint_ = index;
			current_ = std::move(match);
			publish(FeedStatus::Ok, current_);
			return true;
		}
	}
	if (!any_read) {
		publish(FeedStatus::Error, current_);
		return false;
	}
	// The match moved to another date: look it up in the month listing.
	return refresh_listing(now);
}

void MatchFeed::publish(FeedStatus status, std::optional<Match> match)
{
	std::lock_guard lock(mutex_);
	snapshot_.status = status;
	snapshot_.match = std::move(match);
	++snapshot_.revision;
}

} // namespace fscore
