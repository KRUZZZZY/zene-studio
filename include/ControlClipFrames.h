/*
 * ControlClipFrames.h - the frame-exact paths of clip.split and clip.trim (ED-021)
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

#ifndef LMMS_CONTROL_CLIP_FRAMES_H
#define LMMS_CONTROL_CLIP_FRAMES_H

#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"

namespace lmms::control::clipframes
{

/*! 0.5.0 ED-021: clip.split and clip.trim take the frame-exact path (SampleFrameEdits.h) when the clip is
 *  a sample clip and the call asks for it: an extra frame count after a tick ('frames' for a split,
 *  'start_frames' / 'end_frames' for a trim), or zero-crossing snap - 'zero_crossing', which defaults to
 *  arrangement.modes' snap_to_zero_crossings. Every other call takes the tick path, unchanged. */
bool wanted(const ClipRef& ref, const QJsonObject& args);

//! clip.split's frame-exact path: one undo step (the track's checkpoint), both halves' ids, the frame cut at.
ControlResult split(const ClipRef& ref, const QJsonObject& args);

//! clip.trim's frame-exact path: one undo step (the clip's checkpoint), the new edges and their frames.
ControlResult trim(const ClipRef& ref, const QJsonObject& args);

} // namespace lmms::control::clipframes

#endif // LMMS_CONTROL_CLIP_FRAMES_H
