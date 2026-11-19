# Targets shared by the plugin build and the standalone test build. Nothing here
# may depend on OBS or Qt.

# buildspec.json is the single source of the version, for both builds.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../buildspec.json" football_buildspec)
string(JSON football_version GET "${football_buildspec}" version)

# JSON parsing for the ESPN answers (header only, pinned like goal-bar and multi-stream).
include(FetchContent)
FetchContent_Declare(
  nlohmann_json_header
  # Same file at the same tag; CMake tries the URLs in order, so a GitHub release
  # download outage falls back to raw.githubusercontent.com.
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
      https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
  URL_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json_header)

add_library(
  football-core STATIC
  core/board_design.cpp
  core/cancel_token.cpp
  core/espn.cpp
  core/goal_animation.cpp
  core/goal_watch.cpp
  core/league_catalog.cpp
  core/match_selection.cpp
  core/sha256.cpp
  core/style_preset.cpp
  core/time_util.cpp
  core/version.cpp)
target_include_directories(football-core PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_include_directories(football-core SYSTEM PRIVATE "${nlohmann_json_header_SOURCE_DIR}")
target_compile_definitions(football-core PRIVATE FSCORE_VERSION="${football_version}")
target_compile_features(football-core PUBLIC cxx_std_17)
set_target_properties(football-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Feed: polling worker, league listing and image downloads. Plain C++ and threads;
# HTTP and the clock are injected by the OBS adapter, so the tests drive them with fakes.
find_package(Threads REQUIRED)
add_library(football-feed STATIC feed/image_store.cpp feed/league_listing.cpp feed/match_feed.cpp)
target_link_libraries(football-feed PUBLIC football-core Threads::Threads)
set_target_properties(football-feed PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Updater logic: release check, staging manifest, installer script and settings. Plain
# C++ and the standard library; the Qt service in the plugin does the network and UI.
add_library(
  football-update STATIC
  update/atomic_file.cpp
  update/install_plan.cpp
  update/staging.cpp
  update/update_check.cpp
  update/update_settings.cpp
  update/update_settings_file.cpp)
target_link_libraries(football-update PUBLIC football-core)
target_include_directories(football-update SYSTEM PRIVATE "${nlohmann_json_header_SOURCE_DIR}")
set_target_properties(football-update PROPERTIES POSITION_INDEPENDENT_CODE ON)
