/*
 * ChordFollow.h - 0.5.0 E23: playback reads the chord track - a note of a track that follows it sounds on
 *                 the nearest tone of the chord sounding where the note starts
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
 */

#ifndef LMMS_CHORD_FOLLOW_H
#define LMMS_CHORD_FOLLOW_H

#include "ChordTrack.h"
#include "Engine.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "Track.h"

namespace lmms::chordfollow
{

/*! AUDIO THREAD (InstrumentTrack::decorateNoteHandle, arrangement playback, after the clip's transpose):
 *  moves @a handle to the key the song's chord track gives a note of @a track starting at song tick
 *  @a songTick (ChordTrack::followedKey, which chord.follow_set turns on per track). A track that does not
 *  follow, or a stretch where the chord track is silent, leaves the handle as it is. The play handle's own
 *  copy of the note moves, never the clip's note - the clip keeps the keys it was written with. Reads
 *  integers: no allocation, no lock. */
inline void apply(NotePlayHandle& handle, const Track& track, tick_t songTick)
{
	const Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	const int key = song->chordTrack().followedKey(track.id(), handle.key(), songTick);
	if (key == handle.key()) { return; }
	handle.setKey(key);
	handle.setFrequencyUpdate();
}

} // namespace lmms::chordfollow

#endif // LMMS_CHORD_FOLLOW_H
