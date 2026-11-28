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

enum class PanelContent { LivePitch, Lineups, Stats, Table, Feed, Momentum, Timeline, Preview, Scorers, Round, Goals };

// How the pitch is drawn: from above, or tilted like a TV camera in the stands.
enum class PitchView { TopDown, Perspective };

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

// One line of the live commentary.
struct FeedLine {
	QString minute;
	QString text;
	PlayKind kind = PlayKind::Other;
	int side = 0; // 1 home, -1 away, 0 neither
};

// A goal, card or substitution on the match timeline.
struct TimelineEvent {
	QString minute;
	double at = 0; // minutes, for its place on the line
	PlayKind kind = PlayKind::Other;
	bool home = true;
	QString player;
	QString detail; // "Sale Pedri" for a substitution, "2 - 1" for a goal
};

struct FormCell {
	QChar result; // W / D / L, already localized (G / E / P)
	int outcome = 0; // 1 win, 0 draw, -1 loss
	QString score;
	QString opponent;
	QImage opponent_logo;
};

struct LeaderRow {
	QString rank, name, team, value, matches;
	QImage logo;
	bool highlighted = false;
};

struct RoundRow {
	QString home, away;
	QImage home_logo, away_logo;
	QString center; // "2 - 1" or "21:00"
	QString status; // "67'", "FINAL", "HOY"
	bool live = false;
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
	// A play on replay from the dock: `status` reads "REPETICIÓN · 63'" and is drawn
	// in amber; the live pitch and the momentum show it as a badge of their own.
	bool replay = false;
	double blink = 1.0; // live dot
	double pulse = 0.0; // 0..1, the latest play's ring
	// Ball route over the last plays, oldest first (empty: not drawn), and how far the
	// ball has travelled from the previous play to the latest one (1: it has arrived).
	std::vector<PitchPoint> ball_route;
	int ball_sequence = -1;
	double ball_progress = 1.0;

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

	// For list panels, the rows the source is sized for (0: as many as there are), so the
	// frame keeps its size while the list fills up.
	size_t rows = 0;

	PitchView pitch_view = PitchView::TopDown;
	QString latest_team; // team name in the latest play's tag

	// Live commentary.
	QString feed_title;
	std::vector<FeedLine> feed;

	// Momentum.
	QString momentum_title;
	std::vector<double> momentum; // per minute, -1 away .. 1 home
	double now_minute = -1;       // the match clock, -1 when unknown

	// Timeline.
	std::vector<TimelineEvent> timeline;
	double timeline_end = 90; // minutes the line spans
	QString half_time_label;

	// Preview.
	QString form_title, meetings_title, venue_line;
	std::vector<FormCell> home_form, away_form;
	QString meetings_summary;
	std::vector<QString> meetings;

	// Scorers.
	QString scorers_title, scorers_value_header, scorers_matches_header;
	std::vector<LeaderRow> scorers;

	// Round.
	QString round_title;
	std::vector<RoundRow> round;

	// Goals of the match, each team on its own side ("Vinícius Júnior (pen.) 67'").
	std::vector<QString> home_goals, away_goals;
	QString no_goals; // shown in the middle while it is 0 - 0
};

QSize panel_natural_size(PanelContent content, size_t rows = 10);

// Paints one frame at `size` (the design is drawn at its natural size and scaled).
// Same threading rules as ScoreboardPainter.
QImage paint_panel(const PanelView &view, QSize size);

} // namespace fscore::render
