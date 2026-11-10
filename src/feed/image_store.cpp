#include "feed/image_store.hpp"

#include <utility>

namespace fscore {
namespace {

constexpr int64_t kRetryAfterSeconds = 300;
// Crests are a few dozen KB; anything this large is not an image we want.
constexpr size_t kMaxImageBytes = 4 * 1024 * 1024;

} // namespace

ImageStore::ImageStore(HttpGet http, Clock clock, bool start_thread) : http_(std::move(http)), clock_(std::move(clock))
{
	if (start_thread)
		worker_ = std::thread([this] { run(); });
}

ImageStore::~ImageStore()
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

std::shared_ptr<const std::string> ImageStore::get(const std::string &url)
{
	if (url.rfind("https://", 0) != 0)
		return nullptr;
	std::lock_guard lock(mutex_);
	Entry &entry = entries_[url];
	if (entry.bytes)
		return entry.bytes;
	if (!entry.pending && clock_() >= entry.retry_at) {
		entry.pending = true;
		queue_.push_back(url);
		wake_.notify_one();
	}
	return nullptr;
}

bool ImageStore::fetch_one()
{
	std::string url;
	{
		std::lock_guard lock(mutex_);
		if (queue_.empty())
			return false;
		url = std::move(queue_.front());
		queue_.pop_front();
	}

	const HttpResult result = http_(url, &cancel_);
	const bool ok = result.ok() && !result.body.empty() && result.body.size() <= kMaxImageBytes;
	{
		std::lock_guard lock(mutex_);
		Entry &entry = entries_[url];
		entry.pending = false;
		if (ok)
			entry.bytes = std::make_shared<const std::string>(result.body);
		else
			entry.retry_at = clock_() + kRetryAfterSeconds;
	}
	++generation_;
	return true;
}

void ImageStore::run()
{
	for (;;) {
		{
			std::unique_lock lock(mutex_);
			wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
			if (stop_)
				return;
		}
		fetch_one();
	}
}

} // namespace fscore
