#include "feed/image_store.hpp"

#include "fake_http.hpp"
#include "test_harness.hpp"

using namespace fscore;
using test_harness::FakeHttp;

TEST_CASE(downloads_once_and_caches)
{
	FakeHttp http;
	http.set("https://x/a.png", "PNGDATA");
	int64_t now = 1000;
	ImageStore store(http.get(), [&now] { return now; }, false);

	CHECK(store.get("https://x/a.png") == nullptr); // queued
	CHECK(store.get("https://x/a.png") == nullptr); // not queued twice
	CHECK(store.fetch_one());
	CHECK(!store.fetch_one());
	CHECK_EQ(store.generation(), 1u);
	const auto bytes = store.get("https://x/a.png");
	REQUIRE(bytes != nullptr);
	CHECK_EQ(*bytes, std::string("PNGDATA"));
	CHECK_EQ(http.requests().size(), 1u);
}

TEST_CASE(retries_a_failure_later)
{
	FakeHttp http;
	int64_t now = 1000;
	ImageStore store(http.get(), [&now] { return now; }, false);
	store.get("https://x/missing.png");
	CHECK(store.fetch_one());
	CHECK(store.get("https://x/missing.png") == nullptr);
	CHECK(!store.fetch_one()); // too soon to retry
	now += 301;
	http.set("https://x/missing.png", "OK");
	store.get("https://x/missing.png");
	CHECK(store.fetch_one());
	CHECK(store.get("https://x/missing.png") != nullptr);
}

TEST_CASE(only_fetches_https)
{
	FakeHttp http;
	ImageStore store(http.get(), [] { return int64_t(0); }, false);
	CHECK(store.get("http://x/a.png") == nullptr);
	CHECK(store.get("") == nullptr);
	CHECK(!store.fetch_one());
}
