/*
 * FolderSummaryLane.h - a folder track's row draws its children's clips as summary clips
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

#ifndef LMMS_GUI_FOLDER_SUMMARY_LANE_H
#define LMMS_GUI_FOLDER_SUMMARY_LANE_H

#include <vector>

#include <QPoint>
#include <QWidget>

#include "FolderSummary.h"
#include "lmms_export.h"

class QPainter;

namespace lmms
{
class TrackFolder;
}

namespace lmms::gui
{

class TrackView;

/*! 0.5.0 ED-035: a folder track's row shows one summary clip per stretch of the song where its
 *  children hold clips (folderSummary), each carrying a miniature of the clips it stands for - so a
 *  collapsed folder still shows where its music is. A summary clip is edited as one:
 *
 *  - drag it to move every clip inside it (track.folder_move with the span);
 *  - Ctrl+drag to copy them there (range.copy then range.paste over the span's tracks);
 *  - its menu duplicates it right after itself (the same copy) or deletes it (range.delete).
 *
 *  Every edit is ONE existing command that checkpoints the tracks it touches, so it is one undo step.
 *  The arranger's range (interface state) is put back as it was after a copy or a delete.
 *
 *  The lane lies over the folder's TrackContentWidget; a press or a menu anywhere but on a summary clip
 *  is passed on to that widget, as it was before. */
class LMMS_EXPORT FolderSummaryLane : public QWidget
{
	Q_OBJECT

public:
	FolderSummaryLane(TrackFolder* folder, TrackView* view);

	//! The folder's summary clips, as they are now.
	std::vector<FolderSpan> spans() const;
	//! Where summary clip @a span lies in this lane.
	QRect spanRect(const FolderSpan& span) const;
	//! The index of the summary clip under @a pos, or -1.
	int spanAt(const QPoint& pos) const;

	//! The three edits, each one command (one undo step); false when the command refused.
	bool moveSpan(const FolderSpan& span, tick_t delta);
	bool copySpan(const FolderSpan& span, tick_t position);
	bool deleteSpan(const FolderSpan& span);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	int xOfTick(tick_t tick) const;
	//! @a dx pixels as ticks, on the song editor's snap unless @a free.
	tick_t ticksOf(int dx, bool free) const;
	void paintSpan(QPainter& p, const FolderSpan& span, int dx, bool dragged) const;
	void paintMiniature(QPainter& p, const FolderSpan& span, const QRect& rect) const;
	//! Repaint when the children's clips changed (a move elsewhere, an undo, the socket).
	void watch();

	TrackFolder* m_folder;
	TrackView* m_view;
	std::vector<FolderSpan> m_seen;
	int m_dragged = -1;     //!< the summary clip being dragged, or -1
	QPoint m_pressPos;
	int m_dragDx = 0;
	bool m_copy = false;    //!< Ctrl was held at the press: the drag copies
};

} // namespace lmms::gui

#endif // LMMS_GUI_FOLDER_SUMMARY_LANE_H
