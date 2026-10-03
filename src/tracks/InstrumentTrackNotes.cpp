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

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiEvent.h"
#include "MidiPlayback.h"
#include "NoteRandom.h"
#include "PatternTrack.h"

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

namespace
{

MidiEvent controllerEvent(const ControllerLane& lane, int value)
{
	switch (lane.kind)
	{
	case ControllerKind::PitchBend: return MidiEvent(MidiPitchBend, 0, static_cast<int16_t>(value));
	case ControllerKind::ChannelPressure: return MidiEvent(MidiChannelPressure, 0, static_cast<int16_t>(value));
	case ControllerKind::ProgramChange: return MidiEvent(MidiProgramChange, 0, static_cast<int16_t>(value));
	case ControllerKind::ControlChange: break;
	}
	return MidiEvent(MidiControlChange, 0, static_cast<int16_t>(lane.controller), static_cast<int16_t>(value));
}

} // namespace

bool InstrumentTrack::handleInputTransform(const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	// MIDI input thread. The transformed event re-enters processInEvent once; the flag keeps it from
	// being transformed twice (a thread's own re-entry, so a thread-local).
	thread_local bool inside = false;
	if (inside || event.source() != MidiEvent::Source::External || !m_inputTransform.active()) { return false; }
	MidiEvent transformed = event;
	if (m_inputTransform.apply(&transformed))
	{
		inside = true;
		processInEvent(transformed, time, offset);
		inside = false;
	}
	return true;
}

void InstrumentTrack::playClipControllers(const MidiClip* clip, tick_t tick, f_cnt_t offset)
{
	// Audio thread, under play()'s tryLock: reads the lanes, writes only their own lastSent/lastTick,
	// allocates nothing.
	for (const ControllerLane& lane : clip->controllers().lanes())
	{
		const int value = lane.valueAt(tick);
		const bool jumped = tick != lane.lastTick + 1;
		lane.lastTick = tick;
		if (!jumped && value == lane.lastSent) { continue; }
		lane.lastSent = value;
		processOutEvent(controllerEvent(lane, value), TimePos(tick), offset);
	}
}

bool InstrumentTrack::noteStartsNow(const Note& note, const TimePos& curStart, const MidiClip* clip) const
{
	if (note.pos() == curStart) { return true; }
	if (!(note.pos() < curStart && note.endPos() > curStart)) { return false; }
	// A note under the clip's own start (a clip trimmed from the left) always sounds from there; one
	// under a start, a jump or a loop does when chase is on (0.5.0 ED-075, MidiPlayback.h).
	const MidiPlayback& playback = MidiPlayback::instance();
	return curStart == -clip->startTimeOffset() || (playback.chase() && playback.chasingNow());
}

bool InstrumentTrack::notePlaysNow(const Note& note, uint32_t midiSeed) const
{
	// MIDI depth: note probability. The decision is taken here, once per note trigger at scheduling
	// time - never in the per-frame loop - and it is a pure function of the project seed and the
	// note's identity, so it neither allocates nor locks on the audio thread. A note that keeps the
	// default probability of 1 returns before the roll is even computed, which is what keeps a
	// project that does not use this feature bit-identical to before.
	//
	// Consequence of the stateless roll, stated deliberately: the result is stable across passes, so
	// a 50% note either plays in every repeat of the clip or in none of them; the seed is what
	// re-rolls the take. A note's recurrence (0.5.0 ED-133) is what varies it by pass.
	return !note.muted() && note.recursOnPass(MidiPlayback::instance().pass())
		&& NoteRandom::passesProbability(note.probability(), midiSeed, note.key(), note.pos(), note.length());
}

void InstrumentTrack::sendArticulation(int index, f_cnt_t offset)
{
	// 0.5.0 ED-121: what the note's articulation sends before it - read under play()'s lock, no allocation.
	const Articulation* articulation = m_articulations.at(index);
	if (articulation == nullptr) { return; }
	// A program change or a controller goes out in this block (a MIDI event has no carry-over to a later
	// one), so it reaches the instrument no later than the note it prepares, however far that is delayed.
	const f_cnt_t now = std::min(offset, Engine::audioEngine()->framesPerPeriod() - 1);
	const TimePos time = TimePos::fromFrames(now, Engine::framesPerTick());
	if (articulation->program >= 0) { processOutEvent(MidiEvent(MidiProgramChange, 0, static_cast<int16_t>(articulation->program)), time, now); }
	if (articulation->controller >= 0)
	{
		processOutEvent(MidiEvent(MidiControlChange, 0, static_cast<int16_t>(articulation->controller),
			static_cast<int16_t>(articulation->value)), time, now);
	}
	if (articulation->keyswitch >= 0)
	{
		// The keyswitch is a play handle, which waits out a delay itself; one note, made once, re-keyed.
		static Note key(TimePos(1), TimePos(0), DefaultKey);
		key.setKey(articulation->keyswitch);
		Engine::audioEngine()->addPlayHandle(NotePlayHandleManager::acquire(this, offset,
			static_cast<f_cnt_t>(Engine::framesPerTick()), key));
	}
}

void InstrumentTrack::triggerNote(const NoteTrigger& trigger, f_cnt_t offset, f_cnt_t frames, const TimePos& curStart)
{
	const Note& note = **trigger.note;
	// Swing and the MIDI offset (0.5.0 ED-142 / ED-076) delay a note that starts here - not one chased.
	if (note.pos() == curStart)
	{
		const tick_t clipStart = trigger.songGlobal ? trigger.clip->startPosition().getTicks() + trigger.clip->startTimeOffset().getTicks() : 0;
		offset += MidiPlayback::instance().delayFrames(clipStart + note.pos().getTicks(), Engine::framesPerTick(),
			Engine::audioEngine()->outputSampleRate());
	}
	sendArticulation(note.conditions().articulation, offset);
	// 0.5.0 ED-133: the note's repeats - that many more hits, evenly spaced inside it (not a step note's).
	const int hits = frames > 0 ? static_cast<int>(std::min<f_cnt_t>(note.conditions().repeats + 1, frames)) : 1;
	const f_cnt_t each = frames / hits;
	for (int hit = 0; hit < hits; ++hit)
	{
		NotePlayHandle* handle = NotePlayHandleManager::acquire(this, offset + hit * each, hits > 1 ? each : frames, note);
		handle->setPatternTrack(trigger.patternTrack);
		// Velocity jitter, the slide source, the song-global offset and the clip's transpose.
		decorateNoteHandle(handle, trigger.clip, trigger.note, trigger.first, trigger.songGlobal, trigger.midiSeed);
		Engine::audioEngine()->addPlayHandle(handle);
	}
}

TimePos InstrumentTrack::loopedPosition(const MidiClip* clip, const TimePos& position)
{
	const int loop = clip->clipEdits().loopTicks;
	return loop > 0 && position >= 0 ? TimePos(position.getTicks() % loop) : position;
}

} // namespace lmms
