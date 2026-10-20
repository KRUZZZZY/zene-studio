/*
 * ControlTrackCreation.h - one undo step for a track a command created
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
#ifndef LMMS_CONTROL_TRACK_CREATION_H
#define LMMS_CONTROL_TRACK_CREATION_H

#include "lmms_export.h"

namespace lmms
{

class Track;
class TrackContainer;

namespace control
{

/*! Record one undo step for @a track, which the command running has just created: undo removes it,
 *  redo builds it again from what it was saved as (so a redo after other edits gets an equal track,
 *  not a dangling pointer). Used by clip.slice_to_instrument and clip.audio_to_midi. */
LMMS_EXPORT void recordCreatedTrack(Track* track);

/*! The same step for a track created in @a container rather than the song: a Pattern Editor copy lives in the
 *  PatternStore. Undo removes it from @a container and redo builds it there, never in the song. */
LMMS_EXPORT void recordCreatedTrack(Track* track, TrackContainer* container);

/*! The live track whose stable id (Track::id) is @a id, or nullptr when none has it any more. Undo steps hold a
 *  track's id and look it up here when they run - never a Track* another step may already have freed (AGENTS.md). */
LMMS_EXPORT Track* trackWithId(int id);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_TRACK_CREATION_H
