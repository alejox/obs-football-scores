#include "core/espn.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

#include <string>

using namespace fscore;

namespace {

// One live event in the scoreboard shape, with the fields the parser reads.
std::string live_board(const std::string &status_name, const std::string &state, const std::string &home_score)
{
	return R"({"leagues":[{"name":"Spanish LALIGA","logos":[{"href":"http://a.espncdn.com/l.png"}]}],
	"events":[{"id":"9","date":"2026-11-22T20:00Z","status":{"displayClock":"67'","type":{"name":")" +
	       status_name + R"(","state":")" + state + R"(","shortDetail":"67'"}},
	"competitions":[{"competitors":[
	  {"homeAway":"away","score":"1","team":{"id":"83","displayName":"Barcelona","shortDisplayName":"Barça","abbreviation":"FCB","logo":"https://a.espncdn.com/b.png","color":"A50044"}},
	  {"homeAway":"home","score":")" +
	       home_score + R"(","team":{"id":"86","displayName":"Real Madrid","abbreviation":"RMA","logos":[{"href":"https://a.espncdn.com/r.png"}]}}],
	"details":[
	  {"scoringPlay":true,"team":{"id":"86"},"clock":{"displayValue":"12'"},"athletesInvolved":[{"displayName":"Vinícius Júnior"}],"penaltyKick":true},
	  {"scoringPlay":false,"team":{"id":"83"},"clock":{"displayValue":"30'"},"yellowCard":true},
	  {"scoringPlay":true,"team":{"id":"83"},"clock":{"displayValue":"55'"},"athletesInvolved":[{"displayName":"Lamine Yamal"}]}
	]}]}]})";
}

} // namespace

TEST_CASE(parses_a_real_day_scoreboard)
{
	const auto board = espn::parse_scoreboard(test_harness::read_fixture("esp1_day.json"));
	REQUIRE(board.has_value());
	CHECK_EQ(board->league_name, std::string("Spanish LALIGA"));
	// The variant for dark backgrounds wins.
	CHECK_EQ(board->league_logo_url, std::string("https://a.espncdn.com/i/leaguelogos/soccer/500-dark/15.png"));
	REQUIRE(board->matches.size() == 5u);

	const Match &first = board->matches[0];
	CHECK_EQ(first.id, std::string("401882814"));
	CHECK_EQ(first.home.name, std::string("Elche"));
	CHECK_EQ(first.away.name, std::string("Real Betis"));
	CHECK_EQ(first.home.abbreviation, std::string("ELC"));
	CHECK_EQ(first.home.logo_url, std::string("https://a.espncdn.com/i/teamlogos/soccer/500/3751.png"));
	CHECK(first.home.color.has_value());
	CHECK_EQ(first.home.color.value_or(0), 0x288A00u);
	CHECK(first.state == MatchState::Finished);
	CHECK_EQ(first.home.score.value_or(-1), 0);
	CHECK_EQ(first.away.score.value_or(-1), 2);
	CHECK_EQ(first.start_utc, 1794142800);
	// Two goals among the cards.
	REQUIRE(first.goals.size() == 2u);
	CHECK_EQ(first.goals[0].scorer, std::string("Rodrigo Riquelme"));
	CHECK_EQ(first.goals[0].minute, std::string("81'"));
	CHECK_EQ(first.goals[0].team_id, first.away.id);

	const Match &last = board->matches[4];
	CHECK_EQ(last.home.name, std::string("Atlético Madrid"));
	CHECK_EQ(last.home.score.value_or(-1), 1);
	CHECK_EQ(last.away.score.value_or(-1), 2);
	CHECK_EQ(last.goals.size(), 3u);
	CHECK(last.goals[0].scorer_photo_url.empty()); // most players have no headshot
	CHECK_EQ(board->matches[3].goals[0].scorer, std::string("Tajon Buchanan"));
	CHECK_EQ(board->matches[3].goals[0].scorer_photo_url,
		 std::string("https://a.espncdn.com/i/headshots/soccer/players/full/284769.png"));
}

TEST_CASE(home_and_away_follow_the_feed_not_the_order)
{
	const auto board = espn::parse_scoreboard(live_board("STATUS_SECOND_HALF", "in", "1"));
	REQUIRE(board && board->matches.size() == 1u);
	const Match &match = board->matches[0];
	CHECK_EQ(match.home.name, std::string("Real Madrid"));
	CHECK_EQ(match.away.name, std::string("Barcelona"));
	CHECK_EQ(match.away.short_name, std::string("Barça"));
	CHECK_EQ(match.home.short_name, std::string("Real Madrid")); // falls back to the name
	CHECK_EQ(match.home.logo_url, std::string("https://a.espncdn.com/r.png")); // from "logos"
	CHECK(!match.home.color.has_value());
	CHECK_EQ(match.away.color.value_or(0), 0xA50044u);
	CHECK(match.state == MatchState::Live);
	CHECK_EQ(match.clock, std::string("67'"));
	CHECK_EQ(match.home.score.value_or(-1), 1);
	CHECK_EQ(match.away.score.value_or(-1), 1);
	REQUIRE(match.goals.size() == 2u);
	CHECK(match.goals[0].penalty);
	CHECK_EQ(match.goals[1].scorer, std::string("Lamine Yamal"));
	// http logos are upgraded to https.
	CHECK_EQ(board->league_logo_url, std::string("https://a.espncdn.com/l.png"));
}

TEST_CASE(maps_the_status_names)
{
	auto state_of = [](const char *name, const char *state) {
		const auto board = espn::parse_scoreboard(live_board(name, state, "0"));
		return board->matches.at(0).state;
	};
	CHECK(state_of("STATUS_SCHEDULED", "pre") == MatchState::Scheduled);
	CHECK(state_of("STATUS_FIRST_HALF", "in") == MatchState::Live);
	CHECK(state_of("STATUS_HALFTIME", "in") == MatchState::HalfTime);
	CHECK(state_of("STATUS_FULL_TIME", "post") == MatchState::Finished);
	CHECK(state_of("STATUS_POSTPONED", "post") == MatchState::Postponed);
	CHECK(state_of("STATUS_SOMETHING_NEW", "in") == MatchState::Live);
}

TEST_CASE(no_scores_before_kick_off)
{
	const auto board = espn::parse_scoreboard(live_board("STATUS_SCHEDULED", "pre", "0"));
	CHECK(!board->matches.at(0).home.score.has_value());
	CHECK(!board->matches.at(0).away.score.has_value());
}

TEST_CASE(rejects_what_is_not_a_scoreboard)
{
	CHECK(!espn::parse_scoreboard(""));
	CHECK(!espn::parse_scoreboard("not json"));
	CHECK(!espn::parse_scoreboard(R"({"code":400,"message":"Failed to get events endpoint."})"));
	const auto empty = espn::parse_scoreboard(R"({"events":[]})");
	REQUIRE(empty.has_value());
	CHECK(empty->matches.empty());
	// Broken events are skipped, not fatal.
	const auto partial = espn::parse_scoreboard(R"({"events":[{"id":"1"},{"competitions":[]}]})");
	REQUIRE(partial.has_value());
	CHECK(partial->matches.empty());
}

TEST_CASE(builds_the_urls)
{
	CHECK_EQ(espn::month_url("esp.1", 2026, 11),
		 std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/esp.1/scoreboard?dates=202611"));
	CHECK_EQ(espn::day_url("mex.1", "20261106"),
		 std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/mex.1/scoreboard?dates=20261106"));
}

TEST_CASE(candidate_days_start_with_the_us_eastern_date)
{
	// 01:00 UTC on the 7th is the evening of the 6th in New York.
	const auto late = espn::candidate_days(1794142800 - 36 * 3600); // 2026-11-07T01:00Z
	REQUIRE(late.size() == 2u);
	CHECK_EQ(late[0], std::string("20261106"));
	CHECK_EQ(late[1], std::string("20261107"));
	// An afternoon kick-off has a single candidate.
	const auto afternoon = espn::candidate_days(1794142800);
	REQUIRE(afternoon.size() == 1u);
	CHECK_EQ(afternoon[0], std::string("20261108"));
}
