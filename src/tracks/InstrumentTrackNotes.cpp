/*
 * InstrumentTrackNotes.cpp - what a fresh note play handle gets beyond its note
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

/*! Split out of InstrumentTrack::play (0.5.0, for the file-length ratchet): the per-note
 *  decorations, moved here unchanged - velocity jitter, the slide source, the song-global offset -
 *  and the clip's non-destructive transpose (ED-038). Audio thread, called once per note trigger:
 *  no allocation, no lock. */

#include <algorithm>

#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "NoteRandom.h"

namespace lmms
{

void InstrumentTrack::decorateNoteHandle(NotePlayHandle* handle, const MidiClip* clip, NoteVector::const_iterator note,
	NoteVector::const_iterator first, bool songGlobal, uint32_t midiSeed) const
{
	const Note* currentNote = *note;
	// MIDI depth: velocity jitter. Applied to the play handle's own
	// copy of the note, never to the persisted one, and only when the
	// note asks for it.
	if( currentNote->velocityJitter() > 0.f )
	{
		const float factor = NoteRandom::velocityFactor( currentNote->velocityJitter(), midiSeed,
				currentNote->key(), currentNote->pos(), currentNote->length() );
		handle->setVolume( static_cast<volume_t>( std::clamp(
			currentNote->getVolume() * factor,
			static_cast<float>( MinVolume ), static_cast<float>( MaxVolume ) ) ) );
	}
	// 0.5.0 ED-038: the clip's transpose, on the play handle's own copy - the notes in the clip
	// keep their keys. The slide source moves with it, so a glide keeps its interval.
	const int transpose = clip->clipEdits().transpose;
	if( transpose != 0 )
	{
		handle->setKey( std::clamp( currentNote->key() + transpose, 0, NumKeys - 1 ) );
		handle->setFrequencyUpdate();
	}
	// Slide notes glide from the nearest preceding note in this clip
	// (SPEC-slide-notes D-2; no preceding note -> no glide)
	if( currentNote->slide() && note != first )
	{
		handle->setSlideSourceKey( std::clamp( ( *( note - 1 ) )->key() + transpose, 0, NumKeys - 1 ) );
	}
	// are we playing global song?
	if( songGlobal )
	{
		// then set song-global offset of clip in order to
		// properly perform the note detuning
		handle->setSongGlobalParentOffset( clip->startPosition() + clip->startTimeOffset());
	}
}

} // namespace lmms
