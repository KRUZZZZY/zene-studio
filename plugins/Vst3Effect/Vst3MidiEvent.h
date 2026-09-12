/*
 * Vst3MidiEvent.h - LMMS MIDI to VST3 events, and the event-bus wiring
 *
 * Copyright (c) 2026 LMMS contributors
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
 * the MIDI idiom for a note-off, and both note events carry noteId -1 because
 * the host does not track note identities.
 */
auto midiToVst3Event(const MidiEventIn& midi, Steinberg::int32 busIndex,
	Steinberg::Vst::Event* event) -> bool;

/**
 * Drains queued MIDI into a plug-in's event list, ordered by sample offset.
 *
 * Real-time path: allocates nothing (the scratch vector is sized in
 * prepare()), takes no locks, and stops when the block is full - anything left
 * queued is picked up by the next block. At most kMaxMidiEventsPerBlock events
 * are taken per call, so the work per block is bounded.
 *
 * @returns how many events were added to \a events
 */
auto drainMidiIntoEventList(MidiQueue& queue, std::vector<MidiEventIn>& scratch,
	Steinberg::Vst::EventList& events, Steinberg::int32 busIndex, int frames) -> int;

} // namespace lmms::vst3

#endif // LMMS_VST3_MIDI_EVENT_H
