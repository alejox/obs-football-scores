// Render adapter: Qt only, no OBS.
#pragma once

#include "core/board_design.hpp"
#include "core/goal_animation.hpp"
#include "core/style_preset.hpp"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>

namespace fscore::render {

struct BoardStyle {
	BoardDesign design = BoardDesign::Classic;
	StylePreset preset = StylePreset::Minimal; // Classic only
	QColor accent{255, 61, 110};
	QColor text{255, 255, 255};
	bool show_league = true;
};

struct SideView {
	QString name;
	QString abbreviation; // "RMA"; falls back to the name
	QImage logo;  // may be null while it downloads
	QColor color; // invalid when the feed has none
};

// Everything one frame shows, with the texts already localized.
struct ScoreView {
	bool has_match = false;
	QString message; // shown alone when there is no match ("Loading…")

	QString league_name;
	QImage league_logo;
	QImage league_flag; // null for international competitions

	SideView home;
	SideView away;
	QString center;    // "2 - 1", or the kick-off time
	bool has_score = false; // false before kick-off and when postponed
	QString score_home;
	QString score_away;
	QString status;    // "67'", "HALF TIME", "SAT 22 NOV"
	bool live = false; // draws the blinking dot before the status
	double blink = 1.0; // [0, 1] live dot opacity

	// Goal animation: shown while goal.active.
	GoalAnimationFrame goal;
	bool goal_home = true;
	QString goal_title;  // "GOAL!"
	QString goal_detail; // "F. López 66'"
	QImage goal_photo;   // the scorer's headshot, replaces the crest when set
};

// The designs other than Classic, drawn at their natural size and scaled to
// `size`. Same threading rules as ScoreboardPainter.
QImage paint_design(const ScoreView &view, const BoardStyle &style, QSize size);

// Paints one frame into a transparent ARGB32_Premultiplied image. Safe to call
// from any thread while a QGuiApplication exists.
class ScoreboardPainter {
public:
	static QImage paint(const ScoreView &view, const BoardStyle &style, QSize size);
};

} // namespace fscore::render
