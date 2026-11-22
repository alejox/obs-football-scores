# Standalone core tests: configures with plain CMake and needs neither OBS nor Qt.
#
#   cmake -S tests -B build_tests
#   cmake --build build_tests
#   ctest --test-dir build_tests --output-on-failure
cmake_minimum_required(VERSION 3.28...3.30)

project(obs-football-scores-tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)
if(MSVC)
  add_compile_options(/W3 /utf-8)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(../src src)

add_library(test_harness STATIC support/test_harness.cpp support/fixtures.cpp)
target_include_directories(test_harness PUBLIC support)
target_compile_definitions(test_harness PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_LIST_DIR}/fixtures")

enable_testing()

function(add_core_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE test_harness football-core)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

add_core_test(cancel_token_test core/cancel_token_test.cpp)
add_core_test(board_design_test core/board_design_test.cpp)
add_core_test(time_util_test core/time_util_test.cpp)
add_core_test(espn_test core/espn_test.cpp)
add_core_test(league_catalog_test core/league_catalog_test.cpp)
add_core_test(match_selection_test core/match_selection_test.cpp)
add_core_test(goal_watch_test core/goal_watch_test.cpp)
add_core_test(goal_animation_test core/goal_animation_test.cpp)
add_core_test(style_preset_test core/style_preset_test.cpp)

function(add_feed_test name)
  add_core_test(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE football-feed)
endfunction()

add_feed_test(match_feed_test feed/match_feed_test.cpp)
add_feed_test(image_store_test feed/image_store_test.cpp)
add_feed_test(league_listing_test feed/league_listing_test.cpp)

add_core_test(sha256_test core/sha256_test.cpp)

function(add_update_test name)
  add_core_test(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE football-update)
endfunction()

add_update_test(update_check_test update/update_check_test.cpp)
add_update_test(staging_test update/staging_test.cpp)
add_update_test(install_plan_test update/install_plan_test.cpp)
add_update_test(update_settings_test update/update_settings_test.cpp)

add_core_test(audio_clip_test audio/audio_clip_test.cpp)
target_link_libraries(audio_clip_test PRIVATE football-audio)
target_compile_definitions(audio_clip_test PRIVATE GOAL_WAV="${CMAKE_CURRENT_LIST_DIR}/../data/sounds/goal.wav")
add_core_test(match_details_test core/match_details_test.cpp)
add_feed_test(details_feed_test feed/details_feed_test.cpp)
