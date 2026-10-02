/*
 * ArrangerOverlays.cpp - what the 0.5.0 arranger draws over the inherited song editor
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

#include "ArrangerOverlays.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>

#include "DesignTokens.h"
#include "ProjectMarkers.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"

namespace lmms::gui
{

MarkerRulerOverlay::MarkerRulerOverlay(TimeLineWidget* ruler) :
	QWidget(ruler),
	m_ruler(ruler)
{
	setObjectName(QStringLiteral("markerRulerOverlay"));
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setGeometry(ruler->rect());
	ruler->installEventFilter(this);
	connect(&ProjectMarkers::instance(), &ProjectMarkers::changed, this, qOverload<>(&QWidget::update));
	show();
	raise();
}

bool MarkerRulerOverlay::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_ruler && event->type() == QEvent::Resize) { setGeometry(m_ruler->rect()); }
	if (watched == m_ruler && event->type() == QEvent::Paint) { update(); }
	return QWidget::eventFilter(watched, event);
}

void MarkerRulerOverlay::paintEvent(QPaintEvent*)
{
	m_drawn.clear();
	if (m_ruler.isNull()) { return; }
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(tokens::font(tokens::TextRole::Small));
	for (const Marker& marker : ProjectMarkers::instance().markers())
	{
		const int x = m_ruler->markerX(TimePos(marker.tick));
		if (x < 0 || x >= width()) { continue; }
		const QColor colour = marker.color.isEmpty() ? pal.accent : QColor(marker.color);
		QPainterPath flag;
		flag.moveTo(x, 0);
		flag.lineTo(x + 7, 0);
		flag.lineTo(x, 7);
		flag.closeSubpath();
		p.fillPath(flag, colour);
		p.setPen(QPen(colour, 1));
		p.drawLine(x, 0, x, height());
		p.setPen(pal.text);
		p.drawText(QPoint(x + 9, height() - 3), marker.name);
		m_drawn.append(x);
	}
}

void installArrangerOverlays(SongEditor* editor)
{
	if (editor == nullptr || editor->timeLine() == nullptr) { return; }
	if (editor->timeLine()->findChild<MarkerRulerOverlay*>() == nullptr) { new MarkerRulerOverlay(editor->timeLine()); }
}

} // namespace lmms::gui
