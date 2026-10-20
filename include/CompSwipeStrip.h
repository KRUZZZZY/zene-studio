/*
 * CompSwipeStrip.h - comp by swiping: the take lanes as rows, a drag along one comps that range
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
#ifndef LMMS_GUI_COMP_SWIPE_STRIP_H
#define LMMS_GUI_COMP_SWIPE_STRIP_H

#include <vector>

#include <QJsonObject>
#include <QPointer>
#include <QWidget>

#include "LmmsTypes.h"
#include "TakeWaveform.h"
#include "lmms_export.h"

namespace lmms
{
class Clip;
class Track;
}

class QResizeEvent;
class QTouchEvent;

namespace lmms::gui
{

/*! 0.5.0 L03 (ED-165): comp by swiping. The track's take lanes are drawn as rows over the comp's
 *  span (the takes' span before there is a comp), each comp segment lit on the lane it plays; a
 *  drag along a lane runs comp.select for the range dragged on that lane, and a double-click runs comp.cycle at that tick (ED-168) - the
 *  registry's commands, so each swipe is one undo step. setState() takes comp.get_state's answer. */
/*! 0.5.0 theme 10: the strip without a mouse - Up / Down choose the lane, Left / Right move the cursor a beat
 *  (Ctrl: a bar), Shift+Left / Shift+Right mark a range from it, Home / End go to the span's ends; Enter or Space
 *  comps the range - or, with none, the comp segment under the cursor - to the cursor's lane (comp.select), C cycles
 *  the take there (comp.cycle), Escape drops the range, Ctrl+Shift+L opens the editable segment list. The cursor and
 *  the range are outlined, and a screen reader reads them with the lane the comp plays there. */
class LMMS_EXPORT CompSwipeStrip : public QWidget
{
	Q_OBJECT
public:
	struct Segment
	{
		int begin = 0;
		int end = 0;
		int lane = 0;
	};

	//! 0.5.0 L03 (Q050-123): one take clip's row in the strip: where its waveform is drawn, on the
	//! strip's own tick->pixel mapping. A test reads these to check a take's alignment.
	struct TakeBand
	{
		int lane = 0;
		int begin = 0;
		int end = 0;
		QRect rect;
		//! Guarded: an undo (or a project load) replaces a track's clips, and a paint may run
		//! before the next refresh has re-read them.
		QPointer<Clip> clip;

		bool operator==(const TakeBand&) const = default;
	};

	CompSwipeStrip(const QString& trackId, QWidget* parent);

	void setState(const QJsonObject& state);
	//! The tick at @a x and the lane at @a y (or -1), for the gesture and the test.
	int tickAt(int x) const;
	int laneAt(int y) const;
	QSize sizeHint() const override;

	//! What the editable segment list reads (theme 10).
	QString trackId() const { return m_trackId; }
	const std::vector<Segment>& segments() const { return m_segments; }
	const std::vector<int>& lanes() const { return m_lanes; }
	//! 0.5.0 L03 (Q050-123): each take clip's band, rebuilt from the track when the state is read.
	const std::vector<TakeBand>& takeBands() const { return m_takeBands; }
	//! The strip's tick-to-pixel mapping, for a test comparing a band with the ruler.
	int tickToX(int tick) const { return xOf(tick); }
	//! The keyboard's place: the lane, the tick, and the range's other end (-1: no range).
	int keyLane() const;
	int keyTick() const { return m_keyTick; }
	int keyAnchor() const { return m_keyAnchor; }
	QString accessibleValue() const;

signals:
	//! A swipe or a cycle ran; the panel refreshes and shows @a status.
	void ran(const QString& status);
	//! setState() took a new answer: the segment list re-reads.
	void stateChanged();

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	static constexpr int RowHeight = 22;

	int xOf(int tick) const;
	void paintKeyCursor(QPainter& p) const;
	bool handlesKey(const QKeyEvent* event) const;
	bool moveKey(const QKeyEvent* event);
	//! Where Left / Right (a beat, or a bar) and Home / End put the cursor; -1 for another key.
	int movedTick(int key, bool byBar) const;
	void compAtCursor();
	void cycleAtCursor();
	//! 0.5.0 L03 (Q050-274): a single-finger swipe; true when the event was a touch this strip took.
	bool touchEvent(QEvent* event);
	bool beginTouch(QTouchEvent* event);
	bool followTouch(QTouchEvent* event);
	void cancelTouch();
	//! Normalise, commit once through comp.select, and say what happened.
	void commitDrag(int lane, int from, int to);
	//! 0.5.0 L03 (Q050-123): take bands for the current state (also after a resize).
	void rebuildTakes();
	//! The comp's segment holding @a tick, or nullptr.
	const Segment* segmentAt(int tick) const;

	QString m_trackId;
	std::vector<int> m_lanes;
	std::vector<Segment> m_segments;
	int m_begin = 0;
	int m_end = 0;
	int m_dragLane = -1;
	int m_dragFrom = 0;
	int m_dragTo = 0;
	//! 0.5.0 L03 (Q050-274): the finger being followed; -1 when no touch sequence is live.
	int m_touchId = -1;
	bool m_touchActive = false;
	QPointer<Track> m_track;
	std::vector<TakeBand> m_takeBands;
	TakeWaveform m_waves;
	int m_keyRow = 0;       //!< theme 10: the lane row the keys act on
	int m_keyTick = 0;      //!< ... the cursor's tick
	int m_keyAnchor = -1;   //!< ... the marked range's other end, or -1
};

} // namespace lmms::gui

#endif // LMMS_GUI_COMP_SWIPE_STRIP_H
