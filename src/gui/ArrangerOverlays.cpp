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

#include <algorithm>
#include <cmath>

#include <QEvent>
#include <QJsonArray>
#include <QMouseEvent>
#include <QScrollArea>
#include <QPainter>
#include <QPainterPath>

#include "ArrangementEdits.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Song.h"
#include "TempoMap.h"
#include "ProjectMarkers.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "TrackContentWidget.h"
#include "TrackView.h"

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

void MarkerRulerOverlay::paintTempo(QPainter& p)
{
	// ED-050: an active tempo map as a curve along the ruler's lower edge - holds flat, ramps
	// slope - so a ramp or a jump is visible where it happens.
	const TempoMap& map = Engine::getSong()->tempoMap().map();
	if (!map.active() || map.empty()) { return; }
	const int global = Engine::getSong()->getTempo();
	int low = global;
	int high = global;
	for (const TempoMapEvent& event : map.all())
	{
		if (event.hasTempo) { low = std::min(low, event.tempo); high = std::max(high, event.tempo); }
	}
	const double x0 = m_ruler->markerX(TimePos(0));
	const double perTick = (m_ruler->markerX(TimePos(TimePos::ticksPerBar())) - x0) / TimePos::ticksPerBar();
	if (perTick <= 0.0) { return; }
	constexpr int Band = 8;
	QPolygonF curve;
	for (int x = 0; x <= width(); x += 2)
	{
		const double tempo = map.exactTempoAtTick(static_cast<tick_t>(std::max(0.0, (x - x0) / perTick)), global);
		const double y = height() - 1 - (high > low ? (tempo - low) / (high - low) : 0.5) * Band;
		curve << QPointF(x, y);
	}
	p.setPen(QPen(tokens::palette().play, 1.25));
	p.drawPolyline(curve);
}

void MarkerRulerOverlay::paintEvent(QPaintEvent*)
{
	m_drawn.clear();
	if (m_ruler.isNull()) { return; }
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	paintTempo(p);
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

ArrangerRangeOverlay::ArrangerRangeOverlay(SongEditor* editor) :
	QWidget(editor->contentWidget()->viewport()),
	m_editor(editor)
{
	setObjectName(QStringLiteral("arrangerRangeOverlay"));
	setAttribute(Qt::WA_NoSystemBackground);
	setTool(Tool::None);
	QWidget* viewport = editor->contentWidget()->viewport();
	setGeometry(viewport->rect());
	viewport->installEventFilter(this);
	connect(&ArrangerRange::instance(), &ArrangerRange::changed, this, qOverload<>(&QWidget::update));
	connect(editor, &SongEditor::pixelsPerBarChanged, this, qOverload<>(&QWidget::update));
	show();
	raise();
}

void ArrangerRangeOverlay::setTool(Tool tool)
{
	m_tool = tool;
	setAttribute(Qt::WA_TransparentForMouseEvents, !toolActive());
	setCursor(tool == Tool::Zoom ? Qt::CrossCursor : tool == Tool::Audition ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

void ArrangerRangeOverlay::beginRangeFrom(const QPoint& globalPos)
{
	m_dragging = true;
	m_from = mapFromGlobal(globalPos);
	grabMouse();
}

bool ArrangerRangeOverlay::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::Resize) { setGeometry(static_cast<QWidget*>(watched)->rect()); }
	if (event->type() == QEvent::Paint) { update(); }
	return QWidget::eventFilter(watched, event);
}

int ArrangerRangeOverlay::contentLeft() const
{
	const QList<TrackView*>& views = m_editor->trackViews();
	if (views.isEmpty() || views.first()->getTrackContentWidget() == nullptr) { return 0; }
	return views.first()->getTrackContentWidget()->mapTo(parentWidget(), QPoint(0, 0)).x();
}

tick_t ArrangerRangeOverlay::tickAt(int x) const
{
	const float ppb = std::max(1.f, m_editor->pixelsPerBar());
	const double ticks = m_editor->currentPosition().getTicks() + (x - contentLeft()) * TimePos::ticksPerBar() / ppb;
	const double snap = std::max(1.0, static_cast<double>(m_editor->getSnapSize()) * TimePos::ticksPerBar());
	return static_cast<tick_t>(std::max(0.0, std::round(ticks / snap) * snap));
}

Track* ArrangerRangeOverlay::trackAt(int y) const
{
	for (TrackView* view : m_editor->trackViews())
	{
		const QRect area(view->mapTo(parentWidget(), QPoint(0, 0)), view->size());
		if (y >= area.top() && y <= area.bottom()) { return view->getTrack(); }
	}
	return nullptr;
}

QRect ArrangerRangeOverlay::rangeRect() const
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active() || m_editor.isNull()) { return {}; }
	const float ppb = std::max(1.f, m_editor->pixelsPerBar());
	const auto xOf = [&](tick_t tick) {
		return contentLeft() + static_cast<int>((tick - m_editor->currentPosition().getTicks()) * ppb / TimePos::ticksPerBar());
	};
	QRect area;
	const QVector<Track*> covered = range.tracks();
	for (TrackView* view : m_editor->trackViews())
	{
		if (!covered.contains(view->getTrack())) { continue; }
		const QPoint top = view->mapTo(parentWidget(), QPoint(0, 0));
		area = area.united(QRect(xOf(range.start()), top.y(), xOf(range.end()) - xOf(range.start()), view->height()));
	}
	return area;
}

void ArrangerRangeOverlay::paintEvent(QPaintEvent*)
{
	const QRect area = rangeRect();
	if (area.isEmpty()) { return; }
	QColor fill = tokens::palette().accent;
	fill.setAlpha(48);
	QPainter p(this);
	p.fillRect(area, fill);
	p.setPen(QPen(tokens::palette().accent, 1));
	p.drawLine(area.topLeft(), area.bottomLeft());
	p.drawLine(area.topRight(), area.bottomRight());
}

void ArrangerRangeOverlay::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return QWidget::mousePressEvent(event); }
	m_dragging = true;
	m_from = event->pos();
	if (m_tool == Tool::Audition)
	{
		Track* track = trackAt(m_from.y());
		QJsonObject args{{QStringLiteral("position"), tickAt(m_from.x())}};
		if (track != nullptr) { args.insert(QStringLiteral("track"), control::trackIdOf(track)); }
		ControlRegistry::instance()->invoke(QStringLiteral("transport.audition_start"), args);
	}
}

void ArrangerRangeOverlay::mouseMoveEvent(QMouseEvent* event)
{
	if (m_dragging && (m_tool == Tool::Range || m_tool == Tool::Smart)) { dispatchRange(event->pos()); }
}

void ArrangerRangeOverlay::mouseReleaseEvent(QMouseEvent* event)
{
	if (!m_dragging) { return; }
	m_dragging = false;
	if (QWidget::mouseGrabber() == this) { releaseMouse(); }
	finishGesture(event->pos());
}

void ArrangerRangeOverlay::finishGesture(const QPoint& to)
{
	switch (m_tool)
	{
	case Tool::Audition: ControlRegistry::instance()->invoke(QStringLiteral("transport.audition_stop")); break;
	case Tool::Zoom:
		if (std::abs(to.x() - m_from.x()) > 4)
		{
			ControlRegistry::instance()->invoke(QStringLiteral("arrangement.zoom"),
				{{QStringLiteral("start"), std::min(tickAt(m_from.x()), tickAt(to.x()))}, {QStringLiteral("end"), std::max(tickAt(m_from.x()), tickAt(to.x()))}});
		}
		break;
	default: dispatchRange(to); break;
	}
}

void ArrangerRangeOverlay::dispatchRange(const QPoint& to)
{
	// The tracks between the two rows, in the arranger's order.
	QJsonArray tracks;
	bool inside = false;
	Track* first = trackAt(m_from.y());
	Track* last = trackAt(to.y());
	for (TrackView* view : m_editor->trackViews())
	{
		const bool edge = view->getTrack() == first || view->getTrack() == last;
		if (edge || inside) { tracks.append(control::trackIdOf(view->getTrack())); }
		if (edge && first != last) { inside = !inside; }
	}
	ControlRegistry::instance()->invoke(QStringLiteral("range.set"), {{QStringLiteral("start"), tickAt(m_from.x())},
		{QStringLiteral("end"), tickAt(to.x())}, {QStringLiteral("tracks"), tracks}});
}

void installArrangerOverlays(SongEditor* editor)
{
	if (editor == nullptr || editor->timeLine() == nullptr) { return; }
	if (editor->timeLine()->findChild<MarkerRulerOverlay*>() == nullptr) { new MarkerRulerOverlay(editor->timeLine()); }
	if (editor->findChild<ArrangerRangeOverlay*>() == nullptr) { new ArrangerRangeOverlay(editor); }
}

} // namespace lmms::gui
