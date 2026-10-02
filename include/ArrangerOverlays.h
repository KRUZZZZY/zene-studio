/*
 * ArrangerOverlays.h - what the 0.5.0 arranger draws over the inherited song editor
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

#ifndef LMMS_GUI_ARRANGER_OVERLAYS_H
#define LMMS_GUI_ARRANGER_OVERLAYS_H

#include <QPointer>
#include <QWidget>

#include "LmmsTypes.h"
#include "lmms_export.h"

class QPainter;

namespace lmms
{
class Track;
}

namespace lmms::gui
{

class SongEditor;
class TimeLineWidget;

/*! ED-048: the project's markers, drawn over the arranger's ruler - a flag and the name, in the
 *  marker's colour - from ProjectMarkers. A child of the ruler that follows its size and repaints
 *  with it; transparent to the mouse, so the ruler keeps every gesture it had. */
class LMMS_EXPORT MarkerRulerOverlay : public QWidget
{
	Q_OBJECT
public:
	explicit MarkerRulerOverlay(TimeLineWidget* ruler);

	//! The x of each marker drawn on the last paint (a test's view of it).
	QList<int> drawnMarkers() const { return m_drawn; }

protected:
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void paintTempo(QPainter& p);

	QPointer<TimeLineWidget> m_ruler;
	QList<int> m_drawn;
};

/*! ED-011: the time range (ArrangerRange) drawn over the tracks it covers, and the arranger's own
 *  tools beyond the song editor's: RANGE (press, drag across time and tracks, release: range.set,
 *  snapped), AUDITION (ED-009: transport.audition_start while held, from the pressed point, the
 *  pressed track alone), ZOOM (ED-008: the dragged span fills the width - arrangement.zoom) and
 *  SMART (ED-010: a clip's upper half draws a range, its lower half moves and trims as ever - the
 *  clip hands the press over, ClipView::mousePressEvent). Transparent to the mouse except while a
 *  tool of its own is chosen, so every clip gesture is the song editor's. */
class LMMS_EXPORT ArrangerRangeOverlay : public QWidget
{
	Q_OBJECT
public:
	enum class Tool { None, Range, Audition, Zoom, Smart };

	explicit ArrangerRangeOverlay(SongEditor* editor);

	void setTool(Tool tool);
	Tool tool() const { return m_tool; }
	bool toolActive() const { return m_tool == Tool::Range || m_tool == Tool::Audition || m_tool == Tool::Zoom; }
	//! SMART: a clip hands a press in its upper half over; the overlay takes the drag from there.
	void beginRangeFrom(const QPoint& globalPos);
	//! The viewport rectangle the range covers, empty with no range (a test's view of it).
	QRect rangeRect() const;
	//! The tick and the track at a viewport point (the gesture's mapping; public for tests).
	tick_t tickAt(int x) const;
	lmms::Track* trackAt(int y) const;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	int contentLeft() const;
	void dispatchRange(const QPoint& to);
	void finishGesture(const QPoint& to);

	QPointer<SongEditor> m_editor;
	Tool m_tool = Tool::None;
	bool m_dragging = false;
	QPoint m_from;
};

//! Put the arranger's overlays on the song editor once (the shell calls it at start-up).
LMMS_EXPORT void installArrangerOverlays(SongEditor* editor);

} // namespace lmms::gui

#endif // LMMS_GUI_ARRANGER_OVERLAYS_H
