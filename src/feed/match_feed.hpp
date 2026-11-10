// Feed adapter: plain C++ and one worker thread. HTTP and the clock are
// injected so the tests can drive it with fakes.
#pragma once

#include "core/cancel_token.hpp"
#include "core/http_types.hpp"
#include "core/match.hpp"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace fscore {

struct FeedConfig {
	std::string league;   // ESPN slug: "esp.1"
	std::string match_id; // empty = automatic (live, else next, else last)
};

enum class FeedStatus {
	Loading,   // nothing read yet
	Ok,        // `match` is current
	NoMatches, // the league has no match to show right now
	NotFound,  // the chosen match is no longer listed
	Error,     // the last read failed; `match` may hold older data
};

struct FeedSnapshot {
	FeedStatus status = FeedStatus::Loading;
	std::optional<Match> match;
	std::string league_name;
	std::string league_logo_url;
	uint64_t revision = 0; // bumped on every change
};

using Clock = std::function<int64_t()>; // seconds since the Unix epoch

class MatchFeed {
public:
	// Starts polling at once. With `start_thread` false nothing runs on its own
	// and the tests call poll_once().
	MatchFeed(FeedConfig config, HttpGet http, Clock clock, bool start_thread = true);
	~MatchFeed(); // stops and joins the worker, aborting a request in flight

	MatchFeed(const MatchFeed &) = delete;
	MatchFeed &operator=(const MatchFeed &) = delete;

	FeedSnapshot snapshot() const;
	const FeedConfig &config() const { return config_; }

	// Wakes the worker for an immediate refresh.
	void refresh_now();

	// One refresh cycle; returns the seconds to wait before the next one.
	int poll_once();

private:
	void run();
	bool refresh_listing(int64_t now);
	bool refresh_day(int64_t now);
	void publish(FeedStatus status, std::optional<Match> match);

	const FeedConfig config_;
	const HttpGet http_;
	const Clock clock_;

	mutable std::mutex mutex_;
	FeedSnapshot snapshot_;

	// Worker state.
	std::optional<Match> current_;
	int64_t next_listing_at_ = 0;
	size_t day_hint_ = 0; // index into the candidate days that last held the match
	bool last_failed_ = false;

	std::mutex wake_mutex_;
	std::condition_variable wake_;
	bool wake_requested_ = false;
	bool stop_ = false;
	CancelToken cancel_;
	std::thread worker_;
};

} // namespace fscore
