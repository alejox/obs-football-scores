// Feed adapter: plain C++ and one worker thread. Downloads crests and flags
// once and keeps their bytes in memory for every source.
#pragma once

#include "core/cancel_token.hpp"
#include "core/http_types.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace fscore {

class ImageStore {
public:
	using Clock = std::function<int64_t()>;

	ImageStore(HttpGet http, Clock clock, bool start_thread = true);
	~ImageStore();

	ImageStore(const ImageStore &) = delete;
	ImageStore &operator=(const ImageStore &) = delete;

	// The downloaded bytes of `url`, or null while it is not available yet; the
	// first call queues the download. A failed download is retried after a few
	// minutes. Only https URLs are fetched.
	std::shared_ptr<const std::string> get(const std::string &url);

	// Bumped every time a download finishes, so callers know when to ask again.
	uint64_t generation() const { return generation_; }

	// Runs one queued download (tests). Returns false when the queue was empty.
	bool fetch_one();

private:
	struct Entry {
		std::shared_ptr<const std::string> bytes;
		bool pending = false;
		int64_t retry_at = 0; // after a failure
	};

	void run();

	const HttpGet http_;
	const Clock clock_;

	std::mutex mutex_;
	std::condition_variable wake_;
	std::unordered_map<std::string, Entry> entries_;
	std::deque<std::string> queue_;
	bool stop_ = false;
	std::atomic<uint64_t> generation_{0};
	CancelToken cancel_;
	std::thread worker_;
};

} // namespace fscore
