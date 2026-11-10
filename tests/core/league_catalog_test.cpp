#include "core/league_catalog.hpp"

#include "test_harness.hpp"

#include <set>
#include <string>

using namespace fscore;

TEST_CASE(slugs_are_unique_and_found)
{
	std::set<std::string> slugs;
	for (const League &league : league_catalog()) {
		CHECK(slugs.insert(league.slug).second);
		CHECK(find_league(league.slug) == &league);
		CHECK(std::string(league.name).size() > 0);
	}
	CHECK(find_league("esp.1") != nullptr);
	CHECK(find_league("nope.9") == nullptr);
}

TEST_CASE(flag_urls)
{
	CHECK_EQ(country_flag_url("col"), std::string("https://a.espncdn.com/i/teamlogos/countries/500/col.png"));
	CHECK(country_flag_url("").empty());
	CHECK_EQ(std::string(find_league("uefa.champions")->country), std::string());
}
