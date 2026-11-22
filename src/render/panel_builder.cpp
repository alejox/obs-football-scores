#include "render/panel_builder.hpp"

#include "core/pitch.hpp"

#include <cmath>

namespace fscore::render {
namespace {

QColor color_of(const Team &team, QColor fallback)
{
	if (!team.color)
		return fallback;
	const uint32_t rgb = *team.color;
	return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Two teams in near-identical colors would be unreadable on the pitch: the away
// side then takes a light neutral.
QColor distinct_away(const QColor &home, const QColor &away)
{
	const int dr = home.red() - away.red(), dg = home.green() - away.green(), db = home.blue() - away.blue();
	if (dr * dr + dg * dg + db * db < 70 * 70)
		return QColor(236, 240, 245);
	return away;
}

QString kind_label(PlayKind kind, const PanelLabels &l)
{
	switch (kind) {
	case PlayKind::Goal:
		return l.goal;
	case PlayKind::ShotOnTarget:
		return l.shot_on;
	case PlayKind::ShotOffTarget:
		return l.shot_off;
	case PlayKind::ShotBlocked:
		return l.shot_blocked;
	case PlayKind::Woodwork:
		return l.woodwork;
	case PlayKind::Corner:
		return l.corner;
	case PlayKind::Foul:
		return l.foul;
	case PlayKind::Offside:
		return l.offside;
	default:
		return {};
	}
}

QString number(double value)
{
	return QString::number(static_cast<long long>(std::llround(value)));
}

std::optional<double> stat(const TeamStats &stats, const char *name)
{
	const auto it = stats.find(name);
	if (it == stats.end())
		return std::nullopt;
	return it->second;
}

StatRow make_row(const QString &label, double home, double away, bool percent = false)
{
	StatRow row;
	row.label = label;
	row.home = number(home) + (percent ? QStringLiteral("%") : QString());
	row.away = number(away) + (percent ? QStringLiteral("%") : QString());
	row.home_share = home + away > 0 ? home / (home + away) : 0.5;
	return row;
}

} // namespace

void fill_teams(PanelView &view, const Match &match, const ImageLookup &images)
{
	view.home.name = QString::fromStdString(match.home.short_name.empty() ? match.home.name : match.home.short_name);
	view.away.name = QString::fromStdString(match.away.short_name.empty() ? match.away.name : match.away.short_name);
	view.home.abbreviation = QString::fromStdString(match.home.abbreviation);
	view.away.abbreviation = QString::fromStdString(match.away.abbreviation);
	view.home.logo = images(match.home.logo_url);
	view.away.logo = images(match.away.logo_url);
	view.home.color = color_of(match.home, QColor(37, 99, 235));
	view.away.color = distinct_away(view.home.color, color_of(match.away, QColor(220, 38, 38)));
	if (match.started()) {
		view.home.score = QString::number(match.home.score.value_or(0));
		view.away.score = QString::number(match.away.score.value_or(0));
	}
}

void fill_live_pitch(PanelView &view, const MatchDetails &details, const PanelLabels &labels)
{
	view.pressure = pressure(details.plays);
	view.pressure_label = labels.pressure;
	view.marks.clear();
	const auto tracked = tracked_plays(details.plays, 6);
	for (const TrackedPlay &t : tracked) {
		PitchMark mark;
		mark.kind = t.play->kind;
		mark.home = !t.play->side || *t.play->side == TeamSide::Home;
		mark.at = play_point(*t.play);
		mark.has_end = t.play->has_end;
		if (mark.has_end)
			mark.end = play_end_point(*t.play);
		mark.recency = t.recency;
		view.marks.push_back(mark);
	}
	view.caption.clear();
	view.caption_sub.clear();
	if (!tracked.empty() && tracked.front().recency == 0) {
		const Play &latest = *tracked.front().play;
		const QString what = kind_label(latest.kind, labels);
		view.caption = QString::fromStdString(latest.minute);
		if (!what.isEmpty())
			view.caption += QStringLiteral(" · ") + what.toUpper();
		view.caption_sub = QString::fromStdString(latest.player);
	}
	view.no_plays = labels.no_plays;

	view.strip.clear();
	const TeamStats &h = details.home_stats, &a = details.away_stats;
	if (auto hp = stat(h, "possessionPct"), ap = stat(a, "possessionPct"); hp && ap)
		view.strip.push_back(make_row(labels.possession, *hp, *ap, true));
	if (auto hs = stat(h, "totalShots"), as = stat(a, "totalShots"); hs && as)
		view.strip.push_back(make_row(labels.shots, *hs, *as));
	if (auto ht = stat(h, "shotsOnTarget"), at = stat(a, "shotsOnTarget"); ht && at)
		view.strip.push_back(make_row(labels.shots_on_target, *ht, *at));
	if (auto hc = stat(h, "wonCorners"), ac = stat(a, "wonCorners"); hc && ac)
		view.strip.push_back(make_row(labels.corners, *hc, *ac));
}

void fill_lineups(PanelView &view, const MatchDetails &details)
{
	view.players.clear();
	for (const bool home : {true, false}) {
		const Lineup &lineup = home ? details.home_lineup : details.away_lineup;
		for (const PlacedPlayer &placed : place_lineup(lineup, home ? TeamSide::Home : TeamSide::Away)) {
			LineupMark mark;
			mark.at = placed.at;
			mark.jersey = QString::fromStdString(placed.player.jersey);
			mark.name = QString::fromStdString(placed.player.short_name);
			mark.home = home;
			mark.subbed_out = placed.player.subbed_out;
			view.players.push_back(mark);
		}
	}
	view.home_formation = QString::fromStdString(details.home_lineup.formation);
	view.away_formation = QString::fromStdString(details.away_lineup.formation);
}

void fill_stats(PanelView &view, const MatchDetails &details, const PanelLabels &labels)
{
	view.stats.clear();
	const TeamStats &h = details.home_stats, &a = details.away_stats;
	const struct {
		const char *name;
		const QString *label;
		bool percent;
	} rows[] = {
		{"possessionPct", &labels.possession, true},  {"totalShots", &labels.shots, false},
		{"shotsOnTarget", &labels.shots_on_target, false}, {"wonCorners", &labels.corners, false},
		{"accuratePasses", &labels.passes, false},    {"foulsCommitted", &labels.fouls, false},
		{"offsides", &labels.offsides, false},        {"yellowCards", &labels.yellow_cards, false},
	};
	for (const auto &row : rows) {
		const auto hv = stat(h, row.name), av = stat(a, row.name);
		if (hv && av)
			view.stats.push_back(make_row(*row.label, *hv, *av, row.percent));
	}
}

void fill_table(PanelView &view, const StandingsGroup &group, const Match *match, size_t max_rows,
		const PanelLabels &labels, const ImageLookup &images)
{
	const std::string home_id = match ? match->home.id : std::string();
	const std::string away_id = match ? match->away.id : std::string();
	view.table.clear();
	view.table_title = QString::fromStdString(group.name);
	for (int i = 0; i < 6; ++i)
		view.table_header[i] = labels.table_header[i];
	for (const StandingRow &row : visible_rows(group, max_rows, home_id, away_id)) {
		TableRow out;
		out.rank = QString::number(row.rank);
		out.name = QString::fromStdString(row.short_name);
		out.logo = images(row.logo_url);
		out.played = QString::number(row.played);
		out.wins = QString::number(row.wins);
		out.draws = QString::number(row.draws);
		out.losses = QString::number(row.losses);
		out.goal_difference = (row.goal_difference > 0 ? QStringLiteral("+") : QString()) +
				      QString::number(row.goal_difference);
		out.points = QString::number(row.points);
		if (row.zone_color) {
			const uint32_t rgb = *row.zone_color;
			out.zone = QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
		}
		out.highlighted = !row.team_id.empty() && (row.team_id == home_id || row.team_id == away_id);
		view.table.push_back(out);
	}
}

} // namespace fscore::render
