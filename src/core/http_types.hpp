// Pure core: no OBS, no Qt. HTTP shapes injected into the feed; the OBS
// adapter maps them onto the system network layer (src/net), the tests use fakes.
#pragma once

#include "core/cancel_token.hpp"

#include <functional>
#include <string>

namespace fscore {

struct HttpResult {
	int status = 0; // 0 = no response, see `error`
	std::string body;
	std::string error;
	bool ok() const { return status >= 200 && status < 300; }
};

// Blocking GET. `cancel` may be null.
using HttpGet = std::function<HttpResult(const std::string &url, CancelToken *cancel)>;

} // namespace fscore
