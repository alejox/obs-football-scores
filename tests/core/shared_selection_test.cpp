#include "core/shared_selection.hpp"

#include "test_harness.hpp"

using namespace fscore;

namespace {

// One source: its settings, what it shows and its link state.
struct Source {
	Selection settings;
	Selection shown;
	LinkState state;
	bool linked = true;
	bool has_match = true;

	void update(SharedSelection &shared)
	{
		settings = link_on_update(shared, state, linked, settings, shown, has_match);
		shown = settings;
	}
	void tick(SharedSelection &shared)
	{
		if (auto next = link_on_tick(shared, state, shown, has_match)) {
			settings = *next;
			update(shared);
		}
	}
};

} // namespace

TEST_CASE(a_change_in_one_linked_source_reaches_the_others)
{
	SharedSelection shared;
	Source a{{"esp.1", "100"}}, b{{"eng.1", "200"}}, c{{"ita.1", ""}};
	c.linked = false;
	a.update(shared); // first one sets the group
	b.update(shared); // joins: takes a's
	c.update(shared);
	CHECK(b.shown == (Selection{"esp.1", "100"}));
	CHECK(c.shown == (Selection{"ita.1", ""}));

	b.settings = {"eng.1", "300"}; // the user changes b
	b.update(shared);
	a.tick(shared);
	b.tick(shared);
	c.tick(shared);
	CHECK(a.shown == (Selection{"eng.1", "300"}));
	CHECK(b.shown == (Selection{"eng.1", "300"}));
	CHECK(c.shown == (Selection{"ita.1", ""})); // individual: untouched
	CHECK_EQ(shared.get().revision, uint64_t{2}); // following did not publish again
}

TEST_CASE(another_setting_changing_does_not_override_the_group)
{
	SharedSelection shared;
	Source a{{"esp.1", "100"}}, b{{"esp.1", "100"}};
	a.update(shared);
	b.update(shared);
	a.settings = {"eng.1", "300"};
	a.update(shared);
	// b's width changes before its tick saw the new selection: it must not push its old one.
	b.update(shared);
	CHECK(shared.get().selection == (Selection{"eng.1", "300"}));
	b.tick(shared);
	CHECK(b.shown == (Selection{"eng.1", "300"}));
}

TEST_CASE(a_league_only_source_keeps_the_shared_match)
{
	SharedSelection shared;
	Source board{{"esp.1", "100"}}, table{{"ger.1", ""}};
	table.has_match = false;
	board.update(shared);
	table.update(shared);
	CHECK_EQ(table.shown.league, std::string("esp.1"));
	CHECK(shared.get().selection == (Selection{"esp.1", "100"}));

	table.settings.league = "eng.1"; // a new league: the match no longer applies
	table.update(shared);
	board.tick(shared);
	CHECK(board.shown == (Selection{"eng.1", ""}));

	board.settings = {"eng.1", "500"};
	board.update(shared);
	table.tick(shared);
	CHECK(shared.get().selection == (Selection{"eng.1", "500"}));
}

TEST_CASE(linking_later_joins_the_group_and_unlinking_leaves_it)
{
	SharedSelection shared;
	Source a{{"esp.1", "100"}}, b{{"eng.1", "200"}};
	b.linked = false;
	a.update(shared);
	b.update(shared);
	b.linked = true;
	b.update(shared);
	CHECK(b.shown == (Selection{"esp.1", "100"}));
	b.linked = false;
	b.settings = {"ita.1", "9"};
	b.update(shared);
	a.tick(shared);
	CHECK(a.shown == (Selection{"esp.1", "100"}));
}
