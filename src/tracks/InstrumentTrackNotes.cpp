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

#include <QDomElement>

#include "ChordFollow.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentOutputBuses.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiEvent.h"
#include "MidiPlayback.h"
#include "NoteRandom.h"
#include "PatternTrack.h"
#include "Song.h"

namespace lmms
{

static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

void InstrumentTrack::recordNoteOnActivity()
{
	m_noteOnGeneration.fetch_add(1, std::memory_order_relaxed);
	m_activeUiNotes.fetch_add(1, std::memory_order_relaxed);
}

void InstrumentTrack::recordNoteOffActivity()
{
	int activeNotes = m_activeUiNotes.load(std::memory_order_relaxed);
	while (activeNotes > 0 && !m_activeUiNotes.compare_exchange_weak(activeNotes, activeNotes - 1,
		std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

InstrumentTrack::NoteActivitySnapshot InstrumentTrack::noteActivitySnapshot() const
{
	return {m_activeUiNotes.load(std::memory_order_relaxed), m_noteOnGeneration.load(std::memory_order_relaxed)};
}

void InstrumentTrack::decorateNoteHandle(NotePlayHandle* handle, const MidiClip* clip, NoteVector::const_iterator note,
	NoteVector::const_iterator first, bool songGlobal, const TimePos& songOffset, uint32_t midiSeed) const
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
	// 0.5.0 E23: a track that follows the chord track plays the note on the chord's nearest tone (ChordFollow.h).
	if( songGlobal ) { chordfollow::apply( *handle, *this, songOffset.getTicks() + currentNote->pos().getTicks() ); }
	// Slide notes glide from the nearest preceding note in this clip
	// (SPEC-slide-notes D-2; no preceding note -> no glide)
	if( currentNote->slide() && note != first )
	{
		handle->setSlideSourceKey( std::clamp( ( *( note - 1 ) )->key() + transpose, 0, NumKeys - 1 ) );
	}
	// are we playing global song? then set the song-global offset of the clip (of the pass playing, on a
	// looped clip) in order to properly perform the note detuning
	if( songGlobal ) { handle->setSongGlobalParentOffset(songOffset); }
}

namespace
{

struct ActiveInputTransform
{
	const InstrumentTrack* track;
	const ActiveInputTransform* previous;
};

thread_local const ActiveInputTransform* s_activeInputTransforms = nullptr;

bool inputTransformIsActiveFor(const InstrumentTrack* track)
{
	for (const ActiveInputTransform* active = s_activeInputTransforms; active != nullptr; active = active->previous)
	{
		if (active->track == track) { return true; }
	}
	return false;
}

class InputTransformScope
{
public:
	explicit InputTransformScope(const InstrumentTrack* track) : m_context{track, s_activeInputTransforms}
	{
		s_activeInputTransforms = &m_context;
	}
	~InputTransformScope() { s_activeInputTransforms = m_context.previous; }

private:
	ActiveInputTransform m_context;
};

MidiEvent controllerEvent(const ControllerLane& lane, int value, int channel)
{
	switch (lane.kind)
	{
	case ControllerKind::PitchBend: return MidiEvent(MidiPitchBend, static_cast<int8_t>(channel), static_cast<int16_t>(value));
	case ControllerKind::ChannelPressure: return MidiEvent(MidiChannelPressure, static_cast<int8_t>(channel), static_cast<int16_t>(value));
	case ControllerKind::ProgramChange: return MidiEvent(MidiProgramChange, static_cast<int8_t>(channel), static_cast<int16_t>(value));
	case ControllerKind::ControlChange: break;
	}
	return MidiEvent(MidiControlChange, static_cast<int8_t>(channel), static_cast<int16_t>(lane.controller), static_cast<int16_t>(value));
}

} // namespace

bool InstrumentTrack::handleInputTransform(const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	// MIDI input thread. Each active track may transform once in a forwarding chain; a track's own recursive
	// re-entry is skipped while a nested layer track can still apply its transform.
	if (inputTransformIsActiveFor(this) || event.source() != MidiEvent::Source::External || !m_inputTransform.active())
	{
		return false;
	}
	MidiEvent transformed = event;
	if (m_inputTransform.apply(&transformed))
	{
		InputTransformScope scope{this};
		processInEvent(transformed, time, offset);
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
		const bool jumped = MidiPlayback::instance().chasingNow() || tick < lane.lastTick;
		lane.lastTick = tick;
		if (!jumped && value == lane.lastSent) { continue; }
		lane.lastSent = value;
		processOutEvent(controllerEvent(lane, value, midiPort()->realOutputChannel()), TimePos(tick), offset);
	}
}

bool InstrumentTrack::noteStartsNow(const Note& note, const TimePos& curStart, const MidiClip* clip, bool chaseNow) const
{
	const bool atStart = note.pos() == curStart;
	if (!atStart && !(note.pos() < curStart && note.endPos() > curStart)) { return false; }
	// A note under the clip's own start (a clip trimmed from the left) always sounds from there; one
	// under a start, a jump, a loop or an unmute (0.5.0 ED-075, Q050-040) only when chased.
	if (!atStart && curStart == -clip->startTimeOffset()) { return true; }
	if (!atStart && !chaseNow) { return false; }
	// A chase must not sound a note twice: an under-playhead note a seek or loop left ringing already
	// has a live handle, and starting another would double it (Q050-040, InstrumentTrack::play).
	return !chaseNow || !noteIsAlreadySounding(note, clip);
}

bool InstrumentTrack::noteIsAlreadySounding(const Note& note, const MidiClip* clip) const
{
	// The handle's originating clip and the originating note's id (Note::id(), stable while the note
	// lives): a same-key note in another overlapping clip is not this note (r2), and neither is a twin
	// of the same key and position in this clip, which the model permits (r4). Audio thread, under
	// play()'s tryLock: reads the handle list, allocates nothing.
	const int id = note.id();
	for (PlayHandle* handle : Engine::audioEngine()->playHandles())
	{
		auto* noteHandle = dynamic_cast<NotePlayHandle*>(handle);
		if (noteHandle == nullptr || noteHandle->instrumentTrack() != this) { continue; }
		if (noteHandle->clip() != clip || noteHandle->sourceNoteId() != id) { continue; }
		// A released handle is a release tail, not the note: a loop may sound the note again over it.
		if (noteHandle->isReleased()) { continue; }
		return true;
	}
	return false;
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

bool InstrumentTrack::sendArticulationEvents(const ArticulationEvents& events, int channel, int key, int volume,
	const TimePos& time, f_cnt_t offset)
{
	// 0.5.0 ED-121 / Q050-015: audio thread, from the note's play handle - POD events only, no allocation, no lock
	// of its own, no Qt signal. A layer rack that leaves this track's instrument out of the note leaves its
	// articulation out too.
	if (!m_layers.basePlays(key, volume)) { return false; }
	const auto channelByte = static_cast<int8_t>(channel);
	if (events.program >= 0) { processOutEvent(MidiEvent(MidiProgramChange, channelByte, static_cast<int16_t>(events.program)), time, offset); }
	if (events.controller >= 0)
	{
		processOutEvent(MidiEvent(MidiControlChange, channelByte, static_cast<int16_t>(events.controller),
			static_cast<int16_t>(events.value)), time, offset);
	}
	if (events.keyswitch < 0) { return false; }
	// The key goes to the instrument and the MIDI output directly: it is no played note, so none of the note
	// bookkeeping (keyboard state, running notes, voices, the newNote signal) applies.
	const MidiEvent on(MidiNoteOn, channelByte, static_cast<int16_t>(events.keyswitch), KeyswitchVelocity);
	if (m_instrument != nullptr) { m_instrument->handleMidiEvent(on, time, offset); }
	m_midiPort.processOutEvent(on, time, offset);
	return true;
}

void InstrumentTrack::releaseKeyswitch(int key, int channel, const TimePos& time, f_cnt_t offset)
{
	const MidiEvent off(MidiNoteOff, static_cast<int8_t>(channel), static_cast<int16_t>(key), 0);
	if (m_instrument != nullptr) { m_instrument->handleMidiEvent(off, time, offset); }
	m_midiPort.processOutEvent(off, time, offset);
}

void InstrumentTrack::triggerNote(const NoteTrigger& trigger, f_cnt_t offset, f_cnt_t frames, const TimePos& curStart)
{
	const Note& note = **trigger.note;
	// Swing and the MIDI offset (0.5.0 ED-142 / ED-076) delay a note that starts here - not one chased.
	if (note.pos() == curStart)
	{
		// In the song the note starts at the position playing now - on a looped clip's later pass too, where
		// the clip's start plus the folded note position would repeat the first pass's swing phase.
		const tick_t songTick = trigger.songGlobal ? trigger.songTick : note.pos().getTicks();
		offset += MidiPlayback::instance().delayFrames(songTick, Engine::framesPerTick(), Engine::audioEngine()->outputSampleRate());
	}
	// MX-035: the track's own delay, on top (a chased note too: the whole track sits later).
	offset += static_cast<f_cnt_t>(std::lround(delayMs() * Engine::audioEngine()->outputSampleRate() / 1000.0));
	// Q050-015: what the note's articulation sends goes with the note's own handle (the first hit's), which sends it
	// at the offset the note starts at - however far swing, the MIDI offset or the track's delay put that.
	const Articulation* articulation = m_articulations.at(note.conditions().articulation);
	const ArticulationEvents articulationEvents = articulation != nullptr ? articulation->events() : ArticulationEvents{};
	// Where the clip's own time 0 lies in the song for the pass playing: the clip's start on its first pass.
	const TimePos songOffset(trigger.songTick - curStart.getTicks());
	// 0.5.0 ED-133: the note's repeats - that many more hits, evenly spaced inside it (not a step note's).
	const int hits = frames > 0 ? static_cast<int>(std::min<f_cnt_t>(note.conditions().repeats + 1, frames)) : 1;
	const f_cnt_t each = frames / hits;
	for (int hit = 0; hit < hits; ++hit)
	{
		// The last hit takes what the even split leaves over, so the note still ends where it ends.
		const f_cnt_t length = hit + 1 < hits ? each : frames - hit * each;
		NotePlayHandle* handle = NotePlayHandleManager::acquire(this, offset + hit * each, length, note);
		handle->setClip(trigger.clip);  // Q050-040: the chase check must tell this clip's note from another's
		handle->setSourceNoteId(note.id());  // Q050-040 r4: ...and one note of the clip from its same-key, same-position twin
		handle->setPatternTrack(trigger.patternTrack);
		// Velocity jitter, the slide source, the song-global offset and the clip's transpose.
		decorateNoteHandle(handle, trigger.clip, trigger.note, trigger.first, trigger.songGlobal, songOffset, trigger.midiSeed);
		// 0.5.0 MX-112: the same note on each layer its zones name - before the hand-off, which owns the handle.
		if (hit == 0) { handle->setArticulation(articulationEvents); }
		m_layers.playNote(*handle, offset + hit * each, length, trigger.patternTrack);
		Engine::audioEngine()->addPlayHandle(handle);
	}
}

tick_t InstrumentTrack::passEnd(const MidiClip* clip, tick_t passBase, bool looped)
{
	// In the clip's own time folded into its loop: a note that runs past the loop's end stops there and the
	// next pass starts it again, rather than sounding on into it (and the last pass ends with the clip).
	const tick_t clipEnd = clip->length() - clip->startTimeOffset() - passBase;
	const int loop = looped ? clip->clipEdits().loopTicks : 0;
	return loop > 0 ? std::min<tick_t>(loop, clipEnd) : clipEnd;
}

TimePos InstrumentTrack::loopedPosition(const MidiClip* clip, const TimePos& position)
{
	const int loop = clip->clipEdits().loopTicks;
	return loop > 0 && position >= 0 ? TimePos(position.getTicks() % loop) : position;
}

// ---------------------------------------------------------------------------
// 0.5.0 MX-112 (layers), MX-114 (MIDI parts, output buses), MX-120 (per-voice modulation)
// ---------------------------------------------------------------------------

void InstrumentTrack::sendNoteOn(const MidiEvent& event, int key, int fallbackChannel, const TimePos& time, f_cnt_t offset,
	std::atomic_bool* baseInstrumentAccepted)
{
	// MX-112: a layer rack whose zones leave the track's own instrument out of this note.
	if (!m_layers.baseTakesNoteOn(event.key(), event.velocity(), baseInstrumentAccepted)) { return; }
	m_midiNotesMutex.lock();
	m_piano.setKeyState(event.key(), true);  // event.key() = original key
	if (key >= 0 && key < NumKeys)
	{
		// MX-114: the MIDI part (channel) the note plays on; without parts, the fallback channel.
		const int previousChannel = m_midiParts.channelOfKey(event.key(), fallbackChannel);
		const int channel = m_midiParts.noteOnChannel(event.key(), event.velocity(), fallbackChannel);
		if (m_runningMidiNotes[key] > 0)
		{
			m_instrument->handleMidiEvent(MidiEvent(MidiNoteOff, previousChannel, key, 0), time, offset);
		}
		++m_runningMidiNotes[key];
		m_instrument->handleMidiEvent(MidiEvent(MidiNoteOn, channel, key, event.velocity()), time, offset);
		noteVoiceChanged(true, channel, key, offset);  // MX-120: a per-voice modulator's voice
	}
	recordNoteOnActivity();	// polled by InstrumentTrackView; no queued Qt signal on the event path
	m_midiNotesMutex.unlock();
}

void InstrumentTrack::sendNoteOff(const MidiEvent& event, int key, int fallbackChannel, const TimePos& time, f_cnt_t offset,
	std::atomic_bool* baseInstrumentAccepted)
{
	if (!m_layers.baseTakesNoteOff(event.key(), baseInstrumentAccepted)) { return; }  // MX-112: its note-on was not sent either
	m_midiNotesMutex.lock();
	m_piano.setKeyState(event.key(), false);  // event.key() = original key
	if (key >= 0 && key < NumKeys && m_runningMidiNotes[key] > 0 && --m_runningMidiNotes[key] == 0)
	{
		const int channel = m_midiParts.noteOffChannel(event.key(), fallbackChannel);  // MX-114
		// The note-off velocity (0 unless the note has a release velocity, 0.5.0 L07).
		m_instrument->handleMidiEvent(MidiEvent(MidiNoteOff, channel, key, event.velocity()), time, offset);
		noteVoiceChanged(false, channel, key, offset);
	}
	recordNoteOffActivity();	// polled by InstrumentTrackView; no queued Qt signal on the event path
	m_midiNotesMutex.unlock();
}

void InstrumentTrack::sendChannelEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	// MX-114: with MIDI parts a channel-wide message reaches every part's channel.
	if (!sendToParts(event, time, offset)) { m_instrument->handleMidiEvent(event, time, offset); }
}

void InstrumentTrack::loadMidiControllers(const QDomElement& element)
{
	for (int i = 0; i < MidiControllerCount; ++i)
	{
		m_midiCCModel[i]->loadSettings(element, "cc" + QString::number(i));
	}
}

bool InstrumentTrack::sendToParts(const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	// Audio thread (processOutEvent): a handful of copies of a POD event, no allocation.
	if (!m_midiParts.active()) { return false; }
	if (event.type() == MidiKeyPressure)
	{
		MidiEvent copy(event);
		copy.setChannel(static_cast<int8_t>(m_midiParts.channelOfKey(event.key(), event.channel())));
		m_instrument->handleMidiEvent(copy, time, offset);
		return true;
	}
	// Every part's channel, and the track's own one for the notes no part claims.
	const std::uint16_t channels = static_cast<std::uint16_t>(m_midiParts.channelMask() | (1u << (event.channel() & 0x0f)));
	for (int channel = 0; channel < InstrumentMidiParts::Channels; ++channel)
	{
		if (((channels >> channel) & 1u) == 0) { continue; }
		MidiEvent copy(event);
		copy.setChannel(static_cast<int8_t>(channel));
		m_instrument->handleMidiEvent(copy, time, offset);
	}
	return true;
}

void InstrumentTrack::noteVoiceChanged(bool on, int channel, int key, f_cnt_t offset)
{
	// Atomics only (ModulationVoices): the per-voice modulators that follow this track start or end a voice.
	if (Song* song = Engine::getSong()) { song->modulationLayer().voiceChanged(id(), on, channel, key, offset); }
}

bool InstrumentTrack::isZeneInstrumentNode(const QString& name)
{
	return name == QLatin1String("midiparts") || name == QLatin1String("layers")
		|| name == QLatin1String("outputbuses");
}

void InstrumentTrack::saveZeneInstrumentSettings(QDomDocument& doc, QDomElement& element, bool presetMode)
{
	m_midiParts.saveSettings(doc, element);
	m_layers.saveSettings(doc, element);
	// Routes name this project's mixer channels: a preset carries the sound, not where it is mixed.
	InstrumentOutputBuses* buses = m_instrument != nullptr ? m_instrument->outputBuses() : nullptr;
	if (!presetMode && buses != nullptr) { buses->saveSettings(doc, element); }
}

void InstrumentTrack::loadZeneInstrumentSettings(const QDomElement& element, bool presetMode)
{
	// MIDI parts and layers reset on absence. Output-bus routes name project mixer channels and presets
	// deliberately omit them, so a preset only replaces routes when it explicitly carries the node.
	m_midiParts.loadSettings(element);
	m_layers.loadSettings(element);
	InstrumentOutputBuses* buses = m_instrument != nullptr ? m_instrument->outputBuses() : nullptr;
	if (buses != nullptr && !m_previewMode
		&& (!presetMode || !element.firstChildElement(QStringLiteral("outputbuses")).isNull()))
	{
		buses->loadSettings(element);
	}
}

} // namespace lmms
