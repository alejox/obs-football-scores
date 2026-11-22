// Render adapter: Qt only, no OBS. The "Football panel" views: the live pitch tracker,
// the lineups on the pitch, the match stats and the league table.
#pragma once

#include "core/pitch.hpp"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>

#include <vector>

namespace fscore::render {

enum class PanelContent { LivePitch, Lineups, Stats, Table };

struct PanelTeam {
	QString name;
	QString abbreviation;
	QImage logo;
	QColor color; // always valid: the adapter picks a fallback
	QString score; // empty before kick-off
};

// One play drawn on the tracker.
struct PitchMark {
	PlayKind kind = PlayKind::Other;
	bool home = true;
	PitchPoint at;
	bool has_end = false;
	PitchPoint end;
	int recency = 99; // 0 = the latest play, 1 = the one before... (99: old, only shots are kept)
};

struct LineupMark {
	PitchPoint at;
	QString jersey;
	QString name;
	bool home = true;
	bool subbed_out = false;
};

struct StatRow {
	QString label;
	QString home;
	QString away;
	double home_share = 0.5; // 0..1, the bar split
};

struct TableRow {
	QString rank;
	QString name;
	QImage logo;
	QString played, wins, draws, losses, goal_difference, points;
	QColor zone; // invalid for no zone
	bool highlighted = false;
};

// Everything one frame shows, texts already localized.
struct PanelView {
	PanelContent content = PanelContent::LivePitch;
	bool has_data = false;
	QString message; // shown alone when !has_data

	PanelTeam home, away;
	QString status; // "67'", "DESCANSO", "SÁB 22 NOV 21:00"
	bool live = false;
	double blink = 1.0; // live dot
	double pulse = 0.0; // 0..1, the latest play's ring

	// Live pitch.
	std::vector<PitchMark> marks;
	QString caption;     // over the latest play: "67' · REMATE AL ARCO"
	QString caption_sub; // "Lamine Yamal"
	double pressure = 0; // -1 away .. 1 home
	QString pressure_label;
	std::vector<StatRow> strip; // 3-4 key stats under the pitch
	QString no_plays;           // shown on the pitch when there is nothing to draw yet

	// Lineups.
	std::vector<LineupMark> players;
	QString home_formation, away_formation;

	// Stats.
	std::vector<StatRow> stats;

	// Table.
	QString table_title;
	std::vector<TableRow> table;
	QString table_header[6]; // PJ, G, E, P, DG, PTS
};

QSize panel_natural_size(PanelContent content, size_t table_rows = 10);

// Paints one frame at `size` (the design is drawn at its natural size and scaled).
// Same threading rules as ScoreboardPainter.
QImage paint_panel(const PanelView &view, QSize size);

} // namespace fscore::render
