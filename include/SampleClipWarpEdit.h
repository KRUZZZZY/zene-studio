/*
 * SampleClipWarpEdit.h - warp markers over a sample clip's waveform: the guides, the hit test, the snap and the
 *                        approximate waveform preview of a marker being dragged (Q050-063)
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

#ifndef LMMS_GUI_SAMPLE_CLIP_WARP_EDIT_H
#define LMMS_GUI_SAMPLE_CLIP_WARP_EDIT_H

#include <vector>

#include <QColor>
#include <QPoint>

#include "LmmsTypes.h"
#include "SampleClipOverlays.h"
#include "WarpMarkers.h"
#include "lmms_export.h"

class QPainter;

namespace lmms::gui
{

//! What a dragged warp marker snaps its timeline position to.
enum class WarpSnap
{
	Off,
	Grid,      //!< the arrangement's snap grid
	Transient  //!< the nearest detected transient, when one is within reach
};

//! One warp marker as the clip view shows it.
struct WarpGuide
{
	f_cnt_t sourceFrame = 0;  //!< the frame of the audio the marker pins (its identity for warp.move / warp.remove)
	tick_t offsetTicks = 0;   //!< where that frame lands, from the clip's warp origin (the warp.list value)
	int tick = 0;             //!< the same position in ticks from the clip's left edge, which is where it is drawn
};

/*! A stretch of the waveform and the pixels it fills: source fractions [start, end) of the whole sample buffer
 *  drawn into x0..x1. Between two markers the audio keeps one rate, so each stretch is drawn at its own width -
 *  that is the "approximate preview" of a warp. */
struct WarpWaveformSegment
{
	int x0 = 0;
	int x1 = 0;
	double start = 0.0;
	double end = 1.0;

	bool operator==(const WarpWaveformSegment& other) const
	{
		return x0 == other.x0 && x1 == other.x1 && start == other.start && end == other.end;
	}
};

/*! The markers of @a map as guides. @a originShiftTicks is the clip's startTimeOffset: the warp origin lies that
 *  many ticks from the clip's left edge, so a marker is drawn at offsetTicks + originShiftTicks. */
LMMS_EXPORT std::vector<WarpGuide> warpGuides(const WarpMarkers& map, int originShiftTicks);

//! Each guide as a line through the whole height of the waveform, with a grip at the top; @a active (an index, or
//! -1) is drawn heavier, the one being dragged or under the pointer.
LMMS_EXPORT void paintWarpGuides(QPainter& p, const std::vector<WarpGuide>& guides, const ClipOverlayFrame& frame,
	const QColor& colour, int active);

//! The index of the guide within a few pixels of @a pos (the nearest), or -1.
LMMS_EXPORT int warpGuideAt(const std::vector<WarpGuide>& guides, const ClipOverlayFrame& frame, const QPoint& pos);

//! The warp offset, in ticks, the pixel column @a x stands for (the inverse of warpGuides' placement).
LMMS_EXPORT tick_t warpOffsetAtX(int x, const ClipOverlayFrame& frame, int originShiftTicks);

/*! Where a marker dragged to @a wanted (a warp offset) lands once snapped, and kept strictly between its
 *  neighbours (@a low .. @a high, the engine's own rule for warp.move).
 *  @a originTicks is the timeline tick of warp offset 0 (the grid is the arrangement's, not the clip's);
 *  @a gridTicks the grid step; @a transientOffsets the transients as warp offsets and @a reachTicks how near one
 *  must be to take the marker. */
LMMS_EXPORT tick_t snapWarpOffset(tick_t wanted, WarpSnap snap, tick_t originTicks, int gridTicks,
	const std::vector<int>& transientOffsets, int reachTicks, tick_t low, tick_t high);

/*! The waveform stretches of a clip warped by @a map: the audio between sourceIn and sourceOut cut at every
 *  marker, each drawn from where its first frame lands to where its last one does. Empty when nothing is wide
 *  enough to draw. The clip view paints the committed map and a drag's candidate map through this one function,
 *  so what the drag showed is what the release leaves. */
LMMS_EXPORT std::vector<WarpWaveformSegment> warpWaveformSegments(const WarpMarkers& map, f_cnt_t sourceIn,
	f_cnt_t sourceOut, f_cnt_t bufferFrames, float framesPerTick, int originShiftTicks, const ClipOverlayFrame& frame);

} // namespace lmms::gui

#endif // LMMS_GUI_SAMPLE_CLIP_WARP_EDIT_H
