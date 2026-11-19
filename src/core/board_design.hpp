// Pure core: no OBS, no Qt.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace fscore {

// The scoreboard layouts offered in the source properties.
enum class BoardDesign {
	Classic,  // slim bar, any width and height (Minimal / Bold / Neon styles)
	Stadium,  // team color panels, crests popping out of the bar
	Clash,    // diagonal split in team colors, giant crests as background art
	Card,     // corner card, crests as the heroes
	Floating, // big crests over a glass score strip, transparent background
	Clean,    // crests and score only, no background at all
};

inline constexpr BoardDesign kBoardDesigns[] = {BoardDesign::Classic, BoardDesign::Stadium, BoardDesign::Clash,
						BoardDesign::Card,    BoardDesign::Floating, BoardDesign::Clean};

struct DesignSize {
	uint32_t width;
	uint32_t height;
};

// Stable ids used for settings persistence. Never change an existing id.
std::string_view board_design_id(BoardDesign design);

// Unknown ids fall back to Classic, so a stale setting still renders.
BoardDesign board_design_from_id(std::string_view id);

// The size a design is drawn at before scaling; nullopt for Classic, whose
// width and height are both free.
std::optional<DesignSize> design_natural_size(BoardDesign design);

// The source height for `width` keeping the design's proportions (at least 16).
uint32_t design_height_for_width(BoardDesign design, uint32_t width, uint32_t classic_height);

} // namespace fscore
