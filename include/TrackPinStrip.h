/*
 * TrackPinStrip.h - the rows of pinned folders, held above the scrolling track list
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

#ifndef LMMS_GUI_TRACK_PIN_STRIP_H
#define LMMS_GUI_TRACK_PIN_STRIP_H

#include <QHash>
#include <QList>
#include <QWidget>

#include "lmms_export.h"

class QVBoxLayout;

namespace lmms::gui
{

class TrackView;

/*! 0.5.0 E21: a pinned folder stays on screen while the track list scrolls. The industry precedent is
 *  Reaper's "pin track to top of arrange view": a pinned folder's row - its head in the track list and
 *  its clips in the arranger, one TrackView - sits in this strip, fixed above the track container's
 *  scroll area (and under the ruler and its lanes), in the container's track order. Its children stay
 *  in the scrolling list where they are, collapsing and hiding as before.
 *
 *  The scrolling list keeps a zero-height placeholder where each pinned row was, so its layout index
 *  of every row still equals the row's index in TrackContainerView's track list (moveTrackView inserts
 *  by that index). sync() is the one writer of both layouts: anything that left them out of step - a
 *  pin, an unpin, a move, a removed row - is put right by the next sync. Interface only: the pin is
 *  the folder's own persisted flag (track.set_pinned, the Folder menu's Pinned), nothing here edits it. */
class LMMS_EXPORT TrackPinStrip : public QWidget
{
public:
	explicit TrackPinStrip(QWidget* parent);

	//! Whether @a view is a pinned folder's row (and so belongs in the strip).
	static bool pins(const TrackView* view);

	/*! Puts each pinned folder's row of @a views in the strip and every other row in @a scrollLayout,
	 *  both in @a views order, with a placeholder in @a scrollLayout for each pinned row. A row's own
	 *  shown/hidden state is kept across the move; the strip shows while it holds a row that shows. */
	void sync(const QList<TrackView*>& views, QVBoxLayout* scrollLayout);

	//! The rows the strip holds, top to bottom (a test's view of it).
	QList<TrackView*> pinnedViews() const;

private:
	//! The placeholder standing for @a view in the scrolling list, made on first use.
	QWidget* placeholderFor(const TrackView* view);
	//! What @a scrollLayout should hold, in order: each row, or its placeholder when it is pinned.
	QList<QWidget*> scrollOrder(const QList<TrackView*>& views);
	//! Drops the placeholders of rows that are no longer pinned or no longer there.
	void dropStalePlaceholders(const QList<TrackView*>& views);
	void updateShown();

	QVBoxLayout* m_layout;
	QHash<const TrackView*, QWidget*> m_placeholders;
};

} // namespace lmms::gui

#endif // LMMS_GUI_TRACK_PIN_STRIP_H
