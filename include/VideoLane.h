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

/*! 0.5.0 ED-054: under the arranger's ruler (and its ruler lanes), a thin strip of the project's video - its
 *  thumbnails tiled from the tick it starts at to the tick it ends at, through the tempo map, so the picture lines
 *  up with the music above the tracks. It takes room only while a video is attached. The thumbnails (Thumbnails
 *  of them, evenly through the video) are decoded one at a time on a VideoFramePump's worker thread; a tile whose
 *  thumbnail has not arrived yet is a plain block. A click on the strip moves the play position there (transport.seek). Interface
 *  only: it reads VideoReference and edits nothing. */
class LMMS_EXPORT VideoLane : public QWidget
{
	Q_OBJECT
public:
	static constexpr int Thumbnails = 24;
	static constexpr int LaneHeight = 30;

	explicit VideoLane(SongEditor* editor);

	//! How many thumbnails have arrived (a test's view of the strip).
	int thumbnailsReady() const;
	//! The x range the video's span took on the last paint (empty when it was off screen).
	QPair<int, int> drawnSpan() const { return m_span; }

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	//! The attachment changed: show or hide the lane, and start its thumbnails again.
	void reload();
	void take(const QImage& image, int tag);
	void ask(int index);
	int xOf(tick_t tick) const;
	tick_t tickAt(int x) const;
	int contentLeft() const;
	void paintStrip(QPainter& p, const QRect& span, int startX);

	QPointer<SongEditor> m_editor;
	VideoFramePump* m_pump;
	QVector<QImage> m_thumbnails;
	int m_generation = 0;  //!< bumped by reload(): an answer from before it is ignored
	QPair<int, int> m_span;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_VIDEO_LANE_H
