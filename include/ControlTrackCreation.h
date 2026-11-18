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

#include <QVector>

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

/*! One track an action created: the track, the container it was created in (the song, or the PatternStore), and its
 *  saved document. Build it with captureCreatedTrack() as soon as the track is made. A Song pattern track saves the
 *  PatternStore it owns, so a document taken after a PatternStore copy exists would bring that copy back on redo a
 *  second time (the copy has its own record). */
struct CreatedTrack
{
	Track* track;
	TrackContainer* container;
	QString saved;
};

/*! @a track as it stands now, saved for an undo record (see CreatedTrack). */
LMMS_EXPORT CreatedTrack captureCreatedTrack(Track* track, TrackContainer* container);

/*! One undo step for every track in @a created, which one action made, in the order it made them. Undo removes
 *  them last-made first; redo builds them again in that order, so a track another one depends on comes back after
 *  it. A Pattern Editor copy into an empty Song is one such group: the Song's pattern track, then the copy. */
LMMS_EXPORT void recordCreatedTracks(const QVector<CreatedTrack>& created);

/*! The live track whose stable id (Track::id) is @a id, or nullptr when none has it any more. Undo steps hold a
 *  track's id and look it up here when they run - never a Track* another step may already have freed (AGENTS.md). */
LMMS_EXPORT Track* trackWithId(int id);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_TRACK_CREATION_H
