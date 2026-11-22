#include "core/leaders.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

using namespace fscore;

TEST_CASE(parses_scorers_and_assists)
{
	const auto boards = parse_leaders(test_harness::read_fixture("esp1_leaders.json"));
	REQUIRE(boards.has_value());
	REQUIRE(boards->goals.size() == 12u);
	CHECK_EQ(boards->goals[0].name, std::string("Raphinha"));
	CHECK_EQ(boards->goals[0].team, std::string("BAR"));
	CHECK_EQ(boards->goals[0].value, 15);
	CHECK_EQ(boards->goals[0].matches, 11);
	CHECK(boards->goals[0].team_logo.find("500-dark") != std::string::npos);
	REQUIRE(!boards->assists.empty());
	CHECK_EQ(boards->assists[0].name, std::string("Lamine Yamal"));
	CHECK(!parse_leaders("{}"));
	CHECK(!parse_leaders("nope"));
}
