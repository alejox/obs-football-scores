#include "core/shared_selection.hpp"

#include <utility>

namespace fscore {

SharedSelection::Value SharedSelection::get() const
{
	std::lock_guard lock(mutex_);
	return value_;
}

uint64_t SharedSelection::publish(Selection selection)
{
	std::lock_guard lock(mutex_);
	value_.selection = std::move(selection);
	return ++value_.revision;
}

namespace {

bool same(const Selection &a, const Selection &b, bool has_match)
{
	return a.league == b.league && (!has_match || a.match == b.match);
}

} // namespace

Selection link_on_update(SharedSelection &shared, LinkState &state, bool linked_now, Selection own,
			 const Selection &previous, bool has_match)
{
	const bool joining = linked_now && (!state.initialized || !state.linked);
	const bool was_initialized = state.initialized;
	state.initialized = true;
	state.linked = linked_now;
	if (!linked_now) {
		state.adopting.reset();
		return own;
	}
	const SharedSelection::Value global = shared.get();

	if (joining) {
		if (global.revision != 0) {
			// Join the group: take what the others show.
			state.seen = global.revision;
			own.league = global.selection.league;
			own.match = global.selection.match;
			return own;
		}
		if (!own.league.empty())
			state.seen = shared.publish({own.league, has_match ? own.match : std::string()});
		return own;
	}

	// The update that follows our own push from link_on_tick is not a user change.
	if (state.adopting && same(own, *state.adopting, has_match)) {
		state.adopting.reset();
		return own;
	}
	state.adopting.reset();
	if (!was_initialized || same(own, previous, has_match))
		return own; // some other setting changed
	// The user picked another league or match here: everyone follows.
	Selection next{own.league, own.match};
	if (!has_match)
		next.match = own.league == global.selection.league ? global.selection.match : std::string();
	if (next != global.selection)
		state.seen = shared.publish(next);
	return own;
}

std::optional<Selection> link_on_tick(SharedSelection &shared, LinkState &state, const Selection &current,
				      bool has_match)
{
	if (!state.linked)
		return std::nullopt;
	const SharedSelection::Value global = shared.get();
	if (global.revision == state.seen)
		return std::nullopt;
	state.seen = global.revision;
	// A league-only source follows the match too: it highlights that match's teams.
	(void)has_match;
	if (current == global.selection)
		return std::nullopt;
	state.adopting = global.selection;
	return global.selection;
}

} // namespace fscore
