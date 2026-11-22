#include "render/panel_painter.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

namespace fscore::render {
namespace {

constexpr qreal kW = 1000;
const QColor kInk(12, 14, 20);
const QColor kPanel(18, 20, 28, 235);
const QColor kLiveRed(225, 29, 46);
const QColor kGrassA(46, 125, 62);
const QColor kGrassB(40, 112, 56);

QFont font(qreal px, int weight, qreal spacing = 0)
{
	QFont f;
	f.setFamilies({QStringLiteral("Inter Display"), QStringLiteral("Inter"), QStringLiteral("Segoe UI"),
		       QStringLiteral("Helvetica Neue"), QStringLiteral("Arial")});
	f.setPixelSize(std::max(1, static_cast<int>(px)));
	f.setWeight(QFont::Weight(weight));
	f.setStyleStrategy(QFont::PreferAntialias);
	if (spacing != 0)
		f.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
	return f;
}

QFont fit(const QString &text, qreal width, qreal max_px, int weight, qreal spacing = 0)
{
	QFont f = font(max_px, weight, spacing);
	const qreal advance = QFontMetricsF(f).horizontalAdvance(text);
	if (width > 0 && advance > width)
		f = font(std::max(max_px * 0.5, max_px * width / advance * 0.97), weight);
	return f;
}

QColor alpha(QColor c, int a)
{
	c.setAlpha(std::clamp(a, 0, 255));
	return c;
}

// Readable as a fill under white text, and distinct from the grass.
QColor team_tone(QColor c)
{
	for (int i = 0; i < 8 && c.lightnessF() > 0.55; ++i)
		c = c.darker(115);
	return c;
}

void text(QPainter &p, const QRectF &r, const QString &s, int align, const QColor &c, bool shadow = true)
{
	if (r.width() <= 0)
		return;
	const QString shown = QFontMetricsF(p.font()).elidedText(s, Qt::ElideRight, r.width());
	if (shadow) {
		p.setPen(QColor(0, 0, 0, 120 * c.alpha() / 255));
		p.drawText(r.translated(0, 1.5), align, shown);
	}
	p.setPen(c);
	p.drawText(r, align, shown);
}

void contained(QPainter &p, const QImage &image, const QRectF &r, qreal opacity = 1)
{
	if (image.isNull() || r.isEmpty())
		return;
	const QSizeF s = QSizeF(image.size()).scaled(r.size(), Qt::KeepAspectRatio);
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.drawImage(QRectF(r.center().x() - s.width() / 2, r.center().y() - s.height() / 2, s.width(), s.height()), image);
	p.restore();
}

QPainterPath rounded(const QRectF &r, qreal radius)
{
	QPainterPath path;
	path.addRoundedRect(r, radius, radius);
	return path;
}

void live_pill(QPainter &p, QPointF center, qreal h, const PanelView &v)
{
	p.setFont(font(h * 0.55, QFont::Bold, 0.5));
	const qreal tw = QFontMetricsF(p.font()).horizontalAdvance(v.status);
	const qreal dot = v.live ? h * 0.3 : 0;
	const qreal gap = v.live ? dot * 0.8 : 0;
	const qreal w = tw + dot + gap + h * 0.9;
	const QRectF pill(center.x() - w / 2, center.y() - h / 2, w, h);
	p.fillPath(rounded(pill, h / 2), v.live ? kLiveRed : QColor(48, 52, 66));
	const qreal x = pill.center().x() - (tw + dot + gap) / 2;
	if (v.live) {
		p.save();
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, static_cast<int>(255 * v.blink)));
		p.drawEllipse(QRectF(x, pill.center().y() - dot / 2, dot, dot));
		p.restore();
	}
	text(p, QRectF(x + dot + gap, pill.top(), tw + 2, h), v.status, Qt::AlignVCenter | Qt::AlignLeft, Qt::white, false);
}

// Crests, names and score across the top of a panel.
void header(QPainter &p, const QRectF &r, const PanelView &v)
{
	p.fillPath(rounded(r, 14), kPanel);
	const qreal crest = r.height() * 0.72;
	const qreal pad = r.height() * 0.18;
	contained(p, v.home.logo, QRectF(r.left() + pad, r.center().y() - crest / 2, crest, crest));
	contained(p, v.away.logo, QRectF(r.right() - pad - crest, r.center().y() - crest / 2, crest, crest));
	// Team color ticks along the bottom edge.
	p.save();
	p.setClipPath(rounded(r, 14));
	p.fillRect(QRectF(r.left(), r.bottom() - 4, r.width() / 2, 4), v.home.color);
	p.fillRect(QRectF(r.center().x(), r.bottom() - 4, r.width() / 2, 4), v.away.color);
	p.restore();

	const qreal score_w = 170;
	const QRectF score(r.center().x() - score_w / 2, r.top() + 4, score_w, r.height() * 0.6);
	const bool scored = !v.home.score.isEmpty();
	p.setFont(font(r.height() * 0.44, QFont::Black));
	text(p, score, scored ? v.home.score + QStringLiteral(" - ") + v.away.score : QStringLiteral("vs"), Qt::AlignCenter,
	     Qt::white);
	live_pill(p, QPointF(r.center().x(), r.bottom() - r.height() * 0.22), r.height() * 0.26, v);

	const qreal name_left = r.left() + pad + crest + 14;
	const QRectF home_name(name_left, r.top(), score.left() - 10 - name_left, r.height());
	const QRectF away_name(score.right() + 10, r.top(), r.right() - pad - crest - 14 - score.right() - 10, r.height());
	p.setFont(fit(v.home.name.toUpper(), home_name.width(), r.height() * 0.3, QFont::Black, 0.5));
	text(p, home_name, v.home.name.toUpper(), Qt::AlignVCenter | Qt::AlignLeft, Qt::white);
	p.setFont(fit(v.away.name.toUpper(), away_name.width(), r.height() * 0.3, QFont::Black, 0.5));
	text(p, away_name, v.away.name.toUpper(), Qt::AlignVCenter | Qt::AlignRight, Qt::white);
}

// --- the pitch --------------------------------------------------------------

struct Pitch {
	QRectF field; // inside the touchlines
	QPointF map(const PitchPoint &pt) const
	{
		return {field.left() + pt.x * field.width(), field.top() + pt.y * field.height()};
	}
};

Pitch draw_pitch(QPainter &p, const QRectF &area, bool show_ends = true)
{
	p.fillPath(rounded(area, 16), kGrassB);
	p.save();
	p.setClipPath(rounded(area, 16));
	const int stripes = 12;
	for (int i = 0; i < stripes; i += 2)
		p.fillRect(QRectF(area.left() + area.width() * i / stripes, area.top(), area.width() / stripes, area.height()),
			   kGrassA);
	p.restore();

	const qreal margin = area.height() * 0.06;
	Pitch pitch;
	pitch.field = area.adjusted(margin, margin, -margin, -margin);
	const QRectF &f = pitch.field;
	const qreal unit = f.width() / 105.0; // metres
	p.save();
	p.setPen(QPen(QColor(255, 255, 255, 190), 2.2));
	p.setBrush(Qt::NoBrush);
	p.drawRect(f);
	p.drawLine(QPointF(f.center().x(), f.top()), QPointF(f.center().x(), f.bottom()));
	p.drawEllipse(f.center(), 9.15 * unit, 9.15 * unit * f.height() / f.height());
	p.setBrush(QColor(255, 255, 255, 200));
	p.drawEllipse(f.center(), 3, 3);
	p.setBrush(Qt::NoBrush);
	const qreal box_h = 40.3 / 68.0 * f.height(), six_h = 18.3 / 68.0 * f.height();
	for (const bool left : {true, false}) {
		const qreal x0 = left ? f.left() : f.right();
		const qreal dir = left ? 1 : -1;
		p.drawRect(QRectF(left ? x0 : x0 - 16.5 * unit, f.center().y() - box_h / 2, 16.5 * unit, box_h));
		p.drawRect(QRectF(left ? x0 : x0 - 5.5 * unit, f.center().y() - six_h / 2, 5.5 * unit, six_h));
		p.drawPoint(QPointF(x0 + dir * 11 * unit, f.center().y()));
		// Goal mouth.
		if (show_ends) {
			const qreal goal_h = 7.32 / 68.0 * f.height();
			p.save();
			p.setPen(QPen(QColor(255, 255, 255, 230), 4));
			p.drawLine(QPointF(x0 - dir * 2, f.center().y() - goal_h / 2), QPointF(x0 - dir * 2, f.center().y() + goal_h / 2));
			p.restore();
		}
	}
	p.restore();
	return pitch;
}

// A marker for one play, in its team color.
void draw_mark(QPainter &p, const Pitch &pitch, const PitchMark &m, const QColor &color, qreal size, qreal opacity)
{
	const QPointF at = pitch.map(m.at);
	p.save();
	p.setOpacity(opacity);
	const qreal r = size / 2;
	switch (m.kind) {
	case PlayKind::Goal: {
		if (m.has_end) {
			p.setPen(QPen(alpha(Qt::white, 200), 2.5, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(at, pitch.map(m.end));
		}
		// A football: white ball, dark centre patch.
		p.setPen(QPen(kInk, 1.6));
		p.setBrush(Qt::white);
		p.drawEllipse(at, r * 1.25, r * 1.25);
		p.setBrush(kInk);
		p.setPen(Qt::NoPen);
		QPolygonF pent;
		for (int i = 0; i < 5; ++i) {
			const qreal a = -M_PI / 2 + i * 2 * M_PI / 5;
			pent << at + QPointF(std::cos(a), std::sin(a)) * r * 0.5;
		}
		p.drawPolygon(pent);
		break;
	}
	case PlayKind::ShotOnTarget:
	case PlayKind::Woodwork:
		if (m.has_end) {
			p.setPen(QPen(alpha(color.lighter(150), 160), 1.6, Qt::DashLine));
			p.drawLine(at, pitch.map(m.end));
		}
		p.setPen(QPen(Qt::white, 2));
		p.setBrush(color);
		p.drawEllipse(at, r, r);
		break;
	case PlayKind::ShotOffTarget:
		p.setPen(QPen(color.lighter(160), 2.6));
		p.setBrush(alpha(kInk, 120));
		p.drawEllipse(at, r, r);
		break;
	case PlayKind::ShotBlocked: {
		p.setPen(QPen(color.lighter(160), 3, Qt::SolidLine, Qt::RoundCap));
		const qreal d = r * 0.8;
		p.drawLine(at + QPointF(-d, -d), at + QPointF(d, d));
		p.drawLine(at + QPointF(-d, d), at + QPointF(d, -d));
		break;
	}
	case PlayKind::Corner: {
		p.setPen(QPen(Qt::white, 2));
		p.drawLine(at, at + QPointF(0, -r * 2.2));
		QPolygonF flag;
		flag << at + QPointF(0, -r * 2.2) << at + QPointF(r * 1.6, -r * 1.7) << at + QPointF(0, -r * 1.2);
		p.setBrush(color.lighter(130));
		p.drawPolygon(flag);
		break;
	}
	case PlayKind::Offside:
		p.setPen(QPen(Qt::white, 2));
		p.setBrush(QColor(250, 204, 21));
		p.drawRect(QRectF(at.x() - r * 0.6, at.y() - r * 0.6, r * 1.2, r * 1.2));
		break;
	case PlayKind::Foul:
		p.setPen(Qt::NoPen);
		p.setBrush(alpha(color.lighter(140), 220));
		p.drawEllipse(at, r * 0.45, r * 0.45);
		break;
	default:
		p.setPen(Qt::NoPen);
		p.setBrush(alpha(Qt::white, 180));
		p.drawEllipse(at, r * 0.4, r * 0.4);
		break;
	}
	p.restore();
}

void pressure_bar(QPainter &p, const QRectF &r, const PanelView &v)
{
	const qreal share = std::clamp(0.5 + v.pressure / 2.0, 0.04, 0.96);
	p.fillPath(rounded(r, r.height() / 2), alpha(kInk, 200));
	p.save();
	p.setClipPath(rounded(r, r.height() / 2));
	const qreal split = r.left() + r.width() * share;
	QLinearGradient hg(r.topLeft(), QPointF(split, r.top()));
	hg.setColorAt(0, team_tone(v.home.color).darker(130));
	hg.setColorAt(1, team_tone(v.home.color).lighter(120));
	p.fillRect(QRectF(r.left(), r.top(), split - r.left(), r.height()), hg);
	QLinearGradient ag(QPointF(split, r.top()), r.topRight());
	ag.setColorAt(0, team_tone(v.away.color).lighter(120));
	ag.setColorAt(1, team_tone(v.away.color).darker(130));
	p.fillRect(QRectF(split, r.top(), r.right() - split, r.height()), ag);
	p.fillRect(QRectF(split - 1.5, r.top(), 3, r.height()), Qt::white);
	p.restore();
	p.setFont(font(r.height() * 0.62, QFont::Black, 2));
	text(p, r, v.pressure_label, Qt::AlignCenter, Qt::white);
}

void stat_strip(QPainter &p, const QRectF &r, const std::vector<StatRow> &rows)
{
	if (rows.empty())
		return;
	p.fillPath(rounded(r, 12), kPanel);
	const qreal cell = r.width() / rows.size();
	for (size_t i = 0; i < rows.size(); ++i) {
		const QRectF c(r.left() + cell * i, r.top(), cell, r.height());
		if (i > 0) {
			p.fillRect(QRectF(c.left(), c.top() + 10, 1, c.height() - 20), QColor(255, 255, 255, 40));
		}
		p.setFont(font(r.height() * 0.24, QFont::DemiBold, 1.5));
		text(p, QRectF(c.left(), c.top() + 4, c.width(), c.height() * 0.4), rows[i].label.toUpper(), Qt::AlignCenter,
		     QColor(255, 255, 255, 170), false);
		p.setFont(font(r.height() * 0.36, QFont::Black));
		text(p, QRectF(c.left(), c.top() + c.height() * 0.4, c.width(), c.height() * 0.55),
		     rows[i].home + QStringLiteral("  –  ") + rows[i].away, Qt::AlignCenter, Qt::white);
	}
}

void paint_live_pitch(QPainter &p, const PanelView &v)
{
	header(p, QRectF(0, 0, kW, 84), v);
	pressure_bar(p, QRectF(150, 96, kW - 300, 20), v);
	const Pitch pitch = draw_pitch(p, QRectF(0, 128, kW, 600));

	// Direction of attack, faded crests behind each goal.
	contained(p, v.home.logo, QRectF(pitch.field.left() + 40, pitch.field.center().y() - 90, 180, 180), 0.10);
	contained(p, v.away.logo, QRectF(pitch.field.right() - 220, pitch.field.center().y() - 90, 180, 180), 0.10);

	// Old shots first (small, faint), then recent plays, the latest on top.
	std::vector<const PitchMark *> order;
	for (const PitchMark &m : v.marks)
		order.push_back(&m);
	std::stable_sort(order.begin(), order.end(), [](auto *a, auto *b) { return a->recency > b->recency; });
	const PitchMark *latest = nullptr;
	for (const PitchMark *m : order) {
		const QColor color = team_tone(m->home ? v.home.color : v.away.color);
		const bool recent = m->recency < 6;
		const qreal size = m->recency == 0 ? 26 : recent ? 20 : 14;
		const qreal opacity = m->recency == 0 ? 1.0 : recent ? 0.95 - m->recency * 0.08 : 0.55;
		draw_mark(p, pitch, *m, color, size, opacity);
		if (m->recency == 0)
			latest = m;
	}

	if (latest) {
		const QPointF at = pitch.map(latest->at);
		// Pulsing ring.
		p.save();
		const qreal grow = 14 + 26 * v.pulse;
		p.setPen(QPen(alpha(Qt::white, static_cast<int>(230 * (1 - v.pulse))), 3));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(at, grow, grow);
		p.restore();
		// Caption bubble, kept inside the pitch.
		if (!v.caption.isEmpty()) {
			p.setFont(font(19, QFont::Black, 0.5));
			const qreal w1 = QFontMetricsF(p.font()).horizontalAdvance(v.caption);
			QFont sub_font = font(16, QFont::DemiBold);
			const qreal w2 = QFontMetricsF(sub_font).horizontalAdvance(v.caption_sub);
			const qreal w = std::min<qreal>(std::max(w1, w2) + 32, 420);
			const qreal h = v.caption_sub.isEmpty() ? 38 : 62;
			// Above, below, then beside the play: the first spot inside the pitch that
			// hides no goal and no shot of the last plays.
			const QRectF candidates[] = {
				QRectF(at.x() - w / 2, at.y() - 34 - h, w, h), QRectF(at.x() - w / 2, at.y() + 34, w, h),
				QRectF(at.x() - w - 34, at.y() - h / 2, w, h), QRectF(at.x() + 34, at.y() - h / 2, w, h)};
			auto inside = [&pitch](QRectF r) {
				r.moveLeft(std::clamp(r.left(), pitch.field.left() + 4, pitch.field.right() - r.width() - 4));
				r.moveTop(std::clamp(r.top(), pitch.field.top() + 4, pitch.field.bottom() - r.height() - 4));
				return r;
			};
			auto hides = [&](const QRectF &r) {
				int hidden = 0;
				for (const PitchMark &m : v.marks) {
					if (&m == latest || (m.kind != PlayKind::Goal && m.recency >= 6))
						continue;
					hidden += r.adjusted(-14, -14, 14, 14).contains(pitch.map(m.at)) ? (m.kind == PlayKind::Goal ? 10 : 1) : 0;
				}
				return hidden + (r.contains(at) ? 100 : 0);
			};
			QRectF bubble = inside(candidates[0]);
			int best = hides(bubble);
			for (const QRectF &candidate : candidates) {
				const QRectF placed = inside(candidate);
				const int score = hides(placed);
				if (score < best) {
					best = score;
					bubble = placed;
				}
			}
			const QColor fill = team_tone(latest->home ? v.home.color : v.away.color).darker(140);
			p.fillPath(rounded(bubble.translated(0, 3), 10), QColor(0, 0, 0, 90));
			p.fillPath(rounded(bubble, 10), alpha(fill, 240));
			p.save();
			p.setPen(QPen(QColor(255, 255, 255, 120), 1.5));
			p.setBrush(Qt::NoBrush);
			p.drawPath(rounded(bubble, 10));
			p.restore();
			text(p, QRectF(bubble.left() + 16, bubble.top() + 6, bubble.width() - 32, 28), v.caption,
			     Qt::AlignVCenter | Qt::AlignLeft, Qt::white);
			if (!v.caption_sub.isEmpty()) {
				p.setFont(sub_font);
				text(p, QRectF(bubble.left() + 16, bubble.top() + 32, bubble.width() - 32, 24), v.caption_sub,
				     Qt::AlignVCenter | Qt::AlignLeft, QColor(255, 255, 255, 220));
			}
		}
	} else if (!v.no_plays.isEmpty()) {
		const QRectF r(pitch.field.center().x() - 260, pitch.field.center().y() - 28, 520, 56);
		p.fillPath(rounded(r, 28), QColor(0, 0, 0, 120));
		p.setFont(font(20, QFont::DemiBold));
		text(p, r, v.no_plays, Qt::AlignCenter, Qt::white);
	}

	stat_strip(p, QRectF(0, 740, kW, 70), v.strip);
}

void paint_lineups(QPainter &p, const PanelView &v)
{
	header(p, QRectF(0, 0, kW, 84), v);
	const Pitch pitch = draw_pitch(p, QRectF(0, 96, kW, 620), false);
	p.setFont(font(22, QFont::Black, 2));
	text(p, QRectF(pitch.field.left() + 10, pitch.field.top() + 6, 200, 30), v.home_formation,
	     Qt::AlignLeft | Qt::AlignVCenter, QColor(255, 255, 255, 220));
	text(p, QRectF(pitch.field.right() - 210, pitch.field.top() + 6, 200, 30), v.away_formation,
	     Qt::AlignRight | Qt::AlignVCenter, QColor(255, 255, 255, 220));
	for (const LineupMark &m : v.players) {
		const QPointF at = pitch.map(m.at);
		const QColor color = team_tone(m.home ? v.home.color : v.away.color);
		const qreal r = 20;
		p.save();
		p.setPen(QPen(Qt::white, 2.5));
		p.setBrush(color);
		p.drawEllipse(at, r, r);
		p.restore();
		p.setFont(font(17, QFont::Black));
		text(p, QRectF(at.x() - r, at.y() - r, 2 * r, 2 * r), m.jersey, Qt::AlignCenter, Qt::white, false);
		if (m.subbed_out) {
			p.save();
			p.setPen(Qt::NoPen);
			p.setBrush(kLiveRed);
			p.drawEllipse(at + QPointF(r * 0.8, -r * 0.8), 7, 7);
			p.restore();
		}
		p.setFont(fit(m.name, 132, 15, QFont::Bold));
		const QRectF label(at.x() - 66, at.y() + r + 2, 132, 22);
		const qreal tw = std::min<qreal>(QFontMetricsF(p.font()).horizontalAdvance(m.name) + 12, 132);
		p.fillPath(rounded(QRectF(at.x() - tw / 2, label.top(), tw, label.height()), 6), QColor(0, 0, 0, 140));
		text(p, label, m.name, Qt::AlignCenter, Qt::white, false);
	}
}

void paint_stats(QPainter &p, const PanelView &v)
{
	header(p, QRectF(0, 0, kW, 84), v);
	const QRectF body(0, 96, kW, 64.0 * std::max<size_t>(v.stats.size(), 1) + 24);
	p.fillPath(rounded(body, 16), kPanel);
	const qreal row_h = 64;
	for (size_t i = 0; i < v.stats.size(); ++i) {
		const StatRow &s = v.stats[i];
		const QRectF row(40, body.top() + 12 + row_h * i, kW - 80, row_h);
		p.setFont(font(26, QFont::Black));
		text(p, QRectF(row.left(), row.top(), 120, 34), s.home, Qt::AlignLeft | Qt::AlignVCenter, Qt::white);
		text(p, QRectF(row.right() - 120, row.top(), 120, 34), s.away, Qt::AlignRight | Qt::AlignVCenter, Qt::white);
		p.setFont(font(17, QFont::DemiBold, 1.5));
		text(p, QRectF(row.left() + 120, row.top(), row.width() - 240, 34), s.label.toUpper(), Qt::AlignCenter,
		     QColor(255, 255, 255, 190), false);
		const QRectF bar(row.left(), row.top() + 40, row.width(), 10);
		p.fillPath(rounded(bar, 5), QColor(255, 255, 255, 30));
		const qreal gap = 6;
		const qreal hw = (bar.width() - gap) * std::clamp(s.home_share, 0.0, 1.0);
		const qreal aw = bar.width() - gap - hw;
		const bool home_leads = s.home_share > 0.5, away_leads = s.home_share < 0.5;
		p.fillPath(rounded(QRectF(bar.left(), bar.top(), hw, bar.height()), 5),
			   home_leads ? team_tone(v.home.color).lighter(115) : alpha(team_tone(v.home.color), 150));
		p.fillPath(rounded(QRectF(bar.right() - aw, bar.top(), aw, bar.height()), 5),
			   away_leads ? team_tone(v.away.color).lighter(115) : alpha(team_tone(v.away.color), 150));
	}
}

void paint_table(QPainter &p, const PanelView &v)
{
	const qreal row_h = 46, top = 64;
	const QRectF body(0, 0, kW * 0.62, top + row_h * v.table.size() + 14);
	p.fillPath(rounded(body, 16), kPanel);
	p.setFont(fit(v.table_title.toUpper(), body.width() * 0.5, 22, QFont::Black, 1.5));
	text(p, QRectF(24, 8, body.width() * 0.55, 48), v.table_title.toUpper(), Qt::AlignLeft | Qt::AlignVCenter,
	     Qt::white);
	const qreal cols[6] = {0.58, 0.645, 0.71, 0.775, 0.86, 0.95};
	p.setFont(font(15, QFont::Bold, 1));
	for (int c = 0; c < 6; ++c)
		text(p, QRectF(body.width() * cols[c] - 30, 8, 60, 48), v.table_header[c], Qt::AlignCenter,
		     QColor(255, 255, 255, 150), false);
	for (size_t i = 0; i < v.table.size(); ++i) {
		const TableRow &r = v.table[i];
		const QRectF row(10, top + row_h * i, body.width() - 20, row_h);
		if (r.highlighted)
			p.fillPath(rounded(row.adjusted(0, 2, 0, -2), 8), QColor(255, 255, 255, 34));
		else if (i % 2)
			p.fillRect(row.adjusted(0, 2, 0, -2), QColor(255, 255, 255, 8));
		if (r.zone.isValid())
			p.fillRect(QRectF(row.left(), row.top() + 8, 4, row.height() - 16), r.zone);
		p.setFont(font(18, QFont::Bold));
		text(p, QRectF(row.left() + 10, row.top(), 34, row.height()), r.rank, Qt::AlignCenter, Qt::white, false);
		contained(p, r.logo, QRectF(row.left() + 50, row.top() + 7, row.height() - 14, row.height() - 14));
		p.setFont(font(19, r.highlighted ? QFont::Black : QFont::DemiBold));
		text(p, QRectF(row.left() + 50 + row.height(), row.top(), body.width() * cols[0] - 120 - row.height(), row.height()),
		     r.name, Qt::AlignVCenter | Qt::AlignLeft, Qt::white, false);
		const QString values[6] = {r.played, r.wins, r.draws, r.losses, r.goal_difference, r.points};
		for (int c = 0; c < 6; ++c) {
			p.setFont(font(c == 5 ? 20 : 18, c == 5 ? QFont::Black : QFont::Medium));
			text(p, QRectF(body.width() * cols[c] - 30, row.top(), 60, row.height()), values[c], Qt::AlignCenter,
			     c == 5 ? QColor(Qt::white) : QColor(255, 255, 255, 210), false);
		}
	}
}

} // namespace

QSize panel_natural_size(PanelContent content, size_t table_rows)
{
	switch (content) {
	case PanelContent::LivePitch:
		return {1000, 810};
	case PanelContent::Lineups:
		return {1000, 716};
	case PanelContent::Stats:
		return {1000, 96 + 64 * 8 + 24};
	case PanelContent::Table:
		return {620, static_cast<int>(64 + 46 * std::max<size_t>(table_rows, 1) + 14)};
	}
	return {1000, 810};
}

QImage paint_panel(const PanelView &view, QSize size)
{
	QImage image(size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	if (size.isEmpty())
		return image;
	const QSize natural = panel_natural_size(view.content, view.table.size());
	QPainter p(&image);
	p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
	p.scale(qreal(size.width()) / natural.width(), qreal(size.height()) / natural.height());

	if (!view.has_data) {
		const QRectF r(natural.width() / 2.0 - 280, natural.height() / 2.0 - 36, 560, 72);
		p.fillPath(rounded(r, 36), kPanel);
		p.setFont(font(24, QFont::DemiBold));
		text(p, r, view.message, Qt::AlignCenter, Qt::white);
		return image;
	}
	switch (view.content) {
	case PanelContent::LivePitch:
		paint_live_pitch(p, view);
		break;
	case PanelContent::Lineups:
		paint_lineups(p, view);
		break;
	case PanelContent::Stats:
		paint_stats(p, view);
		break;
	case PanelContent::Table:
		paint_table(p, view);
		break;
	}
	return image;
}

} // namespace fscore::render
