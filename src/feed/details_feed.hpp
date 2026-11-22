// Feed adapter: plain C++ and one worker thread. Keeps one match's summary (stats,
// lineups, plays) and one league's table fresh for the panel source.
#pragma once

#include "core/cancel_token.hpp"
#include "core/http_types.hpp"
#include "core/match_details.hpp"
#include "core/standings.hpp"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fscore {

struct DetailsTarget {
	std::string summary_league; // ESPN slug for the summary ("esp.1", or "all")
	std::string event_id;       // empty: no summary
	std::string standings_league; // empty: no table
	bool live = false;          // the match is in play: refresh the summary often

	bool operator==(const DetailsTarget &o) const
	{
		return summary_league == o.summary_league && event_id == o.event_id &&
		       standings_league == o.standings_league && live == o.live;
	}
};

class DetailsFeed {
public:
	using Clock = std::function<int64_t()>; // seconds

	DetailsFeed(HttpGet http, Clock clock, bool start_thread = true);
	~DetailsFeed();

	DetailsFeed(const DetailsFeed &) = delete;
	DetailsFeed &operator=(const DetailsFeed &) = delete;

	// What to follow. A new match or league is read at once; only `live` changing just
	// adjusts the pace.
	void set_target(const DetailsTarget &target);

	struct Snapshot {
		std::shared_ptr<const MatchDetails> details;              // null until read
		std::shared_ptr<const std::vector<StandingsGroup>> standings; // null until read
		uint64_t revision = 0;
	};
	Snapshot snapshot() const;

	// One cycle (tests): reads what is due, returns the seconds until the next.
	int poll_once();

	static constexpr int kLiveSeconds = 15;
	static constexpr int kIdleSeconds = 120;
	static constexpr int kStandingsSeconds = 600;
	static constexpr int kRetrySeconds = 30;

private:
	void run();

	const HttpGet http_;
	const Clock clock_;

	mutable std::mutex mutex_;
	DetailsTarget target_;
	Snapshot snapshot_;
	int64_t summary_due_ = 0;
	int64_t standings_due_ = 0;
	std::string summary_for_;   // event the stored summary belongs to
	std::string standings_for_; // league the stored table belongs to

	std::condition_variable wake_;
	bool stop_ = false;
	bool wake_requested_ = false;
	CancelToken cancel_;
	std::thread worker_;
};

} // namespace fscore
