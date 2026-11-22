// Render adapter: Qt only, no OBS. Turns match data into a PanelView. The texts come
// in through PanelLabels so the OBS adapter can localize them.
#pragma once

#include "core/match.hpp"
#include "core/leaders.hpp"
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
	QString momentum;            // "MOMENTUM"
	QString yellow_card, red_card, substitution; // timeline / feed words
	QString sub_off;             // "Sale %1"
	QString form, meetings, venue_attendance; // "Forma reciente", "Cara a cara", "%1 · %2 espectadores"
	QString meetings_leads, meetings_tied;    // "%1 domina el historial (%2)", "Historial igualado (%1)"
	QChar win, draw, loss;       // G E P
	QString scorers, assists, goals_header, assists_header, matches_header;
	QString round_title;         // "Jornada de hoy"
	bool spanish = true;         // narrate the commentary in Spanish
};

// Decoded images by URL (null while they download).
using ImageLookup = std::function<QImage(const std::string &url)>;

// The two teams for the header: names, crests, colors and score.
void fill_teams(PanelView &view, const Match &match, const ImageLookup &images);

void fill_live_pitch(PanelView &view, const MatchDetails &details, const PanelLabels &labels);
void fill_lineups(PanelView &view, const MatchDetails &details);
void fill_stats(PanelView &view, const MatchDetails &details, const PanelLabels &labels);
// Live commentary: the latest `rows` lines, newest first, in Spanish when asked.
void fill_feed(PanelView &view, const MatchDetails &details, size_t rows, const PanelLabels &labels);
void fill_momentum(PanelView &view, const MatchDetails &details, const Match &match, const PanelLabels &labels);
// Goals, cards and substitutions, the latest `rows`, newest first.
void fill_timeline(PanelView &view, const MatchDetails &details, size_t rows, const PanelLabels &labels);
void fill_preview(PanelView &view, const MatchDetails &details, const PanelLabels &labels, const ImageLookup &images);
// Goals (or assists) leaders, `rows` of them; the teams of `match` highlighted.
void fill_scorers(PanelView &view, const LeaderBoards &boards, bool assists, const Match *match, size_t rows,
		  const QString &league_name, const PanelLabels &labels, const ImageLookup &images);
// Today's matches of the league: live first, then upcoming, then finished.
void fill_round(PanelView &view, const Scoreboard &round, const Match *match, size_t rows, const QString &league_name,
		const std::function<QString(const Match &)> &status, const std::function<QString(const Match &)> &center,
		const PanelLabels &labels, const ImageLookup &images);

void fill_table(PanelView &view, const StandingsGroup &group, const Match *match, size_t max_rows,
		const PanelLabels &labels, const ImageLookup &images);

} // namespace fscore::render
