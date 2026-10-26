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

#include <algorithm>
#include <array>
#include <vector>

#include <QString>

#include "lmms_export.h"

namespace lmms
{
class TempoMap;
struct TempoMapTimeSignature;
}

namespace lmms::gui
{

//! Which key rows the editor shows. Folding hides rows, never notes: a note whose key is
//! folded away is still in the clip, and unfolding shows it again.
enum class NoteFold
{
	None,  //!< all 128 keys
	Scale, //!< only the keys whose pitch class is in the context's scale
	Used,  //!< only the keys the clip has notes on (plus the ones being drawn)
	Drums  //!< the keys the track names (key_names.*) and the used ones: the drum lane view
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
	//! Clamped to [minRowHeight, 64].
	void setRowHeight(int px);
	//! 0.5.0 Q050-141: the least a row may shrink to (6 px by default; accessible mode raises it to a touch
	//! target plus the gap to the next row, so vertical zoom cannot shrink notes below the touch minimum).
	int minRowHeight() const { return m_minRowHeight; }
	void setMinRowHeight(int px) { m_minRowHeight = std::clamp(px, 6, 64); }

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
	//! The keys the track names, for NoteFold::Drums.
	void setNamedKeys(const std::vector<int>& keys);
	//! @a key, or - when it is outside the scale and a scale is set - the nearest key inside it.
	int nearestInScale(int key) const;
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

	// --- metre (0.5.0 W2) --------------------------------------------------------------
	//! A stretch of the clip's timeline in one metre: from @a start (clip ticks) on, bars of @a barTicks
	//! split into beats of @a beatTicks; @a firstBar is the number of the bar at @a start, counted from
	//! the clip's first bar (0).
	struct MeterSpan
	{
		int start;
		int barTicks;
		int beatTicks;
		int firstBar;
	};
	//! One bar: where it starts (clip ticks), how long it is (shorter than barTicks when a metre change
	//! cuts it), its beat, and its number from the clip's first bar (0).
	struct Bar
	{
		int tick;
		int length;
		int barTicks;
		int beatTicks;
		int index;
	};
	/*! The song's metre as a clip starting at song tick @a clipStart sees it: the metre in force there
	 *  (@a global unless @a map changes it before), then each of the map's metre changes from its own
	 *  tick on - a change starts a bar. Bars count from the clip's start, which is where bar 1 begins.
	 *  An inactive map changes nothing; without a call the grid is 4/4. */
	void setMeter(const TempoMapTimeSignature& global, const TempoMap& map, int clipStart);
	const std::vector<MeterSpan>& meter() const { return m_meter; }
	//! The bars that reach into [@a from, @a to] - the one holding @a from too - in order.
	std::vector<Bar> barsBetween(double from, double to) const;
	//! The beat, in ticks, of the metre in force at @a tick.
	int beatTicksAt(int tick) const;

	static bool isBlackKey(int key);
	static QString keyName(int key);  //!< "A4", "F#3": the engine's numbering, key 0 = C0, key 57 = A4

private:
	void rebuildRows();
	bool shows(int key) const;  //!< whether the fold keeps @a key's row
	void clampScrollY();

	double m_pixelsPerTick = 0.25;  // 48 px per quarter note, 192 px per bar at 4/4
	int m_rowHeight = 14;
	int m_minRowHeight = 6;
	double m_scrollTicks = 0.0;
	int m_scrollY = 0;
	int m_width = 800;
	int m_height = 400;
	NoteFold m_fold = NoteFold::None;
	std::array<bool, 12> m_scaleMask{};
	int m_scaleRoot = 0;
	std::array<bool, 128> m_used{};
	std::array<bool, 128> m_named{};
	std::vector<int> m_rows;
	std::array<int, 128> m_rowOfKey{};
	std::vector<MeterSpan> m_meter{{0, 192, 48, 0}};  // 4/4 until setMeter
};

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_GRID_H
