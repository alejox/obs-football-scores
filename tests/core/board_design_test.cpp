#include "core/board_design.hpp"

#include "test_harness.hpp"

#include <set>
#include <string>

using namespace fscore;

TEST_CASE(ids_round_trip_and_are_unique)
{
	std::set<std::string> ids;
	for (BoardDesign design : kBoardDesigns) {
		CHECK(board_design_from_id(board_design_id(design)) == design);
		CHECK(ids.insert(std::string(board_design_id(design))).second);
	}
	CHECK(board_design_from_id("nope") == BoardDesign::Classic);
	CHECK(board_design_from_id("") == BoardDesign::Classic);
}

TEST_CASE(height_follows_the_design_proportions)
{
	CHECK_EQ(design_height_for_width(BoardDesign::Classic, 960, 120), 120u);
	CHECK_EQ(design_height_for_width(BoardDesign::Stadium, 1280, 120), 220u);
	CHECK_EQ(design_height_for_width(BoardDesign::Stadium, 640, 120), 110u);
	CHECK_EQ(design_height_for_width(BoardDesign::Card, 310, 0), 180u);
	CHECK_EQ(design_height_for_width(BoardDesign::Clash, 16, 0), 16u); // clamped
	for (BoardDesign design : kBoardDesigns) {
		if (design != BoardDesign::Classic)
			CHECK(design_natural_size(design).has_value());
	}
}
