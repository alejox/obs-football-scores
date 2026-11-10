// Small builders for Match values in tests.
#pragma once

#include "core/match.hpp"

#include <string>

namespace test_harness {

inline fscore::Match make_match(const std::string &id, int64_t start, fscore::MatchState state, int home = 0,
				int away = 0)
{
	fscore::Match match;
	match.id = id;
	match.start_utc = start;
	match.state = state;
	match.home.id = "h" + id;
	match.home.name = "Home " + id;
	match.away.id = "a" + id;
	match.away.name = "Away " + id;
	if (match.started()) {
		match.home.score = home;
		match.away.score = away;
	}
	return match;
}

} // namespace test_harness
