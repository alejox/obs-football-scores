#include "render/panel_painter.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace fscore::render {
namespace {

constexpr qreal kW = 1000;
const QColor kInk(12, 14, 20);
const QColor kPanel(18, 20, 28, 235);
const QColor kLiveRed(225, 29, 46);

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

// --- the pitch --------------------------------------------------------------

// Where the field sits on screen. From above it is a rectangle; in perspective the
// flat field is projected onto a trapezoid, the far touchline shorter than the near one.
struct Pitch {
	QRectF field;      // the flat field, before projection
	bool perspective = false;
	QTransform project; // flat field -> screen (perspective only)

	QPointF map(const PitchPoint &pt) const
	{
		const QPointF flat(field.left() + pt.x * field.width(), field.top() + pt.y * field.height());
		return perspective ? project.map(flat) : flat;
	}
	// Markers shrink with distance in perspective.
	qreal scale(const PitchPoint &pt) const { return perspective ? 0.72 + 0.32 * pt.y : 1.0; }
};

QPainterPath ellipse_path(const QPointF &c, qreal rx, qreal ry)
{
	QPainterPath path;
	path.addEllipse(c, rx, ry);
	return path;
}

// Field markings in flat field coordinates (the caller sets any projection).
void draw_markings(QPainter &p, const QRectF &f, qreal width)
{
	const qreal mx = f.width() / 105.0, my = f.height() / 68.0; // metres
	p.setPen(QPen(QColor(255, 255, 255, 215), width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
	p.setBrush(Qt::NoBrush);
	p.drawRect(f);
	p.drawLine(QPointF(f.center().x(), f.top()), QPointF(f.center().x(), f.bottom()));
	p.drawPath(ellipse_path(f.center(), 9.15 * mx, 9.15 * my));
	p.setBrush(QColor(255, 255, 255, 220));
	p.drawPath(ellipse_path(f.center(), 0.45 * mx, 0.45 * my));
	p.setBrush(Qt::NoBrush);
	for (const bool left : {true, false}) {
		const qreal x0 = left ? f.left() : f.right();
		const qreal dir = left ? 1 : -1;
		const qreal box_h = 40.3 * my, six_h = 18.3 * my;
		p.drawRect(QRectF(left ? x0 : x0 - 16.5 * mx, f.center().y() - box_h / 2, 16.5 * mx, box_h));
		p.drawRect(QRectF(left ? x0 : x0 - 5.5 * mx, f.center().y() - six_h / 2, 5.5 * mx, six_h));
		p.setBrush(QColor(255, 255, 255, 220));
		p.drawPath(ellipse_path(QPointF(x0 + dir * 11 * mx, f.center().y()), 0.4 * mx, 0.4 * my));
		p.setBrush(Qt::NoBrush);
		// The "D" outside the box.
		const QRectF arc(x0 + dir * 11 * mx - 9.15 * mx, f.center().y() - 9.15 * my, 18.3 * mx, 18.3 * my);
		p.drawArc(arc, left ? -53 * 16 : 127 * 16, 106 * 16);
		// Corner arcs.
		for (const bool top : {true, false}) {
			const QPointF corner(x0, top ? f.top() : f.bottom());
			const QRectF r(corner.x() - mx, corner.y() - my, 2 * mx, 2 * my);
			const int start = left ? (top ? 270 : 0) : (top ? 180 : 90);
			p.drawArc(r, start * 16, 90 * 16);
		}
	}
}

// Mowing stripes, light from above and a soft vignette, in flat field coordinates.
void draw_grass(QPainter &p, const QRectF &area)
{
	QLinearGradient base(area.topLeft(), area.bottomLeft());
	base.setColorAt(0, QColor(36, 112, 54));
	base.setColorAt(1, QColor(30, 96, 46));
	p.fillRect(area, base);
	const int stripes = 14;
	for (int i = 0; i < stripes; i += 2)
		p.fillRect(QRectF(area.left() + area.width() * i / stripes, area.top(), area.width() / stripes, area.height()),
			   QColor(255, 255, 255, 14));
	QRadialGradient light(area.center(), area.width() * 0.62);
	light.setColorAt(0, QColor(255, 255, 255, 22));
	light.setColorAt(1, QColor(0, 0, 0, 70));
	p.fillRect(area, light);
}

void draw_goal_frames(QPainter &p, const Pitch &pitch)
{
	for (const bool left : {true, false}) {
		const double x = left ? 0.0 : 1.0;
		const double half = 3.66 / 68.0;
		const QPointF a = pitch.map({x, 0.5 - half}), b = pitch.map({x, 0.5 + half});
		const qreal depth = (pitch.perspective ? 10 : 8) * (left ? -1 : 1);
		const qreal lift = pitch.perspective ? 16 * pitch.scale({x, 0.5}) : 0;
		QPolygonF net;
		net << a << QPointF(a.x() + depth, a.y() - lift * 0.4) << QPointF(b.x() + depth, b.y() - lift * 0.4) << b;
		p.save();
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 40));
		p.drawPolygon(net);
		p.setPen(QPen(Qt::white, 3.2, Qt::SolidLine, Qt::RoundCap));
		if (pitch.perspective) {
			// Posts and crossbar standing up from the goal line.
			p.drawLine(a, a - QPointF(0, lift));
			p.drawLine(b, b - QPointF(0, lift));
			p.drawLine(a - QPointF(0, lift), b - QPointF(0, lift));
		} else {
			p.drawLine(a, b);
		}
		p.restore();
	}
}

// The stands, the advertising boards and the floodlit sky behind a perspective pitch.
void draw_stadium(QPainter &p, const QRectF &area, const QPolygonF &field_quad, const QString &board_text)
{
	QLinearGradient sky(area.topLeft(), area.bottomLeft());
	sky.setColorAt(0, QColor(10, 12, 20));
	sky.setColorAt(1, QColor(26, 30, 42));
	p.fillPath(rounded(area, 16), sky);
	p.save();
	p.setClipPath(rounded(area, 16));
	// Rows of seats: thin bands above the far touchline.
	const qreal far_y = field_quad[0].y();
	for (int i = 0; i < 14; ++i) {
		const qreal y = area.top() + (far_y - 30 - area.top()) * i / 14.0;
		p.fillRect(QRectF(area.left(), y, area.width(), 3), QColor(255, 255, 255, 10 + i));
	}
	// Floodlight glow.
	for (const qreal x : {area.left() + area.width() * 0.12, area.right() - area.width() * 0.12}) {
		QRadialGradient glow(QPointF(x, area.top() + 10), area.width() * 0.25);
		glow.setColorAt(0, QColor(255, 255, 230, 60));
		glow.setColorAt(1, QColor(255, 255, 230, 0));
		p.fillRect(area, glow);
	}
	// The crowd: small dots in muted colors over the stands, placed the same way on
	// every frame so they do not flicker.
	uint32_t seed = 20261122;
	auto next = [&seed] {
		seed = seed * 1664525u + 1013904223u;
		return (seed >> 8) / 16777216.0;
	};
	const QColor crowd[] = {QColor(200, 205, 215), QColor(150, 155, 170), QColor(210, 60, 60), QColor(60, 110, 200),
				QColor(230, 200, 90), QColor(90, 95, 110)};
	p.setPen(Qt::NoPen);
	const qreal stand_bottom = far_y - 30;
	for (int i = 0; i < 1600; ++i) {
		const qreal t = next();
		const qreal y = area.top() + 8 + t * (stand_bottom - area.top() - 8);
		const qreal x = area.left() + next() * area.width();
		const qreal r = 1.2 + 1.6 * t;
		QColor c = crowd[static_cast<int>(next() * 6) % 6];
		c.setAlpha(static_cast<int>(70 + 90 * t));
		p.setBrush(c);
		p.drawEllipse(QPointF(x, y), r, r);
	}
	// Side stands, darker, running down towards the near corners.
	for (const bool left : {true, false}) {
		QPolygonF side;
		const QPointF far_corner = left ? field_quad[0] : field_quad[1];
		const QPointF near_corner = left ? field_quad[3] : field_quad[2];
		const qreal edge = left ? area.left() : area.right();
		side << QPointF(edge, far_y - 30) << QPointF(far_corner.x() + (left ? -34 : 34), far_y - 8)
		     << QPointF(near_corner.x() + (left ? -12 : 12), near_corner.y()) << QPointF(edge, near_corner.y());
		QLinearGradient shade(QPointF(edge, 0), QPointF(far_corner.x(), 0));
		shade.setColorAt(0, QColor(8, 10, 16, 235));
		shade.setColorAt(1, QColor(18, 22, 32, 200));
		p.setBrush(shade);
		p.drawPolygon(side);
	}
	// Advertising boards along the far touchline.
	const QRectF boards(field_quad[0].x() - 30, far_y - 26, field_quad[1].x() - field_quad[0].x() + 60, 18);
	p.fillRect(boards, QColor(16, 18, 26));
	p.setFont(font(11, QFont::Black, 2));
	const qreal step = QFontMetricsF(p.font()).horizontalAdvance(board_text) + 40;
	for (qreal x = boards.left() + 10; x < boards.right(); x += step)
		text(p, QRectF(x, boards.top(), step, boards.height()), board_text, Qt::AlignVCenter | Qt::AlignLeft,
		     QColor(255, 255, 255, 150), false);
	p.fillRect(QRectF(boards.left(), boards.bottom(), boards.width(), 2), QColor(255, 255, 255, 40));
	p.restore();
}

Pitch draw_pitch(QPainter &p, const QRectF &area, PitchView view, const QString &board_text)
{
	Pitch pitch;
	if (view == PitchView::TopDown) {
		p.save();
		p.setClipPath(rounded(area, 16));
		draw_grass(p, area);
		p.restore();
		const qreal margin = area.height() * 0.06;
		pitch.field = area.adjusted(margin, margin, -margin, -margin);
		draw_markings(p, pitch.field, 2.2);
		draw_goal_frames(p, pitch);
		return pitch;
	}

	// Perspective: the flat field is mapped onto a trapezoid.
	pitch.perspective = true;
	pitch.field = QRectF(0, 0, 1050, 680);
	const qreal near_inset = area.width() * 0.035, far_inset = area.width() * 0.17;
	const QPolygonF quad = {QPointF(area.left() + far_inset, area.top() + area.height() * 0.28),
				QPointF(area.right() - far_inset, area.top() + area.height() * 0.28),
				QPointF(area.right() - near_inset, area.bottom() - area.height() * 0.05),
				QPointF(area.left() + near_inset, area.bottom() - area.height() * 0.05)};
	QTransform::quadToQuad(QPolygonF(pitch.field).mid(0, 4), quad, pitch.project);
	draw_stadium(p, area, quad, board_text);
	p.save();
	p.setClipPath(rounded(area, 16));
	p.setTransform(pitch.project, true);
	// Grass beyond the lines, then the field itself.
	draw_grass(p, pitch.field.adjusted(-60, -40, 60, 50));
	draw_markings(p, pitch.field, 3.0);
	p.restore();
	draw_goal_frames(p, pitch);
	return pitch;
}

// A marker for one play, in its team color.
void draw_mark(QPainter &p, const Pitch &pitch, const PitchMark &m, const QColor &color, qreal size, qreal opacity)
{
	const QPointF at = pitch.map(m.at);
	const qreal r = size / 2 * pitch.scale(m.at);
	p.save();
	p.setOpacity(opacity);
	// Contact shadow, so markers sit on the grass.
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 70));
	p.drawEllipse(at + QPointF(0, r * 0.35), r * 0.95, r * 0.45);
	switch (m.kind) {
	case PlayKind::Goal: {
		if (m.has_end) {
			p.setPen(QPen(alpha(Qt::white, 210), 2.5, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(at, pitch.map(m.end));
		}
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
			p.setPen(QPen(alpha(color.lighter(160), 170), 1.8, Qt::DashLine));
			p.drawLine(at, pitch.map(m.end));
		}
		p.setPen(QPen(Qt::white, 2));
		p.setBrush(color);
		p.drawEllipse(at, r, r);
		break;
	case PlayKind::ShotOffTarget:
		p.setPen(QPen(color.lighter(170), 2.6));
		p.setBrush(alpha(kInk, 130));
		p.drawEllipse(at, r, r);
		break;
	case PlayKind::ShotBlocked: {
		p.setPen(QPen(color.lighter(170), 3, Qt::SolidLine, Qt::RoundCap));
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
		p.setPen(QPen(Qt::white, 1.2));
		p.setBrush(alpha(color.lighter(140), 230));
		p.drawEllipse(at, r * 0.5, r * 0.5);
		break;
	default:
		p.setPen(Qt::NoPen);
		p.setBrush(alpha(Qt::white, 190));
		p.drawEllipse(at, r * 0.4, r * 0.4);
		break;
	}
	p.restore();
}

bool attacking(PlayKind kind)
{
	return kind == PlayKind::Goal || kind == PlayKind::ShotOnTarget || kind == PlayKind::ShotOffTarget ||
	       kind == PlayKind::ShotBlocked || kind == PlayKind::Woodwork || kind == PlayKind::Corner;
}

// The latest play: a ball, a wedge towards the goal under attack and a two-line tag
// ("Barcelona" / "Remate al arco · 67'"), like a broadcast tracker.
void draw_ball(QPainter &p, const QPointF &at, qreal r)
{
	p.save();
	p.setPen(QPen(kInk, 1.4));
	p.setBrush(Qt::white);
	p.drawEllipse(at, r, r);
	p.setPen(Qt::NoPen);
	p.setBrush(kInk);
	QPolygonF pent;
	for (int i = 0; i < 5; ++i) {
		const qreal a = -M_PI / 2 + i * 2 * M_PI / 5;
		pent << at + QPointF(std::cos(a), std::sin(a)) * r * 0.45;
	}
	p.drawPolygon(pent);
	p.restore();
}

// The ball's route over the last plays: a dotted line that fades towards the older
// ones, the newest stretch drawn as far as the ball has travelled. Returns true while
// the ball is still on its way (it is drawn here, in flight).
bool draw_ball_route(QPainter &p, const Pitch &pitch, const PanelView &v)
{
	const size_t n = v.ball_route.size();
	if (n < 2)
		return false;
	// Ease in and out, so the ball leaves and arrives gently.
	const double raw = std::clamp(v.ball_progress, 0.0, 1.0);
	const double t = raw * raw * (3 - 2 * raw);
	const PitchPoint from = v.ball_route[n - 2], to = v.ball_route[n - 1];
	const PitchPoint now{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
	p.save();
	for (size_t i = 0; i + 1 < n; ++i) {
		const bool last = i + 2 == n;
		const int a = static_cast<int>(60 + 150.0 * (i + 1) / (n - 1));
		QPen pen(alpha(Qt::white, a), last ? 3.2 : 2.2, last ? Qt::SolidLine : Qt::DotLine, Qt::RoundCap);
		p.setPen(pen);
		p.drawLine(pitch.map(v.ball_route[i]), pitch.map(last ? now : v.ball_route[i + 1]));
		if (!last) {
			p.setPen(Qt::NoPen);
			p.setBrush(alpha(Qt::white, a));
			const qreal r = 3.5 * pitch.scale(v.ball_route[i + 1]);
			p.drawEllipse(pitch.map(v.ball_route[i + 1]), r, r);
		}
	}
	p.restore();
	if (raw >= 1.0)
		return false;
	// In flight: a soft shadow on the grass and the ball lifted a little over it.
	const QPointF ground = pitch.map(now);
	const qreal s = pitch.scale(now);
	const qreal lift = std::sin(M_PI * t) * 26 * s;
	p.save();
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 90));
	p.drawEllipse(ground, 9 * s, 4.5 * s);
	p.restore();
	draw_ball(p, ground - QPointF(0, lift), 11 * s);
	return true;
}

void draw_latest(QPainter &p, const Pitch &pitch, const PitchMark &m, const PanelView &v, const QColor &team)
{
	const QPointF at = pitch.map(m.at);
	const qreal s = pitch.scale(m.at);
	if (attacking(m.kind)) {
		const double goal_x = m.home ? 1.0 : 0.0;
		QPolygonF wedge;
		wedge << at << pitch.map({goal_x, 0.5 - 0.09}) << pitch.map({goal_x, 0.5 + 0.09});
		QLinearGradient fade(at, pitch.map({goal_x, 0.5}));
		fade.setColorAt(0, QColor(255, 255, 255, 110));
		fade.setColorAt(1, QColor(255, 255, 255, 10));
		p.save();
		p.setPen(Qt::NoPen);
		p.setBrush(fade);
		p.drawPolygon(wedge);
		p.restore();
	}
	// Pulsing ring.
	p.save();
	const qreal grow = (16 + 30 * v.pulse) * s;
	p.setPen(QPen(alpha(Qt::white, static_cast<int>(230 * (1 - v.pulse))), 3));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(at, grow, grow * (pitch.perspective ? 0.6 : 1.0));
	p.restore();
	// Ball.
	if (m.kind != PlayKind::Goal)
		draw_ball(p, at, 11 * s);
	if (v.caption.isEmpty())
		return;

	// Tag: team name on its color, then what happened.
	const QString top = v.latest_team.toUpper();
	const QString bottom = v.caption;
	const QFont top_font = font(19, QFont::Black, 0.5), bottom_font = font(16, QFont::Bold);
	const qreal w = std::min<qreal>(std::max(QFontMetricsF(top_font).horizontalAdvance(top),
						 QFontMetricsF(bottom_font).horizontalAdvance(bottom)) + 30,
					380);
	const qreal h_top = 32, h_bottom = 28;
	const QRectF candidates[] = {QRectF(at.x() - w - 18, at.y() - h_top - h_bottom / 2, w, h_top + h_bottom),
				     QRectF(at.x() + 18, at.y() - h_top - h_bottom / 2, w, h_top + h_bottom),
				     QRectF(at.x() - w / 2, at.y() - 30 - h_top - h_bottom, w, h_top + h_bottom),
				     QRectF(at.x() - w / 2, at.y() + 30, w, h_top + h_bottom)};
	const QRectF bounds = pitch.perspective ? QRectF(pitch.map({0, 0}).x() - 80, pitch.map({0, 0}).y() - 40,
							 pitch.map({1, 1}).x() - pitch.map({0, 0}).x() + 160,
							 pitch.map({1, 1}).y() - pitch.map({0, 0}).y() + 60)
						: pitch.field;
	auto inside = [&bounds](QRectF r) {
		r.moveLeft(std::clamp(r.left(), bounds.left() + 4, bounds.right() - r.width() - 4));
		r.moveTop(std::clamp(r.top(), bounds.top() + 4, bounds.bottom() - r.height() - 4));
		return r;
	};
	auto hides = [&](const QRectF &r) {
		int hidden = r.adjusted(-6, -6, 6, 6).contains(at) ? 100 : 0;
		for (const PitchMark &other : v.marks) {
			if (&other == &m || (other.kind != PlayKind::Goal && other.recency >= 6))
				continue;
			if (r.adjusted(-12, -12, 12, 12).contains(pitch.map(other.at)))
				hidden += other.kind == PlayKind::Goal ? 10 : 1;
		}
		return hidden;
	};
	// The side away from the goal under attack reads first, like the broadcast tag.
	QRectF tag = inside(m.home ? candidates[0] : candidates[1]);
	int best = hides(tag);
	for (const QRectF &candidate : candidates) {
		const QRectF placed = inside(candidate);
		const int score = hides(placed);
		if (score < best) {
			best = score;
			tag = placed;
		}
	}
	const QRectF top_rect(tag.left(), tag.top(), tag.width(), h_top);
	const QRectF bottom_rect(tag.left(), tag.top() + h_top, tag.width(), h_bottom);
	p.fillPath(rounded(tag.translated(0, 3), 9), QColor(0, 0, 0, 90));
	QPainterPath top_path;
	top_path.addRoundedRect(top_rect, 9, 9);
	QPainterPath bottom_path;
	bottom_path.addRoundedRect(bottom_rect, 9, 9);
	p.fillPath(bottom_path, QColor(12, 14, 20, 235));
	p.fillPath(top_path, team);
	p.setFont(top_font);
	text(p, top_rect.adjusted(14, 0, -14, 0), top, Qt::AlignVCenter | Qt::AlignLeft, Qt::white);
	p.setFont(bottom_font);
	text(p, bottom_rect.adjusted(14, 0, -14, 0), bottom, Qt::AlignVCenter | Qt::AlignLeft, QColor(255, 255, 255, 230),
	     false);
}

// Momentum: one bar per minute, up for home and down for away, a soft line over them.
// `scale` leaves room under the bars for the minute marks.
void draw_momentum(QPainter &p, const QRectF &r, const PanelView &v, bool labels, qreal scale = 0)
{
	p.fillPath(rounded(r, 12), kPanel);
	const QRectF plot = r.adjusted(labels ? 54 : 14, 8, labels ? -54 : -14, -8 - scale);
	const qreal mid = plot.center().y();
	const size_t n = std::max<size_t>(v.momentum.size(), 91);
	const qreal step = plot.width() / n;
	const QColor home = team_tone(v.home.color).lighter(120), away = team_tone(v.away.color).lighter(120);
	p.save();
	p.setPen(QPen(QColor(255, 255, 255, 40), 1, Qt::DashLine));
	p.drawLine(QPointF(plot.left(), mid), QPointF(plot.right(), mid));
	p.drawLine(QPointF(plot.left() + step * 45.5, plot.top()), QPointF(plot.left() + step * 45.5, plot.bottom()));
	p.restore();
	for (size_t i = 0; i < v.momentum.size(); ++i) {
		const double value = v.momentum[i];
		if (std::abs(value) < 0.01)
			continue;
		const qreal h = std::abs(value) * plot.height() / 2;
		const QRectF bar(plot.left() + step * i + step * 0.12, value > 0 ? mid - h : mid, step * 0.76, h);
		p.fillRect(bar, value > 0 ? home : away);
	}
	if (v.now_minute >= 0) {
		const qreal x = plot.left() + step * std::min<double>(v.now_minute, n - 1) + step / 2;
		p.fillRect(QRectF(x - 1, plot.top(), 2, plot.height()), QColor(255, 255, 255, 170));
	}
	if (labels) {
		contained(p, v.home.logo, QRectF(r.left() + 10, plot.top(), 34, plot.height() / 2 - 2));
		contained(p, v.away.logo, QRectF(r.left() + 10, plot.center().y() + 2, 34, plot.height() / 2 - 2));
		p.setFont(font(13, QFont::Bold, 1));
		text(p, QRectF(r.right() - 50, r.top(), 46, r.height() / 2), QStringLiteral("0'"), Qt::AlignCenter,
		     QColor(255, 255, 255, 0), false);
	}
}

void paint_live_pitch(QPainter &p, const PanelView &v)
{
	// Only the pitch and its stat strip: the score and the momentum are sources of their own.
	const bool persp = v.pitch_view == PitchView::Perspective;
	const QRectF area(0, 0, kW, 600);
	const Pitch pitch = draw_pitch(p, area, v.pitch_view, v.table_title);
	if (!persp) {
		contained(p, v.home.logo, QRectF(pitch.field.left() + 40, pitch.field.center().y() - 90, 180, 180), 0.09);
		contained(p, v.away.logo, QRectF(pitch.field.right() - 220, pitch.field.center().y() - 90, 180, 180), 0.09);
	}

	std::vector<const PitchMark *> order;
	for (const PitchMark &m : v.marks)
		order.push_back(&m);
	// Far plays first in perspective, then older before newer.
	std::stable_sort(order.begin(), order.end(), [persp](auto *a, auto *b) {
		if (a->recency == 0 || b->recency == 0)
			return b->recency == 0 && a->recency != 0;
		if (persp && std::abs(a->at.y - b->at.y) > 1e-9)
			return a->at.y < b->at.y;
		return a->recency > b->recency;
	});
	const PitchMark *latest = nullptr;
	for (const PitchMark *m : order) {
		const QColor color = team_tone(m->home ? v.home.color : v.away.color);
		const bool recent = m->recency < 6;
		// Goals stay big and solid however old; other plays fade with age.
		const bool goal = m->kind == PlayKind::Goal;
		const qreal size = m->recency == 0 || goal ? 24 : recent ? 20 : 14;
		const qreal opacity = m->recency == 0 || goal ? 1.0 : recent ? 0.95 - m->recency * 0.08 : 0.55;
		draw_mark(p, pitch, *m, color, size, opacity);
		if (m->recency == 0)
			latest = m;
	}
	// The route first; the tag and the ring wait until the ball arrives.
	const bool in_flight = draw_ball_route(p, pitch, v);
	if (latest && in_flight) {
		// nothing more: the ball is on its way
	} else if (latest) {
		draw_latest(p, pitch, *latest, v, team_tone(latest->home ? v.home.color : v.away.color));
	} else if (!v.no_plays.isEmpty()) {
		const QRectF r(area.center().x() - 260, area.center().y() - 28, 520, 56);
		p.fillPath(rounded(r, 28), QColor(0, 0, 0, 130));
		p.setFont(font(20, QFont::DemiBold));
		text(p, r, v.no_plays, Qt::AlignCenter, Qt::white);
	}
	stat_strip(p, QRectF(0, 612, kW, 70), v.strip);
}

void paint_lineups(QPainter &p, const PanelView &v)
{
	header(p, QRectF(0, 0, kW, 84), v);
	const bool persp = v.pitch_view == PitchView::Perspective;
	const Pitch pitch = draw_pitch(p, QRectF(0, 96, kW, 620), v.pitch_view, v.table_title);
	if (!persp) {
		p.setFont(font(22, QFont::Black, 2));
		text(p, QRectF(pitch.field.left() + 10, pitch.field.top() + 6, 200, 30), v.home_formation,
		     Qt::AlignLeft | Qt::AlignVCenter, QColor(255, 255, 255, 220));
		text(p, QRectF(pitch.field.right() - 210, pitch.field.top() + 6, 200, 30), v.away_formation,
		     Qt::AlignRight | Qt::AlignVCenter, QColor(255, 255, 255, 220));
	} else {
		p.setFont(font(22, QFont::Black, 2));
		text(p, QRectF(24, 106, 200, 30), v.home_formation, Qt::AlignLeft | Qt::AlignVCenter, Qt::white);
		text(p, QRectF(kW - 224, 106, 200, 30), v.away_formation, Qt::AlignRight | Qt::AlignVCenter, Qt::white);
	}
	std::vector<const LineupMark *> order;
	for (const LineupMark &m : v.players)
		order.push_back(&m);
	std::stable_sort(order.begin(), order.end(), [](auto *a, auto *b) { return a->at.y < b->at.y; });
	for (const LineupMark *mp : order) {
		const LineupMark &m = *mp;
		const QPointF at = pitch.map(m.at);
		const qreal s = pitch.scale(m.at);
		const QColor color = team_tone(m.home ? v.home.color : v.away.color);
		const qreal r = 20 * s;
		p.save();
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 80));
		p.drawEllipse(at + QPointF(0, r * 0.5), r, r * 0.45);
		p.setPen(QPen(Qt::white, 2.5));
		p.setBrush(color);
		p.drawEllipse(at, r, r);
		p.restore();
		p.setFont(font(17 * s, QFont::Black));
		text(p, QRectF(at.x() - r, at.y() - r, 2 * r, 2 * r), m.jersey, Qt::AlignCenter, Qt::white, false);
		if (m.subbed_out) {
			p.save();
			p.setPen(Qt::NoPen);
			p.setBrush(kLiveRed);
			p.drawEllipse(at + QPointF(r * 0.8, -r * 0.8), 7 * s, 7 * s);
			p.restore();
		}
		p.setFont(fit(m.name, 132, 15 * std::max<qreal>(s, 0.85), QFont::Bold));
		const QRectF label(at.x() - 66, at.y() + r + 2, 132, 22);
		const qreal tw = std::min<qreal>(QFontMetricsF(p.font()).horizontalAdvance(m.name) + 12, 132);
		p.fillPath(rounded(QRectF(at.x() - tw / 2, label.top(), tw, label.height()), 6), QColor(0, 0, 0, 150));
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

// --- new panels -------------------------------------------------------------

// A small icon for a play kind, centred on `c` (goal ball, card, swap arrows...).
void kind_icon(QPainter &p, PlayKind kind, const QPointF &c, qreal r, const QColor &team)
{
	p.save();
	switch (kind) {
	case PlayKind::Goal: {
		p.setPen(QPen(kInk, 1.4));
		p.setBrush(Qt::white);
		p.drawEllipse(c, r, r);
		p.setPen(Qt::NoPen);
		p.setBrush(kInk);
		QPolygonF pent;
		for (int i = 0; i < 5; ++i) {
			const qreal a = -M_PI / 2 + i * 2 * M_PI / 5;
			pent << c + QPointF(std::cos(a), std::sin(a)) * r * 0.45;
		}
		p.drawPolygon(pent);
		break;
	}
	case PlayKind::YellowCard:
	case PlayKind::RedCard:
		p.setPen(Qt::NoPen);
		p.setBrush(kind == PlayKind::YellowCard ? QColor(250, 204, 21) : QColor(225, 29, 46));
		p.drawRoundedRect(QRectF(c.x() - r * 0.62, c.y() - r * 0.85, r * 1.24, r * 1.7), 2, 2);
		break;
	case PlayKind::Substitution:
		p.setPen(QPen(QColor(34, 197, 94), 2.6, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(c + QPointF(-r * 0.45, r * 0.7), c + QPointF(-r * 0.45, -r * 0.7));
		p.drawLine(c + QPointF(-r * 0.45, -r * 0.7), c + QPointF(-r * 0.85, -r * 0.25));
		p.setPen(QPen(QColor(239, 68, 68), 2.6, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(c + QPointF(r * 0.45, -r * 0.7), c + QPointF(r * 0.45, r * 0.7));
		p.drawLine(c + QPointF(r * 0.45, r * 0.7), c + QPointF(r * 0.85, r * 0.25));
		break;
	case PlayKind::ShotOnTarget:
	case PlayKind::ShotOffTarget:
	case PlayKind::ShotBlocked:
	case PlayKind::Woodwork:
		p.setPen(QPen(Qt::white, 2));
		p.setBrush(kind == PlayKind::ShotOnTarget ? team : alpha(kInk, 160));
		p.drawEllipse(c, r * 0.7, r * 0.7);
		break;
	case PlayKind::Corner:
		p.setPen(QPen(Qt::white, 2));
		p.drawLine(c + QPointF(-r * 0.3, r * 0.8), c + QPointF(-r * 0.3, -r * 0.8));
		p.setBrush(team.lighter(130));
		p.drawPolygon(QPolygonF({c + QPointF(-r * 0.3, -r * 0.8), c + QPointF(r * 0.7, -r * 0.45),
					 c + QPointF(-r * 0.3, -r * 0.1)}));
		break;
	case PlayKind::Foul:
		p.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(c + QPointF(-r * 0.55, -r * 0.55), c + QPointF(r * 0.55, r * 0.55));
		p.drawLine(c + QPointF(-r * 0.55, r * 0.55), c + QPointF(r * 0.55, -r * 0.55));
		break;
	case PlayKind::Offside:
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(250, 204, 21));
		p.drawRect(QRectF(c.x() - r * 0.55, c.y() - r * 0.55, r * 1.1, r * 1.1));
		break;
	default:
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 120));
		p.drawEllipse(c, r * 0.3, r * 0.3);
		break;
	}
	p.restore();
}

// Live commentary: the newest line on top, larger; older lines fade.
void paint_feed(QPainter &p, const PanelView &v)
{
	const QSize size = panel_natural_size(PanelContent::Feed, v.rows ? v.rows : v.feed.size());
	const QRectF body(0, 0, size.width(), size.height());
	p.fillPath(rounded(body, 16), kPanel);
	// Title row with both crests and the score.
	contained(p, v.home.logo, QRectF(18, 12, 34, 34));
	contained(p, v.away.logo, QRectF(body.right() - 52, 12, 34, 34));
	p.setFont(font(20, QFont::Black, 1));
	const QString title = v.home.score.isEmpty() ? v.feed_title
						     : v.home.abbreviation + QStringLiteral("  ") + v.home.score +
							       QStringLiteral(" - ") + v.away.score + QStringLiteral("  ") +
							       v.away.abbreviation;
	text(p, QRectF(60, 8, body.width() - 120, 42), title, Qt::AlignCenter, Qt::white);
	live_pill(p, QPointF(body.center().x(), 58), 18, v);
	const qreal top = 74, row_h = 78;
	for (size_t i = 0; i < v.feed.size(); ++i) {
		const FeedLine &line = v.feed[i];
		const QRectF row(12, top + row_h * i, body.width() - 24, row_h - 6);
		const QColor team = line.side > 0 ? team_tone(v.home.color) : line.side < 0 ? team_tone(v.away.color)
											      : QColor(90, 96, 112);
		p.save();
		p.setOpacity(i == 0 ? 1.0 : std::max(0.45, 0.92 - 0.09 * i));
		p.fillPath(rounded(row, 10), i == 0 ? QColor(255, 255, 255, 26) : QColor(255, 255, 255, 10));
		p.fillRect(QRectF(row.left(), row.top() + 8, 4, row.height() - 16), team);
		kind_icon(p, line.kind, QPointF(row.left() + 40, row.top() + 24), 10, team);
		p.setFont(fit(line.minute, 62, 15, QFont::Black));
		text(p, QRectF(row.left() + 9, row.top() + 40, 62, 22), line.minute, Qt::AlignCenter,
		     QColor(255, 255, 255, 200), false);
		QFont body_font = font(i == 0 ? 17 : 15, i == 0 ? QFont::DemiBold : QFont::Medium);
		p.setFont(body_font);
		const QRectF text_rect(row.left() + 80, row.top() + 4, row.width() - 90, row.height() - 8);
		p.setPen(Qt::white);
		p.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap,
			   QFontMetricsF(body_font).elidedText(line.text, Qt::ElideRight, text_rect.width() * 2.6));
		p.restore();
	}
}

void paint_momentum(QPainter &p, const PanelView &v)
{
	// Just the chart: crests on the left, a minute scale along the bottom.
	const QRectF r(0, 0, kW, 140);
	draw_momentum(p, r, v, true, 20);
	p.setFont(font(12, QFont::Bold));
	const QRectF plot = r.adjusted(54, 8, -54, -28);
	for (const int minute : {0, 15, 30, 45, 60, 75, 90}) {
		const size_t n = std::max<size_t>(v.momentum.size(), 91);
		const qreal x = plot.left() + plot.width() * minute / n;
		text(p, QRectF(x - 20, r.bottom() - 22, 40, 16), QString::number(minute) + QStringLiteral("'"),
		     Qt::AlignCenter, QColor(255, 255, 255, 110), false);
	}
}

// Vertical timeline: minute in the middle, home events to the left, away to the right.
void paint_timeline(QPainter &p, const PanelView &v)
{
	const QSize size = panel_natural_size(PanelContent::Timeline, v.rows ? v.rows : v.timeline.size());
	const QRectF body(0, 0, size.width(), size.height());
	p.fillPath(rounded(body, 16), kPanel);
	contained(p, v.home.logo, QRectF(18, 12, 40, 40));
	contained(p, v.away.logo, QRectF(body.right() - 58, 12, 40, 40));
	p.setFont(font(26, QFont::Black));
	text(p, QRectF(70, 8, body.width() - 140, 36),
	     v.home.score.isEmpty() ? QStringLiteral("vs") : v.home.score + QStringLiteral(" - ") + v.away.score,
	     Qt::AlignCenter, Qt::white);
	live_pill(p, QPointF(body.center().x(), 56), 18, v);
	const qreal cx = body.center().x();
	const qreal top = 84, row_h = 58;
	p.fillRect(QRectF(cx - 1, top, 2, row_h * std::max<size_t>(v.timeline.size(), 1)), QColor(255, 255, 255, 50));
	for (size_t i = 0; i < v.timeline.size(); ++i) {
		const TimelineEvent &e = v.timeline[i];
		const qreal y = top + row_h * i + row_h / 2;
		const QColor team = team_tone(e.home ? v.home.color : v.away.color);
		// Minute bubble on the line.
		const QRectF bubble(cx - 26, y - 14, 52, 28);
		p.fillPath(rounded(bubble, 14), QColor(40, 44, 58));
		p.setFont(font(15, QFont::Black));
		text(p, bubble, e.minute, Qt::AlignCenter, Qt::white, false);
		// Icon next to the bubble, then the player and the detail.
		const qreal dir = e.home ? -1 : 1;
		kind_icon(p, e.kind, QPointF(cx + dir * 46, y), 11, team);
		const QRectF name_rect = e.home ? QRectF(16, y - 22, cx - 76 - 16, 24) : QRectF(cx + 64, y - 22, cx - 80, 24);
		const QRectF detail_rect = name_rect.translated(0, 22);
		const int align = (e.home ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
		p.setFont(fit(e.player, name_rect.width(), 18, QFont::Bold));
		text(p, name_rect, e.player, align, e.kind == PlayKind::Substitution ? QColor(134, 239, 172) : QColor(Qt::white),
		     false);
		p.setFont(font(14, QFont::Medium));
		text(p, detail_rect, e.detail, align, QColor(255, 255, 255, 170), false);
	}
}

void form_row(QPainter &p, const QRectF &r, const QImage &logo, const QString &name, const std::vector<FormCell> &cells,
	      bool logos_right)
{
	contained(p, logo, QRectF(logos_right ? r.right() - 44 : r.left(), r.top() + 4, 44, r.height() - 8));
	p.setFont(fit(name.toUpper(), 210, 20, QFont::Black, 1));
	text(p, QRectF(logos_right ? r.right() - 270 : r.left() + 54, r.top(), 210, r.height()), name.toUpper(),
	     (logos_right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, Qt::white, false);
	const qreal cell = 58, gap = 8;
	const qreal start = logos_right ? r.left() : r.right() - (cell + gap) * cells.size();
	for (size_t i = 0; i < cells.size(); ++i) {
		const FormCell &c = cells[i];
		const QRectF box(start + (cell + gap) * i, r.top() + 4, cell, r.height() - 8);
		const QColor fill = c.outcome > 0 ? QColor(22, 163, 74) : c.outcome < 0 ? QColor(220, 38, 38) : QColor(100, 108, 124);
		p.fillPath(rounded(box, 8), fill);
		p.setFont(font(20, QFont::Black));
		text(p, QRectF(box.left(), box.top() + 2, box.width(), box.height() * 0.5), QString(c.result), Qt::AlignCenter,
		     Qt::white, false);
		p.setFont(font(12, QFont::Bold));
		text(p, QRectF(box.left(), box.top() + box.height() * 0.5, box.width(), box.height() * 0.45),
		     c.opponent + QStringLiteral(" ") + c.score, Qt::AlignCenter, QColor(255, 255, 255, 220), false);
	}
}

void paint_preview(QPainter &p, const PanelView &v)
{
	header(p, QRectF(0, 0, kW, 84), v);
	const QRectF body(0, 92, kW, 468);
	p.fillPath(rounded(body, 16), kPanel);
	p.setFont(font(15, QFont::Black, 2));
	text(p, QRectF(24, body.top() + 10, 600, 28), v.form_title.toUpper(), Qt::AlignLeft | Qt::AlignVCenter,
	     QColor(255, 255, 255, 150), false);
	form_row(p, QRectF(24, body.top() + 44, kW - 48, 64), v.home.logo, v.home.name, v.home_form, false);
	form_row(p, QRectF(24, body.top() + 120, kW - 48, 64), v.away.logo, v.away.name, v.away_form, false);

	p.fillRect(QRectF(24, body.top() + 200, kW - 48, 1), QColor(255, 255, 255, 30));
	p.setFont(font(15, QFont::Black, 2));
	text(p, QRectF(24, body.top() + 212, 600, 28), v.meetings_title.toUpper(), Qt::AlignLeft | Qt::AlignVCenter,
	     QColor(255, 255, 255, 150), false);
	p.setFont(font(22, QFont::Black));
	text(p, QRectF(24, body.top() + 242, kW - 48, 34), v.meetings_summary, Qt::AlignLeft | Qt::AlignVCenter, Qt::white,
	     false);
	p.setFont(font(17, QFont::Medium));
	for (size_t i = 0; i < std::min<size_t>(v.meetings.size(), 5); ++i) {
		const QRectF row(24 + (i % 2) * (kW - 48) / 2, body.top() + 284 + (i / 2) * 34, (kW - 48) / 2 - 10, 30);
		text(p, row, v.meetings[i], Qt::AlignLeft | Qt::AlignVCenter, QColor(255, 255, 255, 210), false);
	}
	p.fillRect(QRectF(24, body.bottom() - 60, kW - 48, 1), QColor(255, 255, 255, 30));
	p.setFont(font(18, QFont::DemiBold));
	text(p, QRectF(24, body.bottom() - 54, kW - 48, 46), v.venue_line, Qt::AlignLeft | Qt::AlignVCenter,
	     QColor(255, 255, 255, 210), false);
}

void paint_scorers(QPainter &p, const PanelView &v)
{
	const QSize size = panel_natural_size(PanelContent::Scorers, v.rows ? v.rows : v.scorers.size());
	const QRectF body(0, 0, size.width(), size.height());
	p.fillPath(rounded(body, 16), kPanel);
	p.setFont(fit(v.scorers_title.toUpper(), body.width() * 0.62, 22, QFont::Black, 1.5));
	text(p, QRectF(24, 8, body.width() * 0.62, 48), v.scorers_title.toUpper(), Qt::AlignLeft | Qt::AlignVCenter,
	     Qt::white);
	p.setFont(font(15, QFont::Bold, 1));
	text(p, QRectF(body.width() - 170, 8, 70, 48), v.scorers_matches_header, Qt::AlignCenter, QColor(255, 255, 255, 150),
	     false);
	text(p, QRectF(body.width() - 90, 8, 70, 48), v.scorers_value_header, Qt::AlignCenter, QColor(255, 255, 255, 150),
	     false);
	const qreal row_h = 50, top = 64;
	for (size_t i = 0; i < v.scorers.size(); ++i) {
		const LeaderRow &r = v.scorers[i];
		const QRectF row(10, top + row_h * i, body.width() - 20, row_h);
		if (r.highlighted)
			p.fillPath(rounded(row.adjusted(0, 2, 0, -2), 8), QColor(255, 255, 255, 34));
		else if (i % 2)
			p.fillRect(row.adjusted(0, 2, 0, -2), QColor(255, 255, 255, 8));
		p.setFont(font(18, QFont::Bold));
		text(p, QRectF(row.left() + 6, row.top(), 34, row.height()), r.rank, Qt::AlignCenter, Qt::white, false);
		contained(p, r.logo, QRectF(row.left() + 48, row.top() + 8, row.height() - 16, row.height() - 16));
		p.setFont(fit(r.name, body.width() - 300, 19, QFont::DemiBold));
		text(p, QRectF(row.left() + 56 + row.height(), row.top(), body.width() - 290 - row.height(), row.height()),
		     r.name, Qt::AlignVCenter | Qt::AlignLeft, Qt::white, false);
		p.setFont(font(17, QFont::Medium));
		text(p, QRectF(body.width() - 170, row.top(), 70, row.height()), r.matches, Qt::AlignCenter,
		     QColor(255, 255, 255, 190), false);
		p.setFont(font(22, QFont::Black));
		text(p, QRectF(body.width() - 90, row.top(), 70, row.height()), r.value, Qt::AlignCenter, Qt::white, false);
	}
}

void paint_round(QPainter &p, const PanelView &v)
{
	const QSize size = panel_natural_size(PanelContent::Round, v.rows ? v.rows : v.round.size());
	const QRectF body(0, 0, size.width(), size.height());
	p.fillPath(rounded(body, 16), kPanel);
	p.setFont(fit(v.round_title.toUpper(), body.width() - 48, 22, QFont::Black, 1.5));
	text(p, QRectF(24, 8, body.width() - 48, 48), v.round_title.toUpper(), Qt::AlignLeft | Qt::AlignVCenter, Qt::white);
	const qreal row_h = 60, top = 64;
	for (size_t i = 0; i < v.round.size(); ++i) {
		const RoundRow &r = v.round[i];
		const QRectF row(10, top + row_h * i, body.width() - 20, row_h - 6);
		p.fillPath(rounded(row, 10), r.highlighted ? QColor(255, 255, 255, 34) : QColor(255, 255, 255, 10));
		const qreal cx = row.center().x();
		const QRectF score(cx - 52, row.top() + 6, 104, row.height() * 0.6);
		p.fillPath(rounded(score, 8), r.live ? QColor(225, 29, 46, 220) : QColor(40, 44, 58));
		p.setFont(font(20, QFont::Black));
		text(p, score, r.center, Qt::AlignCenter, Qt::white, false);
		p.setFont(font(12, QFont::Bold, 0.5));
		text(p, QRectF(score.left() - 10, score.bottom(), score.width() + 20, row.bottom() - score.bottom()), r.status,
		     Qt::AlignCenter, r.live ? QColor(252, 165, 165) : QColor(255, 255, 255, 160), false);
		contained(p, r.home_logo, QRectF(score.left() - 46, row.top() + 10, 34, row.height() - 20));
		contained(p, r.away_logo, QRectF(score.right() + 12, row.top() + 10, 34, row.height() - 20));
		p.setFont(fit(r.home, score.left() - 60 - row.left() - 10, 18, QFont::DemiBold));
		text(p, QRectF(row.left() + 10, row.top(), score.left() - 60 - row.left() - 10, row.height()), r.home,
		     Qt::AlignVCenter | Qt::AlignRight, Qt::white, false);
		p.setFont(fit(r.away, row.right() - 10 - (score.right() + 58), 18, QFont::DemiBold));
		text(p, QRectF(score.right() + 58, row.top(), row.right() - 10 - (score.right() + 58), row.height()), r.away,
		     Qt::AlignVCenter | Qt::AlignLeft, Qt::white, false);
	}
}

} // namespace

QSize panel_natural_size(PanelContent content, size_t rows)
{
	rows = std::max<size_t>(rows, 1);
	switch (content) {
	case PanelContent::LivePitch:
		return {1000, 682};
	case PanelContent::Lineups:
		return {1000, 716};
	case PanelContent::Stats:
		return {1000, 96 + 64 * 8 + 24};
	case PanelContent::Table:
		return {620, static_cast<int>(64 + 46 * rows + 14)};
	case PanelContent::Feed:
		return {560, static_cast<int>(74 + 78 * rows + 8)};
	case PanelContent::Momentum:
		return {1000, 140};
	case PanelContent::Timeline:
		return {620, static_cast<int>(84 + 58 * rows + 14)};
	case PanelContent::Preview:
		return {1000, 560};
	case PanelContent::Scorers:
		return {620, static_cast<int>(64 + 50 * rows + 14)};
	case PanelContent::Round:
		return {620, static_cast<int>(64 + 60 * rows + 8)};
	}
	return {1000, 846};
}

QImage paint_panel(const PanelView &view, QSize size)
{
	QImage image(size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	if (size.isEmpty())
		return image;
	size_t rows = view.table.size();
	switch (view.content) {
	case PanelContent::Feed:
		rows = view.feed.size();
		break;
	case PanelContent::Timeline:
		rows = view.timeline.size();
		break;
	case PanelContent::Scorers:
		rows = view.scorers.size();
		break;
	case PanelContent::Round:
		rows = view.round.size();
		break;
	default:
		break;
	}
	if (view.rows > 0)
		rows = view.rows;
	const QSize natural = panel_natural_size(view.content, rows);
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
	case PanelContent::Feed:
		paint_feed(p, view);
		break;
	case PanelContent::Momentum:
		paint_momentum(p, view);
		break;
	case PanelContent::Timeline:
		paint_timeline(p, view);
		break;
	case PanelContent::Preview:
		paint_preview(p, view);
		break;
	case PanelContent::Scorers:
		paint_scorers(p, view);
		break;
	case PanelContent::Round:
		paint_round(p, view);
		break;
	}
	return image;
}

} // namespace fscore::render
