/*
 * Vst3MidiEvent.h - LMMS MIDI to VST3 events, and the event-bus wiring
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

#ifndef LMMS_VST3_MIDI_EVENT_H
#define LMMS_VST3_MIDI_EVENT_H

#include <vector>

#include "public.sdk/source/vst/hosting/eventlist.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "Vst3MidiQueue.h"

namespace lmms::vst3
{

/**
 * Chooses and activates the input event bus the host drives.
 *
 * An instrument may declare more than one event input; the policy this host
 * implements (and writes down in docs/VST3-INSTRUMENT-HOSTING.md) is to drive
 * the FIRST bus that is active by default and to deactivate the others.
 *
 * Called from HostedPlugin::load(), for instruments only - an effect's event
 * buses are left exactly as the plug-in declared them.
 *
 * @returns the bus index, or -1 when the component declares no input event bus
 */
auto resolveEventInputBus(Steinberg::Vst::IComponent* component) -> Steinberg::int32;

/**
 * Translates one MIDI event into the VST3 event the plug-in's input event bus
 * expects. @returns false for event types this host does not carry.
 *
 * The mapping follows the SDK's own samples (samples/vst-hosting/audiohost/
 * source/media/miditovst.h at the pinned commit): a note-on with velocity 0 is
 * the MIDI idiom for a note-off. The note id is the one NoteIds resolved for
 * the event (-1 when the plug-in takes no note ids); a note expression with no
 * id names no voice, so it is not carried (returns false).
 */
auto midiToVst3Event(const MidiEventIn& midi, Steinberg::int32 busIndex,
	Steinberg::int32 noteId, Steinberg::Vst::Event* event) -> bool;

/**
 * Drains queued MIDI into a plug-in's event list, ordered by sample offset.
 *
 * Real-time path: allocates nothing (the scratch vector is sized in
 * prepare()), takes no locks, and stops when the block is full - anything left
 * queued is picked up by the next block. At most kMaxMidiEventsPerBlock events
 * are taken per call, so the work per block is bounded.
 *
 * A controller \a controllers maps becomes a point in \a changes at its own
 * frame, bounded to [0, frames - 1] (0.5.0 W4); the request's list is sliced
 * per chunk by HostedPlugin::Impl::runChunk (sliceParameterChanges).
 *
 * Each note event's VST3 note id is resolved by queue.noteIds() in the same
 * sorted order the plug-in receives the events (MX-120 round 2).
 *
 * @returns how many events were taken from the queue
 *
 * @a maxEvents caps how many events are taken (the caller passes the event
 * list's remaining room when carried boundary events already occupy part of
 * it); -1 takes the scratch vector's own capacity, as every older caller does.
 */
auto drainMidiIntoEventList(MidiQueue& queue, std::vector<MidiEventIn>& scratch,
	Steinberg::Vst::EventList& events, Steinberg::int32 busIndex, int frames,
	const class MidiControllerMap* controllers = nullptr,
	Steinberg::Vst::IParameterChanges* changes = nullptr, int maxEvents = -1) -> int;

/**
 * The inverse of midiToVst3Event for what a plug-in WRITES to ProcessData::outputEvents: note-on,
 * note-off and polyphonic pressure become MIDI, rebased by @a chunkOffset to the block. @returns
 * false for an event this host does not carry (engine audit E7: before this the list was never
 * handed to the plug-in at all, so an arpeggiator's or a sequencer's notes were lost).
 */
auto vst3EventToMidi(const Steinberg::Vst::Event& event, int chunkOffset, MidiEventIn* midi) -> bool;

} // namespace lmms::vst3

#endif // LMMS_VST3_MIDI_EVENT_H
