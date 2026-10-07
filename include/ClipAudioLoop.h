/*
 * ClipAudioLoop.h - a sample clip's content looped (and rotated) inside the clip
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

#ifndef LMMS_CLIP_AUDIO_LOOP_H
#define LMMS_CLIP_AUDIO_LOOP_H

#include <cstdint>

#include <QString>

#include "ClipEdits.h"
#include "SampleWindow.h"
#include "TimePos.h"
#include "lmms_export.h"

namespace lmms
{

class SampleClip;

/*! 0.5.0 ED-029 (audio): a sample clip's content looped inside the clip. The loop is a range of the clip's SOURCE
 *  frames (ClipEdits::audioLoopStart / audioLoopFrames - source frames, so a tempo change or a trim keeps it on the
 *  same audio); the clip plays from its in-point (plus ClipEdits::audioLoopOffset) as before, and once it reaches
 *  the loop's end it goes back to the loop's start, for as long as the clip is drawn. ED-028: rotating the content
 *  moves the offset, folded into the loop.
 *
 *  SampleTrack::play schedules a looped clip with window() - unfolded, so its length is the pass's length and the
 *  fades are measured from the right place - and SamplePlayHandle plays a copy of the clip's Sample with the loop's
 *  points in Sample::Loop::On, starting from the folded frame. Engaged only for a plain clip: one that renders
 *  linearly (no warp markers, no source tempo), at its own speed and pitch, not recording and on a track that is
 *  not comped. A clip without a loop - every clip of every older project - takes the old path exactly. */
namespace audioloop
{

//! Whether @a clip plays its audio loop.
LMMS_EXPORT bool engaged(const SampleClip& clip);
//! Why @a clip cannot loop its audio (a warped, transposed, sped or recording clip), or an empty string.
LMMS_EXPORT QString refusal(const SampleClip& clip);
//! Source frames from the clip's in-point to the timeline position @a at, unclamped (negative before the clip's
//! origin): the measure SampleClip::sourceFrameAt takes before it clamps into the window.
LMMS_EXPORT std::int64_t elapsedFrames(const SampleClip& clip, TimePos at);
//! The source frame the unfolded content position @a position plays: itself before the loop's end, folded into
//! the loop at and after it (never below 0).
LMMS_EXPORT std::int64_t fold(const ClipEdits& edits, std::int64_t position);
//! The window a pass beginning at @a at plays, UNFOLDED: the in-point plus the frames elapsed at @a at, to the
//! in-point plus the frames elapsed at the clip's end. Its frames may lie past the source; only its length and its
//! start's distance from the in-point are read (SamplePlayHandle::snapshotAudioLoop folds the start).
LMMS_EXPORT SampleWindow window(const SampleClip& clip, TimePos at);
/*! clip.set_loop on a sample clip: @a edits with a loop of @a lengthTicks starting @a startTicks after the in-point,
 *  measured at the clip's rate now and kept in source frames (0 clears the loop, and the offset with it). False,
 *  with @a error, for a loop that runs past the end of the clip's audio. */
LMMS_EXPORT bool withLoop(const SampleClip& clip, int startTicks, int lengthTicks, ClipEdits* edits, QString* error);
/*! ED-028, note.rotate on a sample clip: @a edits with the clip's content turned @a amountTicks later (negative:
 *  earlier), wrapping round inside its loop - or, for a clip with none, inside the clip, which becomes a loop over
 *  exactly the audio it plays. @a turned is false (and @a edits untouched) for a whole number of turns. False, with
 *  @a error, for a clip whose audio does not fill it (an unlooped clip longer than its audio, or audio that starts
 *  after the clip) and for one whose start plays before its loop (an intro does not repeat, so it cannot turn). */
LMMS_EXPORT bool rotated(const SampleClip& clip, int amountTicks, ClipEdits* edits, bool* turned, QString* error);

} // namespace audioloop

} // namespace lmms

#endif // LMMS_CLIP_AUDIO_LOOP_H
