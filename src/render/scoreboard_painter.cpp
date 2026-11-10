#include "render/scoreboard_painter.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace fscore::render {
namespace {

const QColor kLiveRed(239, 68, 68);

struct Layout {
	QRectF board;   // the rounded background
	QRectF league;  // league badge panel (empty when hidden)
	QRectF content; // teams and score
	QRectF score;   // center score box
	QRectF home;    // home name + crest area
	QRectF away;    // away crest + name area
	qreal unit;     // board inner height: every size derives from it
};

Layout layout_for(const BoardStyle &style, QSizeF size, qreal score_width_hint)
{
	const qreal w = size.width();
	const qreal h = size.height();
	// Neon keeps room around the board for its glow.
	const qreal margin = style.preset == StylePreset::Neon ? std::max<qreal>(2.0, h * 0.08) : 1.0;

	Layout l;
	l.board = QRectF(margin, margin, std::max<qreal>(1.0, w - 2 * margin), std::max<qreal>(1.0, h - 2 * margin));
	l.unit = l.board.height();
	const qreal pad = l.unit * 0.10;
	QRectF inner = l.board.adjusted(pad, pad, -pad, -pad);

	if (style.show_league && inner.width() > l.unit * 4) {
		l.league = QRectF(inner.left(), inner.top(), inner.height(), inner.height());
		inner.setLeft(l.league.right() + pad * 1.5);
	}
	l.content = inner;

	const qreal score_w = std::clamp(score_width_hint, l.unit * 1.25, std::max(l.unit * 1.25, inner.width() * 0.4));
	l.score = QRectF(inner.center().x() - score_w / 2, inner.top(), score_w, inner.height());
	l.home = QRectF(inner.left(), inner.top(), std::max<qreal>(0.0, l.score.left() - pad - inner.left()),
			inner.height());
	l.away = QRectF(l.score.right() + pad, inner.top(), std::max<qreal>(0.0, inner.right() - l.score.right() - pad),
			inner.height());
	return l;
}

QFont font_with(qreal pixel_size, QFont::Weight weight)
{
	QFont font;
	font.setPixelSize(std::max(1, static_cast<int>(pixel_size)));
	font.setWeight(weight);
	font.setStyleStrategy(QFont::PreferAntialias);
	return font;
}

// The largest font up to `max_px` (down to `min_px`) at which `text` fits `width`.
QFont fitted_font(const QString &text, qreal width, qreal max_px, qreal min_px, QFont::Weight weight)
{
	QFont font = font_with(max_px, weight);
	if (width <= 0)
		return font;
	const qreal advance = QFontMetricsF(font).horizontalAdvance(text);
	if (advance <= width)
		return font;
	return font_with(std::max(min_px, max_px * width / advance * 0.98), weight);
}

QColor with_alpha(QColor color, int alpha)
{
	color.setAlpha(std::clamp(alpha, 0, 255));
	return color;
}

// Draws `image` scaled to fit inside `rect`, centered, keeping its aspect.
void draw_contained(QPainter &painter, const QImage &image, const QRectF &rect, qreal opacity = 1.0)
{
	if (image.isNull() || rect.width() <= 0 || rect.height() <= 0)
		return;
	const QSizeF scaled = QSizeF(image.size()).scaled(rect.size(), Qt::KeepAspectRatio);
	const QRectF target(rect.center().x() - scaled.width() / 2, rect.center().y() - scaled.height() / 2,
			    scaled.width(), scaled.height());
	painter.save();
	painter.setOpacity(painter.opacity() * opacity);
	painter.drawImage(target, image);
	painter.restore();
}

void draw_text(QPainter &painter, const QRectF &rect, const QString &text, Qt::Alignment align, const QColor &color,
	       const QColor *halo = nullptr)
{
	if (rect.width() <= 0)
		return;
	const QFontMetricsF metrics(painter.font());
	const QString shown = metrics.elidedText(text, Qt::ElideRight, rect.width());
	if (halo) {
		const qreal spread = std::max<qreal>(1.0, painter.font().pixelSize() * 0.06);
		painter.setPen(with_alpha(*halo, 90));
		for (const QPointF offset : {QPointF(spread, 0), QPointF(-spread, 0), QPointF(0, spread), QPointF(0, -spread)})
			painter.drawText(rect.translated(offset), align, shown);
	}
	painter.setPen(color);
	painter.drawText(rect, align, shown);
}

QPainterPath rounded(const QRectF &rect, qreal radius)
{
	QPainterPath path;
	path.addRoundedRect(rect, radius, radius);
	return path;
}

void draw_background(QPainter &painter, const Layout &l, const BoardStyle &style)
{
	const qreal radius = l.unit * 0.18;
	const QPainterPath board = rounded(l.board, radius);
	switch (style.preset) {
	case StylePreset::Minimal:
		painter.fillPath(board, QColor(12, 14, 20, 205));
		break;
	case StylePreset::Bold: {
		QLinearGradient gradient(l.board.topLeft(), l.board.bottomLeft());
		gradient.setColorAt(0.0, QColor(34, 36, 46, 245));
		gradient.setColorAt(1.0, QColor(14, 15, 22, 245));
		painter.fillPath(board, gradient);
		break;
	}
	case StylePreset::Neon: {
		painter.save();
		painter.setBrush(Qt::NoBrush);
		const qreal glow = l.unit * 0.05;
		for (int i = 4; i >= 1; --i) {
			painter.setPen(QPen(with_alpha(style.accent, 20 * (5 - i)), glow * i));
			painter.drawPath(board);
		}
		painter.restore();
		painter.fillPath(board, QColor(8, 8, 18, 215));
		painter.save();
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(style.accent.lighter(140), std::max<qreal>(1.5, l.unit * 0.025)));
		painter.drawPath(board);
		painter.restore();
		break;
	}
	}
}

// Bold: a stripe of each team's color along the outer edges.
void draw_team_stripes(QPainter &painter, const Layout &l, const ScoreView &view)
{
	const qreal radius = l.unit * 0.18;
	const qreal stripe = std::max<qreal>(3.0, l.unit * 0.07);
	painter.save();
	painter.setClipPath(rounded(l.board, radius));
	if (view.home.color.isValid()) {
		const qreal left = l.league.isEmpty() ? l.board.left() : l.league.right() + l.unit * 0.05;
		painter.fillRect(QRectF(left, l.board.bottom() - stripe, l.score.left() - left, stripe), view.home.color);
	}
	if (view.away.color.isValid())
		painter.fillRect(QRectF(l.score.right(), l.board.bottom() - stripe, l.board.right() - l.score.right(), stripe),
				 view.away.color);
	painter.restore();
}

void draw_league(QPainter &painter, const Layout &l, const ScoreView &view, const BoardStyle &style)
{
	if (l.league.isEmpty())
		return;
	const QRectF badge = l.league;
	draw_contained(painter, view.league_logo, badge.adjusted(badge.width() * 0.08, badge.height() * 0.04,
								  -badge.width() * 0.08, -badge.height() * 0.12));
	if (!view.league_flag.isNull()) {
		// Round flag on the badge's lower right corner.
		const qreal side = badge.height() * 0.38;
		const QRectF flag(badge.right() - side, badge.bottom() - side, side, side);
		painter.save();
		painter.setPen(QPen(QColor(255, 255, 255, 220), std::max<qreal>(1.0, side * 0.08)));
		painter.setBrush(Qt::NoBrush);
		QPainterPath circle;
		circle.addEllipse(flag);
		painter.setClipPath(circle);
		const QImage &image = view.league_flag;
		// Cover the circle: fill it completely, cropping the flag's sides.
		const QSizeF cover = QSizeF(image.size()).scaled(flag.size(), Qt::KeepAspectRatioByExpanding);
		painter.drawImage(QRectF(flag.center().x() - cover.width() / 2, flag.center().y() - cover.height() / 2,
					 cover.width(), cover.height()),
				  image);
		painter.setClipping(false);
		painter.drawEllipse(flag);
		painter.restore();
	}
	// Separator.
	painter.save();
	painter.setPen(QPen(with_alpha(style.text, 50), std::max<qreal>(1.0, l.unit * 0.012)));
	const qreal x = l.league.right() + l.unit * 0.075;
	painter.drawLine(QPointF(x, l.board.top() + l.unit * 0.18), QPointF(x, l.board.bottom() - l.unit * 0.18));
	painter.restore();
}

void draw_side(QPainter &painter, const QRectF &area, const SideView &side, bool crest_on_right, const Layout &l,
	       const BoardStyle &style, const QColor *halo)
{
	if (area.width() <= 0)
		return;
	const qreal crest = std::min(area.height() * 0.86, area.width() * 0.45);
	const qreal gap = l.unit * 0.12;
	QRectF crest_rect, name_rect;
	if (crest_on_right) {
		crest_rect = QRectF(area.right() - crest, area.center().y() - crest / 2, crest, crest);
		name_rect = QRectF(area.left(), area.top(), std::max<qreal>(0.0, crest_rect.left() - gap - area.left()),
				   area.height());
	} else {
		crest_rect = QRectF(area.left(), area.center().y() - crest / 2, crest, crest);
		name_rect = QRectF(crest_rect.right() + gap, area.top(),
				   std::max<qreal>(0.0, area.right() - crest_rect.right() - gap), area.height());
	}
	draw_contained(painter, side.logo, crest_rect);

	const QFont::Weight weight = style.preset == StylePreset::Minimal ? QFont::DemiBold : QFont::Bold;
	painter.setFont(fitted_font(side.name, name_rect.width(), l.unit * 0.27, l.unit * 0.18, weight));
	draw_text(painter, name_rect, side.name, (crest_on_right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
		  style.text, halo);
}

void draw_score_box(QPainter &painter, const Layout &l, const ScoreView &view, const BoardStyle &style,
		    const QColor *halo, double pulse)
{
	const QRectF box = l.score;
	const qreal radius = l.unit * 0.14;
	const QRectF main = QRectF(box.left(), box.top(), box.width(), box.height() * 0.68);
	const QRectF status = QRectF(box.left(), main.bottom(), box.width(), box.height() - main.height());

	QColor fill;
	switch (style.preset) {
	case StylePreset::Minimal:
		fill = with_alpha(style.text, 28);
		break;
	case StylePreset::Bold:
		fill = style.accent;
		break;
	case StylePreset::Neon:
		fill = QColor(0, 0, 0, 120);
		break;
	}
	if (pulse > 0.0) {
		painter.save();
		painter.setBrush(Qt::NoBrush);
		for (int i = 3; i >= 1; --i) {
			painter.setPen(QPen(with_alpha(style.accent, static_cast<int>(pulse * 60 * (4 - i))), l.unit * 0.06 * i));
			painter.drawPath(rounded(main, radius));
		}
		painter.restore();
	}
	painter.fillPath(rounded(main, radius), fill);
	if (style.preset == StylePreset::Neon) {
		painter.save();
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(style.accent.lighter(130), std::max<qreal>(1.0, l.unit * 0.02)));
		painter.drawPath(rounded(main, radius));
		painter.restore();
	}

	painter.setFont(font_with(main.height() * 0.66, QFont::Black));
	draw_text(painter, main, view.center, Qt::AlignCenter, style.text, halo);

	// Status line, with a blinking dot while live.
	painter.setFont(font_with(status.height() * 0.62, QFont::DemiBold));
	const QFontMetricsF metrics(painter.font());
	const qreal text_w = std::min(metrics.horizontalAdvance(view.status), status.width());
	const qreal dot = view.live ? status.height() * 0.32 : 0.0;
	const qreal dot_gap = view.live ? dot * 0.7 : 0.0;
	const qreal start = status.center().x() - (text_w + dot + dot_gap) / 2;
	if (view.live) {
		painter.save();
		painter.setPen(Qt::NoPen);
		painter.setBrush(with_alpha(kLiveRed, static_cast<int>(255 * view.blink)));
		painter.drawEllipse(QRectF(start, status.center().y() - dot / 2, dot, dot));
		painter.restore();
	}
	const QColor status_color = view.live ? QColor(255, 255, 255) : with_alpha(style.text, 200);
	draw_text(painter, QRectF(start + dot + dot_gap, status.top(), text_w + 1, status.height()), view.status,
		  Qt::AlignLeft | Qt::AlignVCenter, status_color);
}

void draw_goal_banner(QPainter &painter, const Layout &l, const ScoreView &view, const BoardStyle &style)
{
	const GoalAnimationFrame &goal = view.goal;
	const SideView &side = view.goal_home ? view.home : view.away;
	QColor base = side.color.isValid() ? side.color : style.accent;
	// White text must stay readable: darken light team colors (gold, white, sky blue).
	while (base.lightnessF() > 0.42)
		base = base.darker(125);
	const qreal radius = l.unit * 0.14;

	// The banner slides in from the scoring team's side.
	const QRectF full = l.content;
	const qreal shown = full.width() * goal.banner;
	const QRectF banner = view.goal_home ? QRectF(full.left(), full.top(), shown, full.height())
					     : QRectF(full.right() - shown, full.top(), shown, full.height());
	painter.save();
	painter.setClipRect(banner);
	QLinearGradient gradient(full.topLeft(), full.topRight());
	gradient.setColorAt(0.0, base.darker(view.goal_home ? 100 : 160));
	gradient.setColorAt(1.0, base.darker(view.goal_home ? 160 : 100));
	painter.fillPath(rounded(full, radius), gradient);

	if (goal.text > 0.0) {
		painter.setOpacity(goal.text);
		// Crest + "GOAL!" on the scorer's half, the detail on the other half.
		const QRectF own = view.goal_home ? l.home : l.away;
		const QRectF other = view.goal_home ? l.away : l.home;
		const qreal crest = own.height() * 0.9;
		const QRectF crest_rect = view.goal_home
						  ? QRectF(own.left(), own.center().y() - crest / 2, crest, crest)
						  : QRectF(own.right() - crest, own.center().y() - crest / 2, crest, crest);
		if (view.goal_photo.isNull()) {
			draw_contained(painter, side.logo, crest_rect);
		} else {
			// Round headshot with the crest as a small badge on its corner.
			painter.save();
			QPainterPath circle;
			circle.addEllipse(crest_rect);
			painter.fillPath(circle, QColor(255, 255, 255, 40));
			painter.setClipPath(circle);
			const QImage &photo = view.goal_photo;
			const QSizeF cover = QSizeF(photo.size()).scaled(crest_rect.size(), Qt::KeepAspectRatioByExpanding);
			painter.drawImage(QRectF(crest_rect.center().x() - cover.width() / 2, crest_rect.top(), cover.width(),
						 cover.height()),
					  photo);
			painter.restore();
			const qreal badge = crest_rect.width() * 0.4;
			draw_contained(painter, side.logo,
				       QRectF(crest_rect.right() - badge * 0.8, crest_rect.bottom() - badge, badge, badge));
		}
		const qreal gap = l.unit * 0.12;
		const QRectF title_rect = view.goal_home
						  ? QRectF(crest_rect.right() + gap, own.top(),
							   std::max<qreal>(0.0, own.right() - crest_rect.right() - gap), own.height())
						  : QRectF(own.left(), own.top(),
							   std::max<qreal>(0.0, crest_rect.left() - gap - own.left()), own.height());
		painter.setFont(fitted_font(view.goal_title, title_rect.width(), l.unit * 0.46, l.unit * 0.2, QFont::Black));
		const QColor shadow(0, 0, 0, 110);
		draw_text(painter, title_rect.translated(l.unit * 0.02, l.unit * 0.03), view.goal_title,
			  (view.goal_home ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, shadow);
		draw_text(painter, title_rect, view.goal_title,
			  (view.goal_home ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, QColor(255, 255, 255));

		const QString detail = view.goal_detail.isEmpty() ? side.name : view.goal_detail;
		painter.setFont(fitted_font(detail, other.width(), l.unit * 0.26, l.unit * 0.16, QFont::Bold));
		draw_text(painter, other, detail, (view.goal_home ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
			  QColor(255, 255, 255));
	}
	painter.restore();
}

void draw_message(QPainter &painter, const Layout &l, const QString &message, const BoardStyle &style,
		  const QColor *halo)
{
	painter.setFont(font_with(l.unit * 0.26, QFont::DemiBold));
	draw_text(painter, l.content, message, Qt::AlignCenter, with_alpha(style.text, 220), halo);
}

} // namespace

QImage ScoreboardPainter::paint(const ScoreView &view, const BoardStyle &style, QSize size)
{
	QImage image(size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	if (size.isEmpty())
		return image;

	// The score box grows with its text ("10 - 10", "20:00").
	qreal score_hint = 0.0;
	{
		const qreal unit = size.height();
		const QFontMetricsF metrics(font_with(unit * 0.8 * 0.68 * 0.66, QFont::Black));
		score_hint = metrics.horizontalAdvance(view.center) + unit * 0.5;
	}
	const Layout l = layout_for(style, QSizeF(size), score_hint);

	QPainter painter(&image);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

	const QColor *halo = style.preset == StylePreset::Neon ? &style.accent : nullptr;
	draw_background(painter, l, style);
	draw_league(painter, l, view, style);

	if (!view.has_match) {
		draw_message(painter, l, view.message, style, halo);
		return image;
	}

	if (style.preset == StylePreset::Bold)
		draw_team_stripes(painter, l, view);
	draw_side(painter, l.home, view.home, true, l, style, halo);
	draw_side(painter, l.away, view.away, false, l, style, halo);

	if (view.goal.active && view.goal.banner > 0.0)
		draw_goal_banner(painter, l, view, style);
	draw_score_box(painter, l, view, style, halo, view.goal.active ? view.goal.pulse : 0.0);

	if (view.goal.flash > 0.0) {
		painter.fillPath(rounded(l.board, l.unit * 0.18),
				 QColor(255, 255, 255, static_cast<int>(view.goal.flash * 150)));
	}
	return image;
}

} // namespace fscore::render
