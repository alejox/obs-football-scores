#include "core/board_design.hpp"

#include <algorithm>

namespace fscore {

std::string_view board_design_id(BoardDesign design)
{
	switch (design) {
	case BoardDesign::Classic:
		return "classic";
	case BoardDesign::Stadium:
		return "stadium";
	case BoardDesign::Clash:
		return "clash";
	case BoardDesign::Card:
		return "card";
	case BoardDesign::Floating:
		return "floating";
	case BoardDesign::Clean:
		return "clean";
	}
	return "classic";
}

BoardDesign board_design_from_id(std::string_view id)
{
	for (BoardDesign design : kBoardDesigns) {
		if (board_design_id(design) == id)
			return design;
	}
	return BoardDesign::Classic;
}

std::optional<DesignSize> design_natural_size(BoardDesign design)
{
	switch (design) {
	case BoardDesign::Classic:
		return std::nullopt;
	case BoardDesign::Stadium:
		return DesignSize{1280, 220};
	case BoardDesign::Clash:
		return DesignSize{1280, 160};
	case BoardDesign::Card:
		return DesignSize{620, 360};
	case BoardDesign::Floating:
		return DesignSize{900, 340};
	case BoardDesign::Clean:
		return DesignSize{820, 200};
	}
	return std::nullopt;
}

uint32_t design_height_for_width(BoardDesign design, uint32_t width, uint32_t classic_height)
{
	const auto natural = design_natural_size(design);
	if (!natural)
		return classic_height;
	const uint64_t height = (static_cast<uint64_t>(width) * natural->height + natural->width / 2) / natural->width;
	return static_cast<uint32_t>(std::clamp<uint64_t>(height, 16, 4096));
}

} // namespace fscore
