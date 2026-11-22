#include "render/panel_builder.hpp"

#include "core/narration.hpp"
#include "core/pitch.hpp"

#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

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

int color_distance(const QColor &a, const QColor &b)
{
	const int dr = a.red() - b.red(), dg = a.green() - b.green(), db = a.blue() - b.blue();
	return static_cast<int>(std::sqrt(static_cast<double>(dr * dr + dg * dg + db * db)));
}

// Two teams in similar colors would be unreadable on the pitch: the away side then
// wears its alternate color, or a light neutral when that clashes too.
QColor distinct_away(const QColor &home, const QColor &away, const std::optional<uint32_t> &alternate)
{
	constexpr int kMinDistance = 120;
	if (color_distance(home, away) >= kMinDistance)
		return away;
	if (alternate) {
		const QColor alt((*alternate >> 16) & 0xFF, (*alternate >> 8) & 0xFF, *alternate & 0xFF);
		if (color_distance(home, alt) >= kMinDistance)
			return alt;
	}
	return color_distance(home, QColor(236, 240, 245)) >= kMinDistance ? QColor(236, 240, 245) : QColor(17, 24, 39);
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

namespace {
QString short_name(const std::string &full);
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
	view.away.color = distinct_away(view.home.color, color_of(match.away, QColor(220, 38, 38)), match.away.alternate_color);
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
	view.latest_team.clear();
	if (!tracked.empty() && tracked.front().recency == 0) {
		const Play &latest = *tracked.front().play;
		const QString what = kind_label(latest.kind, labels);
		// "Remate al arco · 67' · L. Yamal"
		view.caption = what.isEmpty() ? QString::fromStdString(latest.minute)
					      : what + QStringLiteral(" · ") + QString::fromStdString(latest.minute);
		if (!latest.player.empty())
			view.caption += QStringLiteral(" · ") + short_name(latest.player);
		view.caption_sub = QString::fromStdString(latest.player);
		const bool home = !latest.side || *latest.side == TeamSide::Home;
		view.latest_team = home ? view.home.name : view.away.name;
	}
	view.momentum = momentum_series(details.plays);
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

namespace {

int side_of(const std::optional<TeamSide> &side)
{
	return !side ? 0 : *side == TeamSide::Home ? 1 : -1;
}

double minute_value(const std::string &minute)
{
	// "67'" -> 67, "90'+3'" -> 93, "45'+2'" -> 47
	double total = 0;
	bool any = false;
	for (const QString &part : QString::fromStdString(minute).split('+')) {
		bool ok = false;
		const double v = QString(part).remove('\'').trimmed().toDouble(&ok);
		if (ok) {
			total += v;
			any = true;
		}
	}
	return any ? total : -1;
}

QString short_name(const std::string &full)
{
	// "Lamine Yamal" -> "L. Yamal"; single names stay.
	const QString name = QString::fromStdString(full).trimmed();
	const qsizetype space = name.indexOf(' ');
	if (space <= 0)
		return name;
	return name.left(1) + QStringLiteral(". ") + name.mid(space + 1);
}

} // namespace

void fill_feed(PanelView &view, const MatchDetails &details, size_t rows, const PanelLabels &labels)
{
	view.feed.clear();
	view.feed_title = labels.pressure; // replaced by the score once the match starts
	for (auto it = details.commentary.rbegin(); it != details.commentary.rend() && view.feed.size() < rows; ++it) {
		FeedLine line;
		line.minute = QString::fromStdString(it->minute);
		line.kind = it->kind;
		line.side = side_of(it->side);
		std::string text = it->text;
		if (labels.spanish) {
			if (auto spanish = narrate_es(it->text))
				text = *spanish;
		}
		line.text = QString::fromStdString(text);
		view.feed.push_back(line);
	}
	view.rows = rows;
}

void fill_momentum(PanelView &view, const MatchDetails &details, const Match &match, const PanelLabels &labels)
{
	view.momentum = momentum_series(details.plays);
	view.momentum_title = labels.momentum;
	view.now_minute = match.in_progress() ? minute_value(match.clock) : -1;
	if (match.state == MatchState::Scheduled)
		view.momentum.clear();
}

void fill_timeline(PanelView &view, const MatchDetails &details, size_t rows, const PanelLabels &labels)
{
	view.timeline.clear();
	static const QRegularExpression replaces(QStringLiteral(R"((.+?) replaces (.+?)(?: because of an injury)?\.$)"));
	for (auto it = details.plays.rbegin(); it != details.plays.rend() && view.timeline.size() < rows; ++it) {
		const Play &play = *it;
		if (play.kind != PlayKind::Goal && play.kind != PlayKind::YellowCard && play.kind != PlayKind::RedCard &&
		    play.kind != PlayKind::Substitution)
			continue;
		TimelineEvent e;
		e.minute = QString::fromStdString(play.minute);
		e.at = minute_value(play.minute);
		e.kind = play.kind;
		e.home = !play.side || *play.side == TeamSide::Home;
		e.player = QString::fromStdString(play.player);
		switch (play.kind) {
		case PlayKind::Goal:
			e.detail = labels.goal;
			break;
		case PlayKind::YellowCard:
			e.detail = labels.yellow_card;
			break;
		case PlayKind::RedCard:
			e.detail = labels.red_card;
			break;
		case PlayKind::Substitution: {
			// "Substitution, Barcelona. Andreas Christensen replaces João Cancelo."
			const QString sentence = QString::fromStdString(play.text);
			const qsizetype dot = sentence.indexOf(QStringLiteral(". "));
			const auto match = replaces.match(dot >= 0 ? sentence.mid(dot + 2) : sentence);
			if (match.hasMatch()) {
				e.player = match.captured(1);
				e.detail = labels.sub_off.arg(match.captured(2));
			} else {
				e.detail = labels.substitution;
			}
			break;
		}
		default:
			break;
		}
		if (e.player.isEmpty())
			e.player = e.detail;
		view.timeline.push_back(e);
	}
	view.rows = rows;
}

void fill_preview(PanelView &view, const MatchDetails &details, const PanelLabels &labels, const ImageLookup &images)
{
	auto cells = [&](const std::vector<FormGame> &games) {
		std::vector<FormCell> out;
		for (const FormGame &g : games) {
			FormCell c;
			c.outcome = g.result == 'W' ? 1 : g.result == 'L' ? -1 : 0;
			c.result = c.outcome > 0 ? labels.win : c.outcome < 0 ? labels.loss : labels.draw;
			c.score = QString::fromStdString(g.score);
			c.opponent = QString::fromStdString(g.opponent);
			c.opponent_logo = images(g.opponent_logo);
			out.push_back(c);
		}
		return out;
	};
	view.home_form = cells(details.home_form);
	view.away_form = cells(details.away_form);
	view.form_title = labels.form;
	view.meetings_title = labels.meetings;
	// "BAR leads series 3-2" / "Series tied 2-2"
	view.meetings_summary = QString::fromStdString(details.head_to_head);
	static const QRegularExpression leads(QStringLiteral(R"(^(\S+) leads series (\S+)$)"));
	static const QRegularExpression tied(QStringLiteral(R"(^Series tied (\S+)$)"));
	if (const auto m = leads.match(view.meetings_summary); m.hasMatch() && !labels.meetings_leads.isEmpty())
		view.meetings_summary = labels.meetings_leads.arg(m.captured(1), m.captured(2));
	else if (const auto t = tied.match(view.meetings_summary); t.hasMatch() && !labels.meetings_tied.isEmpty())
		view.meetings_summary = labels.meetings_tied.arg(t.captured(1));
	view.meetings.clear();
	for (const MeetingGame &m : details.meetings) {
		view.meetings.push_back(QString::fromStdString(m.date) + QStringLiteral("   ") +
					QString::fromStdString(m.home_abbreviation) + QStringLiteral(" ") +
					QString::fromStdString(m.home_score) + QStringLiteral(" - ") +
					QString::fromStdString(m.away_score) + QStringLiteral(" ") +
					QString::fromStdString(m.away_abbreviation));
	}
	QString venue = QString::fromStdString(details.venue);
	if (!details.city.empty())
		venue += QStringLiteral(", ") + QString::fromStdString(details.city);
	const QLocale numbers(labels.spanish ? QLocale::Spanish : QLocale::English);
	view.venue_line = details.attendance > 0 ? labels.venue_attendance.arg(venue, numbers.toString(details.attendance))
						 : venue;
}

void fill_scorers(PanelView &view, const LeaderBoards &boards, bool assists, const Match *match, size_t rows,
		  const QString &league_name, const PanelLabels &labels, const ImageLookup &images)
{
	const std::vector<LeaderEntry> &list = assists ? boards.assists : boards.goals;
	view.scorers.clear();
	view.scorers_title = (assists ? labels.assists : labels.scorers) + QStringLiteral(" · ") + league_name;
	view.scorers_value_header = assists ? labels.assists_header : labels.goals_header;
	view.scorers_matches_header = labels.matches_header;
	int rank = 0, previous = -1;
	for (size_t i = 0; i < list.size() && view.scorers.size() < rows; ++i) {
		const LeaderEntry &e = list[i];
		if (e.value != previous)
			rank = static_cast<int>(i) + 1; // ties share a rank
		previous = e.value;
		LeaderRow row;
		row.rank = QString::number(rank);
		row.name = QString::fromStdString(e.name);
		row.team = QString::fromStdString(e.team);
		row.logo = images(e.team_logo);
		row.value = QString::number(e.value);
		row.matches = QString::number(e.matches);
		row.highlighted = match && !e.team_id.empty() && (e.team_id == match->home.id || e.team_id == match->away.id);
		view.scorers.push_back(row);
	}
	view.rows = rows;
}

void fill_round(PanelView &view, const Scoreboard &round, const Match *match, size_t rows, const QString &league_name,
		const std::function<QString(const Match &)> &status, const std::function<QString(const Match &)> &center,
		const PanelLabels &labels, const ImageLookup &images)
{
	std::vector<const Match *> order;
	for (const Match &m : round.matches)
		order.push_back(&m);
	auto rank = [](const Match &m) { return m.in_progress() ? 0 : m.state == MatchState::Scheduled ? 1 : 2; };
	std::stable_sort(order.begin(), order.end(), [&](const Match *a, const Match *b) {
		if (rank(*a) != rank(*b))
			return rank(*a) < rank(*b);
		return a->start_utc < b->start_utc;
	});
	view.round.clear();
	view.round_title = labels.round_title + QStringLiteral(" · ") + league_name;
	for (const Match *m : order) {
		if (view.round.size() >= rows)
			break;
		RoundRow row;
		row.home = QString::fromStdString(m->home.short_name.empty() ? m->home.name : m->home.short_name);
		row.away = QString::fromStdString(m->away.short_name.empty() ? m->away.name : m->away.short_name);
		row.home_logo = images(m->home.logo_url);
		row.away_logo = images(m->away.logo_url);
		row.center = center(*m);
		row.status = status(*m);
		row.live = m->in_progress();
		row.highlighted = match && m->id == match->id;
		view.round.push_back(row);
	}
	view.rows = rows;
}

} // namespace fscore::render
