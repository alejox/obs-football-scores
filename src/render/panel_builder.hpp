// Render adapter: Qt only, no OBS. Turns match data into a PanelView. The texts come
// in through PanelLabels so the OBS adapter can localize them.
#pragma once

#include "core/match.hpp"
#include "core/match_details.hpp"
#include "core/standings.hpp"
#include "render/panel_painter.hpp"

#include <QString>

#include <functional>
#include <optional>

namespace fscore::render {

struct PanelLabels {
	QString pressure;            // "PRESIÓN"
	QString no_plays;            // "Aquí aparecerán las jugadas del partido"
	QString goal, shot_on, shot_off, shot_blocked, woodwork, corner, foul, offside;
	QString possession, shots, shots_on_target, corners, fouls, offsides, saves, passes, yellow_cards;
	QString table_header[6];     // PJ G E P DG PTS
};

// Decoded images by URL (null while they download).
using ImageLookup = std::function<QImage(const std::string &url)>;

// The two teams for the header: names, crests, colors and score.
void fill_teams(PanelView &view, const Match &match, const ImageLookup &images);

void fill_live_pitch(PanelView &view, const MatchDetails &details, const PanelLabels &labels);
void fill_lineups(PanelView &view, const MatchDetails &details);
void fill_stats(PanelView &view, const MatchDetails &details, const PanelLabels &labels);
void fill_table(PanelView &view, const StandingsGroup &group, const Match *match, size_t max_rows,
		const PanelLabels &labels, const ImageLookup &images);

} // namespace fscore::render
