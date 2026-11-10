// OBS adapter: process-wide services shared by every scoreboard source.
#pragma once

#include "core/http_types.hpp"
#include "core/match.hpp"
#include "feed/image_store.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace fscore::obs {

// Starts / stops the shared services. Call from obs_module_load / unload.
void start_services();
void stop_services();

// HTTPS GET through the system network stack (blocking).
HttpGet http_get();

// Seconds since the Unix epoch.
int64_t now_seconds();

// The shared crest and flag downloader; null outside start/stop.
ImageStore *image_store();

// The matches of `league`, from a short-lived cache or read now (blocking: the
// properties dialog calls it on the UI thread). nullopt when it cannot be read.
std::optional<Scoreboard> league_matches(const std::string &league, bool force_refresh = false);

} // namespace fscore::obs
