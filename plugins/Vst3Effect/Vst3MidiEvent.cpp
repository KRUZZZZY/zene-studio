/*
 * Vst3MidiEvent.cpp - LMMS MIDI to VST3 events, and the event-bus wiring
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

#include "Vst3MidiEvent.h"
#include "Vst3MidiMapping.h"

#include <algorithm>

#include "Midi.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace lmms::vst3
{

auto resolveEventInputBus(IComponent* component) -> int32
{
	if (component == nullptr) { return -1; }

	const auto eventInputCount = component->getBusCount(kEvent, kInput);
	int32 chosen = -1;
	for (int32 i = 0; i < eventInputCount; ++i)
	{
		BusInfo info{};
		if (component->getBusInfo(kEvent, kInput, i, info) != kResultOk) { continue; }
		if ((info.flags & BusInfo::kDefaultActive) == 0) { continue; }
		chosen = i;
		break;
	}
	// A plug-in that declares event inputs but activates none by default still
	// expects MIDI on the first one.
	if (chosen < 0 && eventInputCount > 0) { chosen = 0; }
	if (chosen < 0) { return -1; }

	for (int32 i = 0; i < eventInputCount; ++i)
	{
		component->activateBus(kEvent, kInput, i, i == chosen);
	}
	return chosen;
}

namespace
{

//! 0.5.0 MX-120: the note id a note event carries - its channel and key when the plug-in takes note
//! expressions (so an expression can name the voice), the SDK samples' -1 otherwise.
int32 noteIdOf(const MidiEventIn& midi)
{
	return midi.withNoteId ? static_cast<int32>(midi.channel) * 128 + midi.data0 : -1;
}

} // namespace

auto midiToVst3Event(const MidiEventIn& midi, int32 busIndex, Event* event) -> bool
{
	*event = Event{};
	event->busIndex = busIndex;
	event->sampleOffset = midi.frameOffset;
	event->ppqPosition = 0.0;

	const auto channel = static_cast<int16>(midi.channel);
	const auto key = static_cast<int16>(midi.data0);
	const auto value = static_cast<float>(midi.data1) / 127.f;

	switch (midi.type)
	{
		case MidiNoteOn:
			if (midi.data1 == 0)
			{
				event->type = Event::kNoteOffEvent;
				event->noteOff.channel = channel;
				event->noteOff.pitch = key;
				event->noteOff.velocity = 0.f;
				event->noteOff.noteId = noteIdOf(midi);
				event->noteOff.tuning = 0.f;
				return true;
			}
			event->type = Event::kNoteOnEvent;
			event->noteOn.channel = channel;
			event->noteOn.pitch = key;
			event->noteOn.tuning = 0.f;
			event->noteOn.velocity = value;
			// No length: the host does not guess a note duration, the matching
			// note-off always follows (SDK ivstevents.h:50).
			event->noteOn.length = 0;
			event->noteOn.noteId = noteIdOf(midi);
			return true;

		case MidiNoteOff:
			event->type = Event::kNoteOffEvent;
			event->noteOff.channel = channel;
			event->noteOff.pitch = key;
			event->noteOff.velocity = value;
			event->noteOff.noteId = noteIdOf(midi);
			event->noteOff.tuning = 0.f;
			return true;

		case MidiKeyPressure:
			event->type = Event::kPolyPressureEvent;
			event->polyPressure.channel = channel;
			event->polyPressure.pitch = key;
			event->polyPressure.pressure = value;
			event->polyPressure.noteId = noteIdOf(midi);
			return true;

		case kNoteExpressionMidiType:
			event->type = Event::kNoteExpressionValueEvent;
			event->noteExpressionValue.typeId = midi.expressionType;
			event->noteExpressionValue.noteId = static_cast<int32>(midi.channel) * 128 + midi.data0;
			event->noteExpressionValue.value = std::clamp(midi.expressionValue, 0.0, 1.0);
			return true;

		case MidiControlChange:
			event->type = Event::kLegacyMIDICCOutEvent;
			event->midiCCOut.controlNumber = midi.data0;
			event->midiCCOut.channel = static_cast<int8>(midi.channel);
			event->midiCCOut.value = static_cast<int8>(midi.data1);
			event->midiCCOut.value2 = 0;
			return true;

		default:
			return false;
	}
}

auto drainMidiIntoEventList(MidiQueue& queue, std::vector<MidiEventIn>& scratch,
	EventList& events, int32 busIndex, int frames, const MidiControllerMap* controllers,
	IParameterChanges* changes) -> int
{
	const auto capacity = scratch.size();
	std::size_t count = 0;
	MidiEventIn midi;
	while (count < capacity && queue.pop(&midi))
	{
		// Insertion sort by sample offset: stable, bounded, allocation free,
		// and effectively linear because the MIDI path delivers events in
		// order in the common case (a clip's notes).
		std::size_t position = count;
		while (position > 0 && scratch[position - 1].frameOffset > midi.frameOffset)
		{
			scratch[position] = scratch[position - 1];
			--position;
		}
		scratch[position] = midi;
		++count;
	}
	// A parameter point must lie inside the request ([0, frames), ivstparameterchanges.h), so a
	// mapped controller is bounded to its last frame; an event keeps the inherited [0, frames] rule.
	const int lastFrame = std::max(frames - 1, 0);
	for (std::size_t i = 0; i < count; ++i)
	{
		// E10: a controller the plug-in mapped to a parameter becomes a parameter change, at its own
		// frame (0.5.0 W4).
		if (controllers != nullptr
			&& controllers->apply(scratch[i], changes, std::clamp(scratch[i].frameOffset, 0, lastFrame)))
		{
			continue;
		}
		Event event{};
		if (!midiToVst3Event(scratch[i], busIndex, &event)) { continue; }
		event.sampleOffset = std::clamp(scratch[i].frameOffset, 0, frames);
		events.addEvent(event);
	}
	return static_cast<int>(count);
}

auto vst3EventToMidi(const Event& event, int chunkOffset, MidiEventIn* midi) -> bool
{
	const auto velocity = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 127.f + 0.5f); };
	switch (event.type)
	{
	case Event::kNoteOnEvent:
		*midi = {0x90, static_cast<std::uint8_t>(event.noteOn.channel & 0x0f),
			static_cast<std::uint8_t>(event.noteOn.pitch & 0x7f), velocity(event.noteOn.velocity), 0};
		break;
	case Event::kNoteOffEvent:
		*midi = {0x80, static_cast<std::uint8_t>(event.noteOff.channel & 0x0f),
			static_cast<std::uint8_t>(event.noteOff.pitch & 0x7f), velocity(event.noteOff.velocity), 0};
		break;
	case Event::kPolyPressureEvent:
		*midi = {0xa0, static_cast<std::uint8_t>(event.polyPressure.channel & 0x0f),
			static_cast<std::uint8_t>(event.polyPressure.pitch & 0x7f), velocity(event.polyPressure.pressure), 0};
		break;
	default:
		return false;
	}
	midi->frameOffset = event.sampleOffset + chunkOffset;
	return true;
}

} // namespace lmms::vst3
