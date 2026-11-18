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

#include "ArrangementEdits.h"
#include "ArrangerEditPreview.h"
#include "ArrangerRulerLanes.h"
#include "ClipLockFeedback.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "TrackContentWidget.h"
#include "TrackView.h"
#include "VideoLane.h"

namespace lmms::gui
{

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
	// A tool chosen while an audition is held ends it: the gesture belongs to the tool that began it, so the
	// release finishes the audition rather than running the tool that replaced it over the same press.
	if (m_dragging && m_tool == Tool::Audition && tool != Tool::Audition)
	{
		m_dragging = false;
		ControlRegistry::instance()->invoke(QStringLiteral("transport.audition_stop"));
	}
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
	// The overlay is a transparent child of the viewport it watches, and repainting it repaints that viewport, so an
	// unconditional update() here would feed itself forever. Repaint only when the rows it draws have moved.
	if (event->type() == QEvent::Paint && rangeRows() != m_paintedRows) { update(); }
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

Track* ArrangerRangeOverlay::nearestTrackAt(int y) const
{
	const QList<TrackView*>& views = m_editor->trackViews();
	if (Track* track = trackAt(y); track != nullptr || views.isEmpty()) { return track; }
	return y < views.first()->mapTo(parentWidget(), QPoint(0, 0)).y() ? views.first()->getTrack() : views.last()->getTrack();
}

QVector<QRect> ArrangerRangeOverlay::rangeRows() const
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active() || m_editor.isNull()) { return {}; }
	const float ppb = std::max(1.f, m_editor->pixelsPerBar());
	const auto xOf = [&](tick_t tick) {
		return contentLeft() + static_cast<int>((tick - m_editor->currentPosition().getTicks()) * ppb / TimePos::ticksPerBar());
	};
	QVector<QRect> rows;
	const QVector<Track*> covered = range.tracks();
	for (TrackView* view : m_editor->trackViews())
	{
		if (!covered.contains(view->getTrack())) { continue; }
		const QPoint top = view->mapTo(parentWidget(), QPoint(0, 0));
		rows.append(QRect(xOf(range.start()), top.y(), xOf(range.end()) - xOf(range.start()), view->height()));
	}
	return rows;
}

QRect ArrangerRangeOverlay::rangeRect() const
{
	QRect area;
	for (const QRect& row : rangeRows()) { area = area.united(row); }
	return area;
}

void ArrangerRangeOverlay::paintEvent(QPaintEvent*)
{
	// Row by row: a range over tracks that are not adjacent leaves the rows between them unmarked.
	QColor fill = tokens::palette().accent;
	fill.setAlpha(48);
	QPainter p(this);
	p.setPen(QPen(tokens::palette().accent, 1));
	m_paintedRows = rangeRows();
	for (const QRect& row : m_paintedRows)
	{
		if (row.isEmpty()) { continue; }
		p.fillRect(row, fill);
		p.drawLine(row.topLeft(), row.bottomLeft());
		p.drawLine(row.topRight(), row.bottomRight());
	}
}

bool ArrangerRangeOverlay::event(QEvent* event)
{
	// A gesture ends however it is cut short - the window losing focus, the grab taken away by a popup,
	// the overlay hidden - not only on a release that may never come. A drag left marked active would
	// keep dispatching ranges from a pointer the user no longer holds the button on.
	const QEvent::Type type = event->type();
	if (m_dragging && (type == QEvent::WindowDeactivate || type == QEvent::UngrabMouse || type == QEvent::Hide))
	{
		const bool audition = m_tool == Tool::Audition;
		m_dragging = false;
		if (audition) { ControlRegistry::instance()->invoke(QStringLiteral("transport.audition_stop")); }
	}
	return QWidget::event(event);
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
	// An end off the rows (above or below them) resolves to the nearest row, not past it - and BOTH ends
	// off them still name the rows between: a drag from above the first track to below the last reaches
	// every track, where resolving neither left the span over no track at all.
	if (first == nullptr || last == nullptr) { first = nearestTrackAt(m_from.y()); last = nearestTrackAt(to.y()); }
	for (TrackView* view : m_editor->trackViews())
	{
		const bool edge = view->getTrack() == first || view->getTrack() == last;
		if (edge || inside) { tracks.append(control::trackIdOf(view->getTrack())); }
		if (edge && first != last) { inside = !inside; }
	}
	ControlRegistry::instance()->invoke(QStringLiteral("range.set"), {{QStringLiteral("start"), tickAt(m_from.x())},
		{QStringLiteral("end"), tickAt(to.x())}, {QStringLiteral("tracks"), tracks}});
}

const QStringList& arrangerToolNames()
{
	static const QStringList names{QStringLiteral("draw"), QStringLiteral("knife"), QStringLiteral("select"),
		QStringLiteral("range"), QStringLiteral("audition"), QStringLiteral("zoom"), QStringLiteral("smart")};
	return names;
}

QString currentArrangerTool(SongEditor* editor)
{
	const auto* overlay = editor->findChild<ArrangerRangeOverlay*>();
	if (overlay != nullptr && overlay->tool() != ArrangerRangeOverlay::Tool::None)
	{
		return arrangerToolNames().at(2 + static_cast<int>(overlay->tool()));  // after the three editor modes, None skipped
	}
	return arrangerToolNames().at(static_cast<int>(editor->editMode()));
}

void installArrangerOverlays(SongEditor* editor)
{
	if (editor == nullptr || editor->timeLine() == nullptr) { return; }
	if (editor->findChild<ArrangerRulerLanes*>() == nullptr) { new ArrangerRulerLanes(editor); }
	if (editor->findChild<VideoLane*>() == nullptr) { new VideoLane(editor); }
	if (editor->findChild<ArrangerRangeOverlay*>() == nullptr) { new ArrangerRangeOverlay(editor); }
	if (editor->findChild<ArrangerEditPreview*>() == nullptr) { new ArrangerEditPreview(editor); }
	cliplocks::install();
}

} // namespace lmms::gui
