/*
 * ShellIcons.cpp - resolution-independent icons for the 0.5.0 shell, painted from paths
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "ShellIcons.h"

#include <functional>
#include <map>

#include <QIconEngine>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPixmap>

#include "DesignTokens.h"

namespace lmms::gui::shellicons
{

namespace
{

//! A stroke of width @a w turned into a fillable outline, so every glyph paints one way.
QPainterPath stroked(const QPainterPath& path, qreal w = 1.5)
{
	QPainterPathStroker stroker;
	stroker.setWidth(w);
	stroker.setCapStyle(Qt::RoundCap);
	stroker.setJoinStyle(Qt::RoundJoin);
	return stroker.createStroke(path);
}

QPainterPath line(qreal x1, qreal y1, qreal x2, qreal y2, qreal w = 1.5)
{
	QPainterPath p;
	p.moveTo(x1, y1);
	p.lineTo(x2, y2);
	return stroked(p, w);
}

QPainterPath box(qreal x, qreal y, qreal w, qreal h, qreal r = 1.5)
{
	QPainterPath p;
	p.addRoundedRect(QRectF(x, y, w, h), r, r);
	return p;
}

QPainterPath outline(qreal x, qreal y, qreal w, qreal h, qreal r = 1.5)
{
	return stroked(box(x, y, w, h, r), 1.4);
}

QPainterPath polygon(std::initializer_list<QPointF> points)
{
	QPainterPath p;
	p.addPolygon(QPolygonF(QVector<QPointF>(points)));
	p.closeSubpath();
	return p;
}

QPainterPath inspector()
{
	QPainterPath p = outline(2, 2, 12, 12, 2.5);
	p.addEllipse(QPointF(8, 5.2), 1.1, 1.1);
	return p.united(line(8, 7.6, 8, 11.2, 1.7));
}

QPainterPath browser()
{
	QPainterPath p = outline(2, 4, 12, 9, 1.5);
	return p.united(stroked(polygon({{2, 4}, {2, 2.8}, {6.5, 2.8}, {7.5, 4}}), 1.2));
}

QPainterPath devices()
{
	QPainterPath p = outline(1.5, 3.5, 6, 9, 1.5);
	p = p.united(outline(8.5, 3.5, 6, 9, 1.5));
	p.addEllipse(QPointF(4.5, 7), 1.2, 1.2);
	p.addEllipse(QPointF(11.5, 7), 1.2, 1.2);
	return p;
}

QPainterPath detail()
{
	QPainterPath p = box(2, 3, 5, 2, 0.8);
	p.addPath(box(6, 7, 6, 2, 0.8));
	p.addPath(box(9, 11, 5, 2, 0.8));
	return p;
}

QPainterPath mixer()
{
	QPainterPath p = line(4, 2.5, 4, 13.5, 1.2);
	p = p.united(line(8, 2.5, 8, 13.5, 1.2)).united(line(12, 2.5, 12, 13.5, 1.2));
	p.addPath(box(2.5, 9, 3, 2, 0.6));
	p.addPath(box(6.5, 4.5, 3, 2, 0.6));
	p.addPath(box(10.5, 7, 3, 2, 0.6));
	return p;
}

QPainterPath automation()
{
	QPainterPath curve;
	curve.moveTo(2, 12);
	curve.lineTo(6, 5);
	curve.lineTo(10, 9);
	curve.lineTo(14, 3);
	QPainterPath p = stroked(curve, 1.3);
	for (const QPointF& at : {QPointF(2, 12), QPointF(6, 5), QPointF(10, 9), QPointF(14, 3)})
	{
		p.addEllipse(at, 1.5, 1.5);
	}
	return p;
}

QPainterPath pattern()
{
	QPainterPath p;
	for (int row = 0; row < 3; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			const bool on = (row + col) % 2 == 0;
			p.addPath(on ? box(1.5 + col * 3.4, 3 + row * 3.6, 2.6, 2.6, 0.6)
				: outline(1.5 + col * 3.4, 3 + row * 3.6, 2.6, 2.6, 0.6));
		}
	}
	return p;
}

QPainterPath launcher()
{
	QPainterPath p;
	for (int row = 0; row < 3; ++row)
	{
		for (int col = 0; col < 3; ++col) { p.addPath(box(2 + col * 4.3, 2 + row * 4.3, 3.2, 3.2, 0.7)); }
	}
	return p;
}

QPainterPath arranger()
{
	QPainterPath p = box(1.5, 3, 6, 2.6, 0.8);
	p.addPath(box(8.5, 3, 6, 2.6, 0.8));
	p.addPath(box(4, 7, 8, 2.6, 0.8));
	p.addPath(box(1.5, 11, 4, 2.6, 0.8));
	p.addPath(box(9, 11, 5.5, 2.6, 0.8));
	return p;
}

QPainterPath play() { return polygon({{4, 2.5}, {13.5, 8}, {4, 13.5}}); }
QPainterPath stop() { return box(3.5, 3.5, 9, 9, 1.2); }

QPainterPath record()
{
	QPainterPath p;
	p.addEllipse(QPointF(8, 8), 5, 5);
	return p;
}

QPainterPath loop()
{
	QPainterPath arc;
	arc.arcMoveTo(QRectF(2.5, 3.5, 11, 9), 30);
	arc.arcTo(QRectF(2.5, 3.5, 11, 9), 30, 300);
	QPainterPath p = stroked(arc, 1.5);
	return p.united(polygon({{13.8, 2.8}, {14.2, 7.2}, {10.2, 6.0}}));
}

QPainterPath metronome()
{
	QPainterPath p = stroked(polygon({{5, 14}, {11, 14}, {9.4, 2.5}, {6.6, 2.5}}), 1.3);
	return p.united(line(8, 11, 12.5, 4.5, 1.3));
}

QPainterPath draw()
{
	QPainterPath p = polygon({{3, 13}, {3.6, 10.2}, {11, 2.8}, {13.2, 5}, {5.8, 12.4}});
	return p;
}

QPainterPath selectTool()
{
	return polygon({{3, 2}, {12.5, 9.2}, {8.4, 9.6}, {10.8, 13.8}, {9.2, 14.6}, {6.8, 10.4}, {3.8, 13}});
}

QPainterPath slice()
{
	QPainterPath p = polygon({{9.5, 1.5}, {11.5, 1.5}, {7.5, 14.5}, {5.8, 14.5}});
	return p.united(line(2, 8, 14, 8, 1.0));
}

QPainterPath erase()
{
	QPainterPath p = polygon({{2, 10}, {8.5, 3.5}, {13.5, 8.5}, {9, 13}, {4.5, 13}});
	return p.subtracted(polygon({{4.2, 10}, {6.6, 7.6}, {10, 11}, {8.4, 11.8}, {5.4, 11.8}}));
}

QPainterPath audition()
{
	QPainterPath p = polygon({{2, 6}, {5, 6}, {9, 2.5}, {9, 13.5}, {5, 10}, {2, 10}});
	QPainterPath wave;
	wave.arcMoveTo(QRectF(6, 4, 7, 8), -50);
	wave.arcTo(QRectF(6, 4, 7, 8), -50, 100);
	return p.united(stroked(wave, 1.3));
}

QPainterPath fold()
{
	QPainterPath p = line(2, 3, 14, 3, 1.4).united(line(2, 13, 14, 13, 1.4));
	p = p.united(polygon({{8, 5}, {11, 7.5}, {5, 7.5}})).united(polygon({{8, 11}, {11, 8.5}, {5, 8.5}}));
	return p;
}

QPainterPath ghost()
{
	QPainterPath p = outline(1.5, 3, 7, 3, 0.8);
	p.addPath(box(7.5, 9, 7, 3, 0.8));
	return p;
}

QPainterPath scale()
{
	QPainterPath p;
	const int keys[] = {0, 2, 4, 5, 7, 9, 11};
	for (int i = 0; i < 7; ++i) { p.addPath(box(1.2 + keys[i] * 1.15, 13 - i * 1.6, 1.4, 1.4 + i * 1.6, 0.4)); }
	return p;
}

QPainterPath plus() { return line(8, 3, 8, 13, 1.7).united(line(3, 8, 13, 8, 1.7)); }

QPainterPath quantize()
{
	QPainterPath p = line(3, 2, 3, 14, 1.0).united(line(8, 2, 8, 14, 1.0)).united(line(13, 2, 13, 14, 1.0));
	p.addPath(box(3, 6.5, 5, 3, 0.8));
	return p;
}

QPainterPath follow()
{
	QPainterPath p = line(8, 2, 8, 14, 1.4);
	return p.united(polygon({{10, 5}, {14, 8}, {10, 11}}));
}

QPainterPath chevron()
{
	QPainterPath p;
	p.moveTo(4, 6);
	p.lineTo(8, 10);
	p.lineTo(12, 6);
	return stroked(p, 1.6);
}

QPainterPath newProject()
{
	QPainterPath p = stroked(polygon({{3.5, 1.8}, {9.5, 1.8}, {12.5, 4.8}, {12.5, 14.2}, {3.5, 14.2}}), 1.3);
	return p.united(line(8, 7, 8, 12, 1.4)).united(line(5.5, 9.5, 10.5, 9.5, 1.4));
}

QPainterPath openProject()
{
	QPainterPath p = stroked(polygon({{1.8, 3.5}, {6, 3.5}, {7.2, 5}, {13, 5}, {13, 7}}), 1.3);
	return p.united(polygon({{1.8, 13.5}, {4.2, 7.5}, {15, 7.5}, {12.6, 13.5}}));
}

QPainterPath saveProject()
{
	QPainterPath p = stroked(polygon({{2.5, 2.5}, {11.5, 2.5}, {13.5, 4.5}, {13.5, 13.5}, {2.5, 13.5}}), 1.3);
	p.addPath(box(5, 2.5, 5, 3.5, 0.5));
	return p.united(outline(4.8, 8.5, 6.4, 5, 0.8));
}

QPainterPath exportProject()
{
	QPainterPath p = stroked(polygon({{6, 4}, {2.5, 4}, {2.5, 13.5}, {13.5, 13.5}, {13.5, 10}}), 1.3);
	p = p.united(line(7, 9, 13, 3, 1.4));
	return p.united(polygon({{9, 2.2}, {13.8, 2.2}, {13.8, 7}}));
}

using Maker = std::function<QPainterPath()>;

const std::map<QString, Maker>& makers()
{
	static const std::map<QString, Maker> table{
		{"inspector", inspector}, {"browser", browser}, {"devices", devices}, {"detail", detail},
		{"mixer", mixer}, {"automation", automation}, {"pattern", pattern}, {"launcher", launcher},
		{"arranger", arranger}, {"play", play}, {"stop", stop}, {"record", record}, {"loop", loop},
		{"metronome", metronome}, {"draw", draw}, {"select", selectTool}, {"slice", slice}, {"erase", erase},
		{"audition", audition}, {"fold", fold}, {"ghost", ghost}, {"scale", scale}, {"plus", plus},
		{"quantize", quantize}, {"follow", follow}, {"chevron", chevron}, {"new", newProject},
		{"open", openProject}, {"save", saveProject}, {"export", exportProject}};
	return table;
}

class GlyphEngine : public QIconEngine
{
public:
	explicit GlyphEngine(QString id) : m_id(std::move(id)) {}

	void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
	{
		const tokens::Palette& pal = tokens::palette();
		QColor colour = state == QIcon::On ? pal.accent : pal.text;
		if (mode == QIcon::Disabled) { colour = pal.textDisabled; }
		const qreal side = std::min(rect.width(), rect.height());
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
		painter->scale(side / 16.0, side / 16.0);
		painter->setPen(Qt::NoPen);
		painter->setBrush(colour);
		painter->drawPath(glyph(m_id));
		painter->restore();
	}

	QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
	{
		QPixmap pm(size);
		pm.fill(Qt::transparent);
		QPainter p(&pm);
		paint(&p, QRect(QPoint(0, 0), size), mode, state);
		return pm;
	}

	// No scaledPixmap() override: QIcon asks the engine for the DEVICE-pixel size and sets the
	// pixmap's device-pixel ratio itself, so pixmap() above paints the path at the screen's
	// resolution (DesignTokensTest::iconsPaintAtTheScreensScale holds this).

	QIconEngine* clone() const override { return new GlyphEngine(m_id); }

private:
	QString m_id;
};

} // namespace

QStringList ids()
{
	QStringList list;
	for (const auto& entry : makers()) { list << entry.first; }
	return list;
}

QPainterPath glyph(const QString& id)
{
	const auto it = makers().find(id);
	return it == makers().end() ? QPainterPath() : it->second();
}

QIcon icon(const QString& id) { return QIcon(new GlyphEngine(id)); }

} // namespace lmms::gui::shellicons
