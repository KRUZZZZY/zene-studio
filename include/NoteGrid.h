/*
 * NoteGrid.h - the note editor's geometry: zoom, scroll, folded key rows, snapping, hit tests
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

#ifndef LMMS_GUI_NOTE_GRID_H
#define LMMS_GUI_NOTE_GRID_H

#include <array>
#include <vector>

#include <QString>

#include "lmms_export.h"

namespace lmms::gui
{

//! Which key rows the editor shows. Folding hides rows, never notes: a note whose key is
//! folded away is still in the clip, and unfolding shows it again.
enum class NoteFold
{
	None,  //!< all 128 keys
	Scale, //!< only the keys whose pitch class is in the context's scale
	Used   //!< only the keys the clip has notes on (plus the ones being drawn)
};

//! The note editor's coordinate system, with no widget: everything a paint or a mouse handler
//! needs to turn ticks and keys into pixels and back. NoteGridTest proves it without a display.
//!
//! x: ticks scaled by pixelsPerTick, minus the horizontal scroll (in ticks, fractional, so a
//! zoom anchored under the cursor is smooth). y: one row per VISIBLE key, highest key at the
//! top, minus the vertical scroll (in pixels).
class LMMS_EXPORT NoteGrid
{
public:
	NoteGrid();

	// --- zoom and scroll --------------------------------------------------------------
	double pixelsPerTick() const { return m_pixelsPerTick; }
	//! Clamped to [minPixelsPerTick, maxPixelsPerTick].
	void setPixelsPerTick(double ppt);
	//! Zoom by @a factor keeping tick-under-@a anchorX fixed on screen (smooth zoom).
	void zoomAround(double factor, double anchorX);
	static constexpr double minPixelsPerTick = 0.02;  // a 64-bar clip fits in ~2400 px
	static constexpr double maxPixelsPerTick = 8.0;

	int rowHeight() const { return m_rowHeight; }
	void setRowHeight(int px);

	double scrollTicks() const { return m_scrollTicks; }
	void setScrollTicks(double ticks);
	int scrollY() const { return m_scrollY; }
	void setScrollY(int px);

	//! The visible area's size, for clamping the vertical scroll and centring.
	void setViewport(int width, int height);
	int viewportWidth() const { return m_width; }
	int viewportHeight() const { return m_height; }

	// --- rows -------------------------------------------------------------------------
	//! A 12-entry pitch-class mask (index 0 = C) for NoteFold::Scale and highlighting.
	void setScaleMask(const std::array<bool, 12>& mask, int root);
	const std::array<bool, 12>& scaleMask() const { return m_scaleMask; }
	int scaleRoot() const { return m_scaleRoot; }
	bool inScale(int key) const;

	//! The keys that carry notes, for NoteFold::Used.
	void setUsedKeys(const std::vector<int>& keys);
	//! Keeps the key at the centre of the view centred (or the nearest shown key).
	void setFold(NoteFold fold);
	//! Scroll so @a key's row - or the nearest shown row - is in the middle of the view.
	void centreOnKey(int key);
	NoteFold fold() const { return m_fold; }

	//! Visible keys, top row first (descending pitch).
	const std::vector<int>& rows() const { return m_rows; }
	int rowCount() const { return static_cast<int>(m_rows.size()); }
	//! The row index of @a key, or -1 when it is folded away.
	int rowOfKey(int key) const;
	//! The key under @a y, or -1 outside the rows.
	int keyAtY(int y) const;
	//! The top pixel of @a key's row, or a large negative number when folded away.
	int yOfKey(int key) const;
	int contentHeight() const { return rowCount() * m_rowHeight; }

	// --- time -------------------------------------------------------------------------
	double xOfTick(double tick) const;
	double tickAtX(double x) const;
	//! The first and last tick on screen (the last exclusive).
	double firstVisibleTick() const { return m_scrollTicks; }
	double lastVisibleTick() const;

	//! Snap a tick to the grid: round to the nearest multiple of @a gridTicks (floor for a
	//! draw start, so a click inside a cell starts the note at the cell). gridTicks <= 1 = off.
	static int snap(int tick, int gridTicks);
	static int snapDown(int tick, int gridTicks);

	//! The spacing, in ticks, of the lightest grid lines worth drawing at this zoom: the snap
	//! grid when its cells are at least @a minPixels wide, else the next coarser power of two.
	int visibleGridTicks(int gridTicks, int minPixels = 6) const;

	static bool isBlackKey(int key);
	static QString keyName(int key);  //!< "A4", "F#3": the engine's numbering, key 0 = C0, key 57 = A4

private:
	void rebuildRows();
	void clampScrollY();

	double m_pixelsPerTick = 0.25;  // 48 px per beat, 192 px per bar at 4/4
	int m_rowHeight = 14;
	double m_scrollTicks = 0.0;
	int m_scrollY = 0;
	int m_width = 800;
	int m_height = 400;
	NoteFold m_fold = NoteFold::None;
	std::array<bool, 12> m_scaleMask{};
	int m_scaleRoot = 0;
	std::array<bool, 128> m_used{};
	std::vector<int> m_rows;
	std::array<int, 128> m_rowOfKey{};
};

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_GRID_H
