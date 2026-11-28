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
  core/leaders.cpp
  core/league_catalog.cpp
  core/match_details.cpp
  core/match_selection.cpp
  core/shared_selection.cpp
  core/narration.cpp
  core/pitch.cpp
  core/replay.cpp
  core/standings.cpp
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
add_library(football-feed STATIC feed/details_feed.cpp feed/image_store.cpp feed/league_listing.cpp feed/match_feed.cpp)
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

# Goal sounds: WAV, MP3 and FLAC decoding with dr_libs (public domain / MIT-0, header
# only), pinned to one commit. Plain C/C++, so the tests cover it.
set(dr_libs_commit e381981afa52800b9a3864df548353ff67252fbc)
set(dr_libs_dir "${CMAKE_CURRENT_BINARY_DIR}/dr_libs")
foreach(entry "dr_wav.h|b36c32661cfbbfb1cc0beb19f7a9d2567065e5a297f64172dcebfff59885f37b"
              "dr_mp3.h|bd539c0cafee96a73b6075607aa3caae375ca1eda7eeb8479b87b33b16212570"
              "dr_flac.h|bd569c910a7c465e481fd50af84e2871e81e2154e30ac510d98f953cc0d5a546")
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 header)
  list(GET parts 1 hash)
  if(NOT EXISTS "${dr_libs_dir}/${header}")
    file(DOWNLOAD "https://raw.githubusercontent.com/mackron/dr_libs/${dr_libs_commit}/${header}"
         "${dr_libs_dir}/${header}" EXPECTED_HASH SHA256=${hash} TLS_VERIFY ON STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      file(REMOVE "${dr_libs_dir}/${header}")
      message(FATAL_ERROR "Could not download ${header}: ${status}")
    endif()
  endif()
endforeach()

add_library(football-audio STATIC audio/audio_clip.cpp audio/dr_libs.c)
target_include_directories(football-audio PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_include_directories(football-audio SYSTEM PRIVATE "${dr_libs_dir}")
target_compile_features(football-audio PUBLIC cxx_std_17)
set_target_properties(football-audio PROPERTIES POSITION_INDEPENDENT_CODE ON)
