/*
 * VideoLane.h - the arranger's video lane: a strip of thumbnails along the video's span
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

#ifndef LMMS_GUI_VIDEO_LANE_H
#define LMMS_GUI_VIDEO_LANE_H

#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class VideoFramePump;

namespace gui
{

class SongEditor;

/*! 0.5.0 ED-054, extended by Q050-139: under the arranger's ruler (and its ruler lanes), a thin strip of the
 *  project's video track - every clip's thumbnails tiled from the tick it starts at to the tick it ends at, through
 *  the tempo map, so the picture lines up with the music above the tracks. It takes room only while a clip is
 *  attached. The thumbnails (Thumbnails per clip, evenly through that clip) are decoded one at a time on a
 *  VideoFramePump's worker thread; a tile whose thumbnail has not arrived yet is a plain block. A click on a clip
 *  moves the play position there (transport.seek; a drag is told from a click on release), dragging a clip moves its
 *  song-tick start through video.set_offset, a double-click on empty lane space adds a clip (video.clip_add), and
 *  the context menu (Q050-180.1) adds a clip at the clicked tick, moves a clip's start to the play position,
 *  removes one, or renders the loop range's frames (video.render_frames). The lane reads VideoReference; every edit
 *  goes through the registered video.* commands. */
class LMMS_EXPORT VideoLane : public QWidget
{
	Q_OBJECT
public:
	static constexpr int Thumbnails = 24;
	static constexpr int LaneHeight = 30;

	explicit VideoLane(SongEditor* editor);

	//! How many thumbnails have arrived, across every clip (a test's view of the strip).
	int thumbnailsReady() const;
	//! The x range the clips' spans took on the last paint, first start to last end (empty when off screen).
	QPair<int, int> drawnSpan() const { return m_span; }
	//! The file dialog, then video.clip_add of the chosen file at song tick @a tick (the lane's double-click and
	//! context menu, and View > Add Video Clip... at the play position).
	static void addClipFromDialog(QWidget* parent, tick_t tick);

	//! A press that moves less than this many pixels before its release is a click (a seek), not a drag.
	static constexpr int DragThreshold = 3;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	//! The clip set changed: show or hide the lane, and start its thumbnails again.
	void reload();
	void take(const QImage& image, int tag);
	void askNext();
	void seekTo(int x);
	//! Run @a command; a refusal is shown in a warning titled @a title.
	void invokeOrWarn(const QString& command, const QJsonObject& args, const QString& title);
	//! The folder dialog, then video.render_frames over the song's loop range.
	void renderLoopRange();
	int clipAtX(int x) const;
	int xOf(tick_t tick) const;
	tick_t tickAt(int x) const;
	int contentLeft() const;
	void paintClippedStrip(QPainter& p, int clipIndex, const QRect& span, int startX);

	QPointer<SongEditor> m_editor;
	VideoFramePump* m_pump;
	QVector<QVector<QImage>> m_thumbnails;  //!< one row per clip, Thumbnails each
	QVector<QPair<int, int>> m_pending;     //!< (clip index, thumbnail index) not asked for yet
	QPair<int, int> m_asked;                //!< the one (clip index, thumbnail index) in flight
	int m_generation = 0;  //!< bumped by reload() and sent as the tag: an answer from before it is ignored
	QPair<int, int> m_span;
	QString m_draggingClip;  //!< the clip pressed on, until the release; empty otherwise
	int m_pressX = 0;
	tick_t m_dragStartOffset = 0;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_VIDEO_LANE_H
