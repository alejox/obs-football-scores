#include "obs/services.hpp"

#include "feed/league_listing.hpp"
#include "net/net.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>

namespace fscore::obs {
namespace {

// Requests made while OBS waits on them (the properties dialog) stay short.
constexpr int kRequestTimeoutMs = 8000;
constexpr int64_t kListingCacheSeconds = 120;

struct CachedListing {
	int64_t read_at = 0;
	Scoreboard board;
};

std::mutex services_mutex;
std::unique_ptr<ImageStore> images;

std::mutex listing_mutex;
std::map<std::string, CachedListing> listings;

} // namespace

HttpGet http_get()
{
	return [](const std::string &url, CancelToken *cancel) {
		const net::Response response = net::get(url, {{"Accept", "application/json, image/*"}}, kRequestTimeoutMs,
							 cancel);
		HttpResult result;
		result.status = response.status;
		result.body = response.body;
		result.error = response.error;
		return result;
	};
}

int64_t now_seconds()
{
	using namespace std::chrono;
	return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

void start_services()
{
	std::lock_guard lock(services_mutex);
	if (!images)
		images = std::make_unique<ImageStore>(http_get(), now_seconds);
}

void stop_services()
{
	std::unique_ptr<ImageStore> stopping;
	{
		std::lock_guard lock(services_mutex);
		stopping = std::move(images);
	}
	stopping.reset(); // joins the downloader outside the lock
	std::lock_guard lock(listing_mutex);
	listings.clear();
}

ImageStore *image_store()
{
	std::lock_guard lock(services_mutex);
	return images.get();
}

std::optional<Scoreboard> league_matches(const std::string &league, bool force_refresh)
{
	if (league.empty())
		return std::nullopt;
	const int64_t now = now_seconds();
	{
		std::lock_guard lock(listing_mutex);
		const auto it = listings.find(league);
		if (!force_refresh && it != listings.end() && now - it->second.read_at < kListingCacheSeconds)
			return it->second.board;
	}
	auto board = fetch_league_matches(league, now, http_get());
	if (!board)
		return std::nullopt;
	std::lock_guard lock(listing_mutex);
	listings[league] = CachedListing{now, *board};
	return board;
}

} // namespace fscore::obs
