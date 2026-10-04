/*
 * SampleFrameEdits.h - frame-exact splits and trims of sample clips, and zero-crossing snap
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

#ifndef LMMS_SAMPLE_FRAME_EDITS_H
#define LMMS_SAMPLE_FRAME_EDITS_H

#include <QString>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Clip;
class SampleClip;

/*! 0.5.0 ED-021: edits at a source FRAME rather than a tick. A tick is several hundred frames, so a
 *  cut at a zero crossing cannot be a tick position. The lightest model that holds one: the clip's
 *  window (SampleWindow, already in frames) takes the cut frame, the clip's ticks are the whole ticks
 *  around it, and the sub-tick remainder of a moved start goes into SampleClip::leadFrames(), which
 *  the playback path turns into the play handle's offset. So:
 *  - a tail trim at frame F ends the window at F and the clip at the first tick at or after F;
 *  - a head trim at frame F starts the window at F, the clip at the last tick at or before F, and
 *    leads by the frames between that tick and F;
 *  - a split is both, on the clip and on its clone, which therefore overlap by less than one tick on
 *    the timeline while their audio meets exactly at F.
 *  Linear clips only (no warp markers, the project's tempo): elsewhere the frame-to-tick map is not a
 *  straight line, and editable() says so. UI-thread code; nothing here runs on the audio thread. */
namespace arrangement
{

//! The window zero-crossing snap searches on each side of the frame it is given.
constexpr int ZeroCrossingWindowMs = 10;

//! Whether @a clip takes frame-exact edits; the reason in @a why when it does not.
LMMS_EXPORT bool frameEditable(const SampleClip& clip, QString* why);

//! The source frame of @a clip heard at @a tick plus @a outputFrames engine frames (exact, sub-tick),
//! clamped into the clip's source.
LMMS_EXPORT f_cnt_t frameAtTimeline(const SampleClip& clip, tick_t tick, f_cnt_t outputFrames);

//! The frame nearest @a frame, within ZeroCrossingWindowMs on either side, where the clip's audio (its
//! channels summed, as it plays - reversed included) crosses zero; @a frame itself when none does.
LMMS_EXPORT f_cnt_t nearestZeroCrossing(SampleClip& clip, f_cnt_t frame);

//! Whether @a clip can be split at source frame @a frame: frame-editable, and the frame strictly inside
//! the audio it plays and inside its ticks. The reason in @a why when not.
LMMS_EXPORT bool canSplitAtFrame(const SampleClip& clip, f_cnt_t frame, QString* why);

//! Split @a clip at source frame @a frame; the new right half, or nullptr (and @a why) when
//! canSplitAtFrame() says no. The caller checkpoints the track first.
LMMS_EXPORT SampleClip* splitAtFrame(SampleClip* clip, f_cnt_t frame, QString* why);

//! Move @a clip's start to source frame @a frame (it may reveal audio before the window); false (and
//! @a why) when that start would not be before the clip's end. With @a checkOnly nothing is written, so
//! a caller can refuse before it checkpoints.
LMMS_EXPORT bool trimHeadToFrame(SampleClip* clip, f_cnt_t frame, QString* why, bool checkOnly = false);

//! End @a clip's audio at source frame @a frame and the clip at the tick after; false (and @a why) when
//! that end would not be after the clip's start. @a checkOnly as for trimHeadToFrame().
LMMS_EXPORT bool trimTailToFrame(SampleClip* clip, f_cnt_t frame, QString* why, bool checkOnly = false);

/*! The arranger knife (ClipView::splitClip) with zero-crossing snap on (Modes::snapToZeroCrossings):
 *  @a clip split at the zero crossing nearest @a position, as one undo step (the track's checkpoint).
 *  False - and nothing done - when the mode is off, @a clip is not a frame-editable sample clip, or
 *  the snapped frame is not inside it; the knife then makes its ordinary tick split. */
LMMS_EXPORT bool splitAtZeroCrossing(Clip* clip, tick_t position);

/*! The arranger's drag release (ClipView) with zero-crossing snap on: when @a resized, the edge just
 *  dragged - the start when @a head, else the end - moved frame-exact to the nearest zero crossing, inside
 *  the drag's own undo step (its checkpoint was taken at the press). Nothing for a move, when the mode is
 *  off, or when @a clip is not a frame-editable sample clip. */
LMMS_EXPORT void snapEdgeToZeroCrossing(Clip* clip, bool resized, bool head);

} // namespace arrangement

} // namespace lmms

#endif // LMMS_SAMPLE_FRAME_EDITS_H
