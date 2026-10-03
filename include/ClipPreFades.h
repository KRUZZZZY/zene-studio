/*
 * ClipPreFades.h - an audio clip that sounds before its start and after its end (0.5.0 ED-179)
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
#ifndef LMMS_CLIP_PRE_FADES_H
#define LMMS_CLIP_PRE_FADES_H

#include "SampleWindow.h"
#include "TimePos.h"
#include "lmms_export.h"

namespace lmms
{

class SampleClip;

/*! 0.5.0 W3 (ED-179): pre-fades. A clip's fade-in may start before the clip and its fade-out end after it; the
 *  clip then plays its hidden source audio (the frames its trim hides) there. SampleTrack::play schedules the clip
 *  over its span with the pre-fades and SamplePlayHandle plays a copy of the clip's Sample with the widened window.
 *  Engaged only for a plain clip - one that renders linearly (no warp markers, no source tempo), at its own speed
 *  and pitch, not reversed, not recording and on a track that is not comped; any other clip plays its own span as
 *  before. A clip without pre-fades (every clip of every older project) takes the old arithmetic exactly. */
namespace prefades
{

//! Whether @a clip plays its pre-fades.
LMMS_EXPORT bool engaged(const SampleClip& clip);
//! The ticks of the fade-in before the clip that hidden audio fills: the pre-fade, no longer than the source
//! frames before the clip's in-point (with none, the ramp's silent part is not scheduled at all).
LMMS_EXPORT int leadTicks(const SampleClip& clip);
//! Where @a clip starts sounding: its start, @a leadTicks earlier when engaged.
LMMS_EXPORT TimePos spanStart(const SampleClip& clip);
//! Where @a clip stops sounding: its end, the pre-fade-out later when engaged.
LMMS_EXPORT TimePos spanEnd(const SampleClip& clip);
//! The source window a pass beginning at @a at plays: the clip's own (sourceFrameAt @a at to its end) unless
//! engaged, then reaching before its in-point and past its out-point, inside the sample.
LMMS_EXPORT SampleWindow window(const SampleClip& clip, TimePos at);

} // namespace prefades

} // namespace lmms

#endif // LMMS_CLIP_PRE_FADES_H
