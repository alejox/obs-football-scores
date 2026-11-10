// Scripted HTTP for the feed tests: answers by URL and records every request.
#pragma once

#include "core/http_types.hpp"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace test_harness {

class FakeHttp {
public:
	void set(const std::string &url, const std::string &body, int status = 200)
	{
		std::lock_guard lock(mutex_);
		answers_[url] = fscore::HttpResult{status, body, {}};
	}

	void fail(const std::string &url)
	{
		std::lock_guard lock(mutex_);
		answers_[url] = fscore::HttpResult{0, {}, "offline"};
	}

	std::vector<std::string> requests() const
	{
		std::lock_guard lock(mutex_);
		return requests_;
	}

	void clear_requests()
	{
		std::lock_guard lock(mutex_);
		requests_.clear();
	}

	fscore::HttpGet get()
	{
		return [this](const std::string &url, fscore::CancelToken *) {
			std::lock_guard lock(mutex_);
			requests_.push_back(url);
			const auto it = answers_.find(url);
			if (it == answers_.end())
				return fscore::HttpResult{404, R"({"code":404})", {}};
			return it->second;
		};
	}

private:
	mutable std::mutex mutex_;
	std::map<std::string, fscore::HttpResult> answers_;
	std::vector<std::string> requests_;
};

} // namespace test_harness
