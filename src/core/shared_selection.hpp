// Pure core: no OBS, no Qt. The league and match that linked sources share, so
// changing it in one source changes it in all of them.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace fscore {

struct Selection {
	std::string league;
	std::string match; // empty: automatic
	bool operator==(const Selection &other) const { return league == other.league && match == other.match; }
	bool operator!=(const Selection &other) const { return !(*this == other); }
};

// Thread-safe. Revision 0 means nobody has published yet.
class SharedSelection {
public:
	struct Value {
		Selection selection;
		uint64_t revision = 0;
	};
	Value get() const;
	uint64_t publish(Selection selection);

private:
	mutable std::mutex mutex_;
	Value value_;
};

// What one source remembers about the link. The owner guards it.
struct LinkState {
	bool initialized = false;
	bool linked = false;
	uint64_t seen = 0;
	std::optional<Selection> adopting; // pushed into the settings, update not seen yet
};

// Call when the source's settings change. `own` is what the settings say, `previous`
// what the source was showing. Returns what the source must show: when it differs
// from `own`, write it back to the settings. A source without a match picker
// (`has_match` false) shares the league only and never touches the shared match.
Selection link_on_update(SharedSelection &shared, LinkState &state, bool linked_now, Selection own,
			 const Selection &previous, bool has_match);

// Call every tick. Returns the selection to write into the source's settings when
// another linked source changed it.
std::optional<Selection> link_on_tick(SharedSelection &shared, LinkState &state, const Selection &current,
				      bool has_match);

} // namespace fscore
