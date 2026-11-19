// The large-crest designs: Stadium, Clash, Card, Floating and Clean. Each one is
// drawn at its natural size (core/board_design) and scaled to the source size.

#include "render/scoreboard_painter.hpp"

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

constexpr qreal kPi = 3.14159265358979323846;
const QColor kLiveRed(225, 29, 46);
const QColor kInk(14, 15, 22);

// --- helpers ---------------------------------------------------------------

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

// The largest font up to `max_px` at which `text` fits `width` (never below 40 %).
QFont fit(const QString &text, qreal width, qreal max_px, int weight, qreal spacing = 0)
{
	QFont f = font(max_px, weight, spacing);
	const qreal advance = QFontMetricsF(f).horizontalAdvance(text);
	if (width > 0 && advance > width)
		f = font(std::max(max_px * 0.4, max_px * width / advance * 0.97), weight, spacing * width / advance);
	return f;
}

QColor alpha(QColor color, int a)
{
	color.setAlpha(std::clamp(a, 0, 255));
	return color;
}

// Team color, or a neutral fallback when the feed has none.
QColor team(const SideView &side, const QColor &fallback)
{
	return side.color.isValid() ? side.color : fallback;
}

// Dark enough for white text on top.
QColor readable(QColor color)
{
	for (int i = 0; i < 8 && color.lightnessF() > 0.45; ++i)
		color = color.darker(118);
	return color;
}

void contained(QPainter &p, const QImage &image, const QRectF &rect, qreal opacity = 1.0)
{
	if (image.isNull() || rect.isEmpty())
		return;
	const QSizeF s = QSizeF(image.size()).scaled(rect.size(), Qt::KeepAspectRatio);
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.drawImage(QRectF(rect.center().x() - s.width() / 2, rect.center().y() - s.height() / 2, s.width(), s.height()),
		    image);
	p.restore();
}

// Crest with a soft drop shadow made from its own silhouette.
void crest(QPainter &p, const QImage &image, const QRectF &rect, qreal blur, qreal drop)
{
	if (image.isNull())
		return;
	QImage shadow(image.size(), QImage::Format_ARGB32_Premultiplied);
	shadow.fill(Qt::transparent);
	{
		QPainter sp(&shadow);
		sp.drawImage(0, 0, image);
		sp.setCompositionMode(QPainter::CompositionMode_SourceIn);
		sp.fillRect(shadow.rect(), Qt::black);
	}
	for (int i = 0; i < 6; ++i) {
		const qreal a = 2.0 * kPi * i / 6;
		contained(p, shadow, rect.translated(std::cos(a) * blur, drop + std::sin(a) * blur), 0.10);
	}
	contained(p, shadow, rect.translated(0, drop), 0.25);
	contained(p, image, rect);
}

void text(QPainter &p, const QRectF &rect, const QString &value, int align, const QColor &color, qreal shadow = 2.0)
{
	if (rect.width() <= 0)
		return;
	const QString shown = QFontMetricsF(p.font()).elidedText(value, Qt::ElideRight, rect.width());
	if (shadow > 0) {
		p.setPen(QColor(0, 0, 0, 120 * color.alpha() / 255));
		p.drawText(rect.translated(0, shadow), align, shown);
	}
	p.setPen(color);
	p.drawText(rect, align, shown);
}

void round_flag(QPainter &p, const QImage &flag, const QRectF &rect)
{
	if (flag.isNull())
		return;
	QPainterPath circle;
	circle.addEllipse(rect);
	p.save();
	p.setClipPath(circle);
	const QSizeF s = QSizeF(flag.size()).scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
	p.drawImage(QRectF(rect.center().x() - s.width() / 2, rect.center().y() - s.height() / 2, s.width(), s.height()),
		    flag);
	p.restore();
	p.save();
	p.setPen(QPen(QColor(255, 255, 255, 200), std::max<qreal>(1.0, rect.width() * 0.07)));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(rect);
	p.restore();
}

// Status pill ("● 67'" in red while live, dark otherwise), centered on `center`.
QRectF status_pill(QPainter &p, QPointF center, qreal height, const ScoreView &view, const QColor &idle)
{
	p.setFont(font(height * 0.54, QFont::Bold, 0.5));
	const qreal text_w = QFontMetricsF(p.font()).horizontalAdvance(view.status);
	const qreal dot = view.live ? height * 0.30 : 0;
	const qreal gap = view.live ? dot * 0.8 : 0;
	const qreal width = text_w + dot + gap + height * 0.9;
	const QRectF pill(center.x() - width / 2, center.y() - height / 2, width, height);
	QPainterPath path;
	path.addRoundedRect(pill, height / 2, height / 2);
	p.fillPath(path, view.live ? kLiveRed : idle);
	const qreal x = pill.center().x() - (text_w + dot + gap) / 2;
	if (view.live) {
		p.save();
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, static_cast<int>(255 * view.blink)));
		p.drawEllipse(QRectF(x, pill.center().y() - dot / 2, dot, dot));
		p.restore();
	}
	text(p, QRectF(x + dot + gap, pill.top(), text_w + 2, height), view.status, Qt::AlignVCenter | Qt::AlignLeft,
	     Qt::white, 0);
	return pill;
}

// The score as two numbers around a dash, or the kick-off time before the match.
void score(QPainter &p, const QRectF &rect, const ScoreView &view, qreal px, const QColor &color, qreal gap)
{
	if (!view.has_score) {
		p.setFont(fit(view.center, rect.width() * 0.92, px * 0.72, QFont::Black));
		text(p, rect, view.center, Qt::AlignCenter, color);
		return;
	}
	p.setFont(fit(view.score_home + view.score_away, rect.width() - gap * 2, px, QFont::Black, -1));
	const qreal cx = rect.center().x();
	text(p, QRectF(rect.left(), rect.top(), cx - gap - rect.left(), rect.height()), view.score_home,
	     Qt::AlignVCenter | Qt::AlignRight, color);
	text(p, QRectF(cx + gap, rect.top(), rect.right() - cx - gap, rect.height()), view.score_away,
	     Qt::AlignVCenter | Qt::AlignLeft, color);
	const qreal dash_w = gap * 0.9;
	p.fillRect(QRectF(cx - dash_w / 2, rect.center().y() - px * 0.03, dash_w, px * 0.07), alpha(color, 150));
}

// How strongly the goal state shows right now: 0 outside, 1 while fully up.
qreal goal_level(const ScoreView &view)
{
	return view.goal.active ? view.goal.banner : 0.0;
}

// Crest bounce while celebrating (a few px up and slightly bigger).
QRectF bounced(const QRectF &rect, const ScoreView &view, bool scoring)
{
	if (!scoring || !view.goal.active)
		return rect;
	const qreal grow = rect.width() * 0.08 * view.goal.banner + rect.width() * 0.04 * view.goal.pulse;
	return rect.adjusted(-grow / 2, -grow, grow / 2, 0);
}

QString goal_line(const ScoreView &view, const SideView &side)
{
	return view.goal_detail.isEmpty() ? side.name : view.goal_detail;
}

void flash(QPainter &p, const QPainterPath &shape, const ScoreView &view)
{
	if (view.goal.flash > 0)
		p.fillPath(shape, QColor(255, 255, 255, static_cast<int>(view.goal.flash * 150)));
}

// Message shown alone (loading, no matches...), in a pill the design can afford.
void message(QPainter &p, QSizeF size, const ScoreView &view, const BoardStyle &style, bool glass)
{
	const qreal h = std::min<qreal>(size.height() * 0.42, 90);
	p.setFont(font(h * 0.38, QFont::DemiBold));
	const qreal w = std::min(size.width() - 20, QFontMetricsF(p.font()).horizontalAdvance(view.message) + h * 1.6);
	const QRectF pill(size.width() / 2 - w / 2, size.height() / 2 - h / 2, w, h);
	QPainterPath path;
	path.addRoundedRect(pill, h / 2, h / 2);
	p.fillPath(path, glass ? QColor(0, 0, 0, 130) : alpha(kInk, 235));
	contained(p, view.league_logo, QRectF(pill.left() + h * 0.2, pill.top() + h * 0.18, h * 0.64, h * 0.64));
	text(p, pill.adjusted(h * 0.9, 0, -h * 0.4, 0), view.message, Qt::AlignCenter, style.text);
}

// --- A · Stadium (1280 x 220) ---------------------------------------------

void paint_stadium(QPainter &p, const ScoreView &v, const BoardStyle &style)
{
	const qreal W = 1280;
	const QRectF bar(20, 70, W - 40, 130);
	const qreal slant = 34, cx = W / 2, box_w = 250;
	const qreal g = goal_level(v);

	QPainterPath base;
	base.addRoundedRect(bar, 18, 18);
	p.fillPath(base.translated(0, 6), QColor(0, 0, 0, 90));

	auto panel = [&](bool home) {
		QPainterPath path;
		const qreal edge = home ? bar.left() : bar.right();
		const qreal inner = home ? cx - box_w / 2 : cx + box_w / 2;
		path.moveTo(edge, bar.top());
		path.lineTo(inner + slant, bar.top());
		path.lineTo(inner - slant, bar.bottom());
		path.lineTo(edge, bar.bottom());
		path.closeSubpath();
		return path;
	};
	const QPainterPath home_panel = panel(true), away_panel = panel(false);

	p.save();
	p.setClipPath(base);
	p.fillRect(bar, kInk);
	for (const bool home : {true, false}) {
		const SideView &side = home ? v.home : v.away;
		const QColor color = team(side, style.accent);
		const bool scoring = g > 0 && v.goal_home == home;
		QLinearGradient gradient(home ? bar.topLeft() : bar.topRight(), QPointF(cx, bar.top()));
		gradient.setColorAt(0, readable(color).darker(scoring ? 100 : 200));
		gradient.setColorAt(1, readable(color).darker(scoring ? 90 : 110));
		p.fillPath(home ? home_panel : away_panel, gradient);
		contained(p, side.logo,
			  home ? QRectF(bar.left() + 250, bar.top() - 40, 260, 260)
			       : QRectF(bar.right() - 510, bar.top() - 40, 260, 260),
			  0.10);
	}
	QLinearGradient gloss(bar.topLeft(), bar.bottomLeft());
	gloss.setColorAt(0, QColor(255, 255, 255, 40));
	gloss.setColorAt(0.5, QColor(255, 255, 255, 0));
	p.fillRect(bar, gloss);
	flash(p, base, v);
	p.restore();

	// Score block.
	QPainterPath box;
	box.moveTo(cx - box_w / 2 + slant, bar.top() - 14);
	box.lineTo(cx + box_w / 2 + slant, bar.top() - 14);
	box.lineTo(cx + box_w / 2 - slant, bar.bottom());
	box.lineTo(cx - box_w / 2 - slant, bar.bottom());
	box.closeSubpath();
	p.fillPath(box.translated(0, 5), QColor(0, 0, 0, 110));
	QLinearGradient box_fill(QPointF(cx, bar.top()), QPointF(cx, bar.bottom()));
	box_fill.setColorAt(0, QColor(36, 38, 50));
	box_fill.setColorAt(1, QColor(8, 9, 14));
	p.fillPath(box, box_fill);
	if (v.goal.active && v.goal.pulse > 0) {
		p.save();
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(alpha(style.accent, static_cast<int>(200 * v.goal.pulse)), 6));
		p.drawPath(box);
		p.restore();
	}
	p.save();
	p.setPen(QPen(QColor(255, 255, 255, 60), 2));
	p.setBrush(Qt::NoBrush);
	p.drawPath(box);
	p.restore();
	score(p, QRectF(cx - box_w / 2 + 10, bar.top() - 8, box_w - 20, 100), v, 84, style.text, 22);
	status_pill(p, QPointF(cx, bar.bottom() - 19), 30, v, QColor(40, 42, 54));

	// League tab.
	p.setFont(font(18, QFont::ExtraBold, 2));
	const QString league = v.league_name.toUpper();
	const qreal league_w = std::min<qreal>(QFontMetricsF(p.font()).horizontalAdvance(league), 300);
	const qreal tab_w = 46 + league_w + (v.league_flag.isNull() ? 14 : 48);
	const QRectF tab(cx - tab_w / 2, 18, tab_w, 40);
	QPainterPath tab_path;
	tab_path.addRoundedRect(tab, 12, 12);
	p.fillPath(tab_path, alpha(kInk, 235));
	contained(p, v.league_logo, QRectF(tab.left() + 10, tab.top() + 5, 30, 30));
	text(p, QRectF(tab.left() + 46, tab.top(), league_w + 2, tab.height()), league, Qt::AlignVCenter | Qt::AlignLeft,
	     Qt::white, 0);
	round_flag(p, v.league_flag, QRectF(tab.right() - 38, tab.top() + 8, 24, 24));

	// Crests and names.
	const qreal size = 190;
	const QRectF home_crest(bar.left() + 30, bar.bottom() - size + 18, size, size);
	const QRectF away_crest(bar.right() - 30 - size, bar.bottom() - size + 18, size, size);
	crest(p, v.home.logo, bounced(home_crest, v, v.goal_home), 5, 6);
	crest(p, v.away.logo, bounced(away_crest, v, !v.goal_home), 5, 6);

	const QRectF home_name(home_crest.right() + 18, bar.top(), cx - box_w / 2 - slant - home_crest.right() - 40,
			       bar.height());
	const QRectF away_name(cx + box_w / 2 + slant + 22, bar.top(), away_crest.left() - 18 - (cx + box_w / 2 + slant + 22),
			       bar.height());
	for (const bool home : {true, false}) {
		const SideView &side = home ? v.home : v.away;
		const QRectF rect = home ? home_name : away_name;
		const int align = Qt::AlignVCenter | (home ? Qt::AlignLeft : Qt::AlignRight);
		if (g > 0 && v.goal_home == home) {
			p.save();
			p.setOpacity(v.goal.text);
			p.setFont(fit(v.goal_title, rect.width(), 76, QFont::Black, 2));
			text(p, QRectF(rect.left(), rect.top() - 6, rect.width(), 92), v.goal_title,
			     (home ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, Qt::white);
			const QString line = goal_line(v, side);
			p.setFont(fit(line, rect.width(), 24, QFont::Bold));
			text(p, QRectF(rect.left(), rect.top() + 80, rect.width(), 40), line,
			     (home ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, Qt::white);
			p.restore();
			p.save();
			p.setOpacity(1.0 - v.goal.text);
		} else {
			p.save();
		}
		const QString name = side.name.toUpper();
		p.setFont(fit(name, rect.width(), 40, QFont::Black, 1));
		text(p, rect, name, align, style.text);
		p.restore();
	}
}

// --- B · Clash (1280 x 160) -----------------------------------------------

void paint_clash(QPainter &p, const ScoreView &v, const BoardStyle &style)
{
	const qreal W = 1280, H = 160, cx = W / 2;
	const QRectF bar(10, 10, W - 20, H - 20);
	const qreal g = goal_level(v);
	QPainterPath base;
	base.addRoundedRect(bar, 22, 22);
	p.fillPath(base.translated(0, 5), QColor(0, 0, 0, 80));

	p.save();
	p.setClipPath(base);
	// White text sits on these: darken light team colors (white, sky blue, gold).
	const QColor home_color = readable(team(v.home, QColor(70, 74, 90)));
	const QColor away_color = readable(team(v.away, QColor(50, 54, 70)));
	QLinearGradient hg(bar.topLeft(), bar.bottomLeft());
	hg.setColorAt(0, home_color);
	hg.setColorAt(1, home_color.darker(170));
	p.fillRect(QRectF(bar.left(), bar.top(), cx - bar.left() + 40, bar.height()), hg);
	QPainterPath right;
	right.moveTo(cx + 40, bar.top());
	right.lineTo(bar.right(), bar.top());
	right.lineTo(bar.right(), bar.bottom());
	right.lineTo(cx - 40, bar.bottom());
	right.closeSubpath();
	QLinearGradient ag(bar.topLeft(), bar.bottomLeft());
	ag.setColorAt(0, away_color);
	ag.setColorAt(1, away_color.darker(170));
	p.fillPath(right, ag);
	contained(p, v.home.logo, QRectF(bar.left() - 70, bar.top() - 110, 360, 360), 0.22 + 0.25 * (v.goal_home ? g : 0));
	contained(p, v.away.logo, QRectF(bar.right() - 290, bar.top() - 110, 360, 360),
		  0.22 + 0.25 * (!v.goal_home ? g : 0));
	// Light team colors (gold, white) need a darker veil for white text.
	p.fillRect(bar, QColor(0, 0, 0, 40));
	QRadialGradient vignette(QPointF(cx, bar.center().y()), 330);
	vignette.setColorAt(0, QColor(0, 0, 0, 170));
	vignette.setColorAt(1, QColor(0, 0, 0, 0));
	p.fillRect(bar, vignette);
	flash(p, base, v);
	p.restore();

	const qreal size = 118;
	const QRectF home_crest(cx - 200 - size, bar.center().y() - size / 2, size, size);
	const QRectF away_crest(cx + 200, bar.center().y() - size / 2, size, size);
	crest(p, v.home.logo, bounced(home_crest, v, v.goal_home), 4, 5);
	crest(p, v.away.logo, bounced(away_crest, v, !v.goal_home), 4, 5);

	const QRectF capsule(cx - 150, bar.center().y() - 40, 300, 88);
	QPainterPath cap;
	cap.addRoundedRect(capsule, 20, 20);
	p.fillPath(cap.translated(0, 5), QColor(0, 0, 0, 90));
	p.fillPath(cap, QColor(250, 250, 252));
	if (v.goal.active && v.goal.pulse > 0) {
		p.save();
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(alpha(style.accent, static_cast<int>(220 * v.goal.pulse)), 6));
		p.drawPath(cap);
		p.restore();
	}
	score(p, capsule.adjusted(10, -10, -10, -10), v, 70, kInk, 20);
	status_pill(p, QPointF(cx, capsule.bottom() - 3), 26, v, QColor(40, 42, 54));

	const QRectF home_text(bar.left() + 40, bar.top() + 14, home_crest.left() - bar.left() - 70, 76);
	const QRectF away_text(away_crest.right() + 30, bar.top() + 14, bar.right() - away_crest.right() - 70, 76);
	for (const bool home : {true, false}) {
		const SideView &side = home ? v.home : v.away;
		const QRectF rect = home ? home_text : away_text;
		const int horizontal = home ? Qt::AlignRight : Qt::AlignLeft;
		const bool scoring = g > 0 && v.goal_home == home;
		const QString big = scoring ? v.goal_title : (side.abbreviation.isEmpty() ? side.name : side.abbreviation);
		const QString small = scoring ? goal_line(v, side) : side.name.toUpper();
		p.setFont(fit(big, rect.width(), 66, QFont::Black, 3));
		text(p, rect, big, Qt::AlignBottom | horizontal, style.text);
		p.setFont(fit(small, rect.width(), 19, QFont::Bold, 3));
		text(p, QRectF(rect.left(), rect.bottom() + 2, rect.width(), 30), small, Qt::AlignTop | horizontal,
		     alpha(style.text, 225));
	}

	// League tab hanging from the top edge.
	p.setFont(font(13, QFont::ExtraBold, 2));
	const QString league = v.league_name.toUpper();
	const qreal league_w = std::min<qreal>(QFontMetricsF(p.font()).horizontalAdvance(league), 220);
	const qreal tab_w = 40 + league_w + (v.league_flag.isNull() ? 12 : 34);
	const QRectF tab(cx - tab_w / 2, bar.top(), tab_w, 24);
	QPainterPath tab_path;
	tab_path.addRoundedRect(tab.adjusted(0, -12, 0, 0), 10, 10);
	p.save();
	p.setClipRect(QRectF(0, bar.top(), W, H));
	p.fillPath(tab_path, alpha(kInk, 230));
	p.restore();
	contained(p, v.league_logo, QRectF(tab.left() + 10, tab.top() + 3, 18, 18));
	text(p, QRectF(tab.left() + 34, tab.top(), league_w + 2, tab.height()), league, Qt::AlignVCenter | Qt::AlignLeft,
	     Qt::white, 0);
	round_flag(p, v.league_flag, QRectF(tab.right() - 28, tab.top() + 4, 16, 16));
}

// --- C · Card (620 x 360) -------------------------------------------------

void paint_card(QPainter &p, const ScoreView &v, const BoardStyle &style)
{
	const QRectF card(14, 14, 620 - 28, 360 - 28);
	const qreal g = goal_level(v);
	QPainterPath base;
	base.addRoundedRect(card, 26, 26);
	p.fillPath(base.translated(0, 8), QColor(0, 0, 0, 100));

	p.save();
	p.setClipPath(base);
	QLinearGradient bg(card.topLeft(), card.bottomLeft());
	bg.setColorAt(0, QColor(24, 26, 36));
	bg.setColorAt(1, QColor(10, 11, 16));
	p.fillRect(card, bg);
	for (const bool home : {true, false}) {
		const QColor color = team(home ? v.home : v.away, style.accent);
		const bool scoring = g > 0 && v.goal_home == home;
		QRadialGradient glow(QPointF(home ? card.left() + 140 : card.right() - 140, card.top() + 170),
				     200 + (scoring ? 80 * g : 0));
		glow.setColorAt(0, alpha(color, scoring ? 220 : 150));
		glow.setColorAt(1, QColor(0, 0, 0, 0));
		p.fillRect(card, glow);
	}
	p.fillRect(QRectF(card.left(), card.top(), card.width(), 56), QColor(0, 0, 0, 120));
	p.fillRect(QRectF(card.left(), card.bottom() - 10, card.width() / 2, 10), team(v.home, style.accent));
	p.fillRect(QRectF(card.center().x(), card.bottom() - 10, card.width() / 2, 10), team(v.away, style.accent));
	flash(p, base, v);
	p.restore();

	// Header: league, flag, status (or GOAL while celebrating).
	contained(p, v.league_logo, QRectF(card.left() + 18, card.top() + 10, 36, 36));
	p.setFont(font(20, QFont::ExtraBold, 2));
	const QString league = v.league_name.toUpper();
	const qreal league_w = std::min<qreal>(QFontMetricsF(p.font()).horizontalAdvance(league), 300);
	text(p, QRectF(card.left() + 64, card.top(), league_w + 2, 56), league, Qt::AlignVCenter | Qt::AlignLeft,
	     Qt::white, 0);
	round_flag(p, v.league_flag, QRectF(card.left() + 64 + league_w + 12, card.top() + 16, 24, 24));
	if (g > 0) {
		ScoreView goal_pill = v;
		goal_pill.status = v.goal_title;
		goal_pill.live = false;
		p.save();
		p.setOpacity(std::max(0.2, v.goal.text));
		status_pill(p, QPointF(card.right() - 90, card.top() + 28), 32, goal_pill, style.accent);
		p.restore();
	} else {
		status_pill(p, QPointF(card.right() - 80, card.top() + 28), 30, v, QColor(52, 55, 70));
	}

	const qreal size = 150;
	const QRectF home_crest(card.left() + 140 - size / 2, card.top() + 80, size, size);
	const QRectF away_crest(card.right() - 140 - size / 2, card.top() + 80, size, size);
	crest(p, v.home.logo, bounced(home_crest, v, v.goal_home), 5, 7);
	crest(p, v.away.logo, bounced(away_crest, v, !v.goal_home), 5, 7);
	score(p, QRectF(card.center().x() - 95, home_crest.top(), 190, size), v, 86, style.text, 14);

	for (const bool home : {true, false}) {
		const SideView &side = home ? v.home : v.away;
		const QRectF crest_rect = home ? home_crest : away_crest;
		const QRectF rect(crest_rect.center().x() - 125, crest_rect.bottom() + 10, 250, 40);
		const bool scoring = g > 0 && v.goal_home == home;
		const QString label = scoring ? goal_line(v, side) : side.name.toUpper();
		p.setFont(fit(label, rect.width(), 24, QFont::ExtraBold, scoring ? 0 : 1));
		text(p, rect, label, Qt::AlignCenter, style.text);
	}
}

// --- D · Floating (900 x 340, transparent) --------------------------------

void paint_floating(QPainter &p, const ScoreView &v, const BoardStyle &style)
{
	const qreal W = 900, cx = W / 2;
	const qreal g = goal_level(v);
	const qreal size = 200;
	const QRectF home_crest(cx - 220 - size / 2, 6, size, size);
	const QRectF away_crest(cx + 220 - size / 2, 6, size, size);

	// Soft team-color halos so the crests read on any background.
	for (const bool home : {true, false}) {
		const QRectF rect = home ? home_crest : away_crest;
		const bool scoring = g > 0 && v.goal_home == home;
		QRadialGradient halo(rect.center(), size * (0.62 + 0.25 * (scoring ? g : 0)));
		halo.setColorAt(0, alpha(team(home ? v.home : v.away, style.accent), scoring ? 150 : 90));
		halo.setColorAt(1, QColor(0, 0, 0, 0));
		p.fillRect(rect.adjusted(-size * 0.4, -size * 0.2, size * 0.4, size * 0.3), halo);
	}
	crest(p, v.home.logo, bounced(home_crest, v, v.goal_home), 6, 8);
	crest(p, v.away.logo, bounced(away_crest, v, !v.goal_home), 6, 8);

	// Between the crests: league logo and the status, or GOAL.
	contained(p, v.league_logo, QRectF(cx - 34, 30, 68, 68), 0.95);
	round_flag(p, v.league_flag, QRectF(cx + 14, 76, 26, 26));
	if (g > 0) {
		p.save();
		p.setOpacity(v.goal.text);
		p.setFont(fit(v.goal_title, 230, 54, QFont::Black, 2));
		text(p, QRectF(cx - 115, 104, 230, 70), v.goal_title, Qt::AlignCenter, Qt::white, 3);
		p.restore();
	} else {
		status_pill(p, QPointF(cx, 140), 32, v, QColor(0, 0, 0, 150));
	}

	// Glass strip with names and score.
	const QRectF strip(20, 222, W - 40, 100);
	QPainterPath glass;
	glass.addRoundedRect(strip, 50, 50);
	p.fillPath(glass, QColor(8, 9, 14, 150));
	p.save();
	p.setPen(QPen(QColor(255, 255, 255, 55), 2));
	p.setBrush(Qt::NoBrush);
	p.drawPath(glass);
	p.restore();
	flash(p, glass, v);
	const QRectF score_rect(cx - 110, strip.top(), 220, strip.height());
	if (v.goal.active && v.goal.pulse > 0) {
		QRadialGradient pulse(score_rect.center(), 130);
		pulse.setColorAt(0, alpha(style.accent, static_cast<int>(150 * v.goal.pulse)));
		pulse.setColorAt(1, QColor(0, 0, 0, 0));
		p.fillPath(glass, pulse);
	}
	score(p, score_rect.adjusted(0, -2, 0, -2), v, 76, style.text, 20);
	for (const bool home : {true, false}) {
		const SideView &side = home ? v.home : v.away;
		const QRectF rect = home ? QRectF(strip.left() + 40, strip.top(), score_rect.left() - strip.left() - 60, strip.height())
					 : QRectF(score_rect.right() + 20, strip.top(), strip.right() - score_rect.right() - 60,
						  strip.height());
		const bool scoring = g > 0 && v.goal_home == home;
		const QString label = scoring ? goal_line(v, side) : side.name.toUpper();
		p.setFont(fit(label, rect.width(), 36, QFont::Black, scoring ? 0 : 1));
		text(p, rect, label, Qt::AlignCenter, style.text);
	}
}

// --- E · Clean (820 x 200, no background) ---------------------------------

void paint_clean(QPainter &p, const ScoreView &v, const BoardStyle &style)
{
	const qreal W = 820, H = 200, cx = W / 2;
	const qreal g = goal_level(v);
	const qreal size = 170;
	const QRectF home_crest(20, (H - size) / 2 - 6, size, size);
	const QRectF away_crest(W - 20 - size, (H - size) / 2 - 6, size, size);
	crest(p, v.home.logo, bounced(home_crest, v, v.goal_home), 6, 7);
	crest(p, v.away.logo, bounced(away_crest, v, !v.goal_home), 6, 7);

	// A heavy shadow under the numbers keeps them readable without a box.
	const QRectF score_rect(home_crest.right() + 20, 10, away_crest.left() - home_crest.right() - 40, 130);
	p.save();
	if (v.goal.active && v.goal.pulse > 0) {
		QRadialGradient pulse(score_rect.center(), 180);
		pulse.setColorAt(0, alpha(style.accent, static_cast<int>(120 * v.goal.pulse)));
		pulse.setColorAt(1, QColor(0, 0, 0, 0));
		p.fillRect(score_rect.adjusted(-40, -20, 40, 40), pulse);
	}
	for (int i = 1; i <= 3; ++i) {
		p.setOpacity(0.35);
		score(p, score_rect.translated(0, i * 1.5), v, 120, QColor(0, 0, 0), 30);
	}
	p.setOpacity(1.0);
	score(p, score_rect, v, 120, style.text, 30);
	p.restore();

	if (g > 0) {
		p.save();
		p.setOpacity(v.goal.text);
		const QString line = v.goal_title + QStringLiteral("  ") + goal_line(v, v.goal_home ? v.home : v.away);
		QRectF r(score_rect.left(), 146, score_rect.width(), 44);
		p.setFont(fit(line, r.width() - 20, 28, QFont::Black, 1));
		const qreal w = QFontMetricsF(p.font()).horizontalAdvance(line) + 36;
		QPainterPath pill;
		pill.addRoundedRect(QRectF(cx - w / 2, r.top(), w, r.height()), r.height() / 2, r.height() / 2);
		p.fillPath(pill, style.accent);
		text(p, QRectF(cx - w / 2, r.top(), w, r.height()), line, Qt::AlignCenter, Qt::white, 0);
		p.restore();
	} else {
		status_pill(p, QPointF(cx, 168), 34, v, QColor(0, 0, 0, 160));
	}
}

} // namespace

QImage paint_design(const ScoreView &view, const BoardStyle &style, QSize size)
{
	QImage image(size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	const auto natural = design_natural_size(style.design);
	if (size.isEmpty() || !natural)
		return image;

	QPainter p(&image);
	p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
	p.scale(qreal(size.width()) / natural->width, qreal(size.height()) / natural->height);
	const QSizeF canvas(natural->width, natural->height);

	if (!view.has_match) {
		message(p, canvas, view, style,
			style.design == BoardDesign::Floating || style.design == BoardDesign::Clean);
		return image;
	}
	switch (style.design) {
	case BoardDesign::Stadium:
		paint_stadium(p, view, style);
		break;
	case BoardDesign::Clash:
		paint_clash(p, view, style);
		break;
	case BoardDesign::Card:
		paint_card(p, view, style);
		break;
	case BoardDesign::Floating:
		paint_floating(p, view, style);
		break;
	case BoardDesign::Clean:
		paint_clean(p, view, style);
		break;
	case BoardDesign::Classic:
		break;
	}
	return image;
}

} // namespace fscore::render
