/*
 * RetroMidiCapture.cpp - retrospective MIDI capture: the event mapping, the arm
 *                        switch and the transport-tick publisher
 *
 * Copyright (c) 2026 LMMS developers
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

#include "RetroMidiCapture.h"

#include <atomic>
#include <cstdint>

#include "Midi.h"
#include "MidiEvent.h"

namespace lmms
{

namespace
{

//! Written by the audio thread once per rendered period, read relaxed by the
//! MIDI thread. One process-wide value: the transport position is a property of
//! the song, not of the client that received the event.
std::atomic<tick_t> g_publishedTick{0};

//! Split a 14-bit pitch-bend value into the two seven-bit fields the element
//! stores (the ALSA-sequencer path already folds its -8192..8191 into
//! 0..16383, src/core/midi/MidiAlsaSeq.cpp:606-608).
void storePitchBend(RetroMidiEvent& recorded, int16_t pitchBend) noexcept
{
	const auto value = static_cast<std::uint16_t>(pitchBend) & 0x3FFFu;
	recorded.param1 = static_cast<std::uint8_t>(value & 0x7Fu);
	recorded.param2 = static_cast<std::uint8_t>((value >> 7) & 0x7Fu);
}

//! Map a channel message's parameters into the two seven-bit fields. Every
//! accessor already masks to seven bits (include/MidiEvent.h).
void storeEventParams(RetroMidiEvent& recorded, const MidiEvent& event) noexcept
{
	switch (event.type())
	{
		case MidiNoteOff:
		case MidiNoteOn:
		case MidiKeyPressure:
			recorded.param1 = static_cast<std::uint8_t>(event.key()) & 0x7Fu;
			recorded.param2 = event.velocity();
			return;

		case MidiControlChange:
			recorded.param1 = event.controllerNumber();
			recorded.param2 = static_cast<std::uint8_t>(event.controllerValue()) & 0x7Fu;
			return;

		case MidiProgramChange:
		case MidiChannelPressure:
			// program() and channelPressure() are both param(0).
			recorded.param1 = static_cast<std::uint8_t>(event.param(0)) & 0x7Fu;
			return;

		case MidiPitchBend:
			storePitchBend(recorded, event.pitchBend());
			return;

		default:
			// recordable() admits channel messages only, so this is unreachable
			// for a captured event. Keep the fixed-size snapshot untouched.
			return;
	}
}

} // namespace


RetroMidiCapture::RetroMidiCapture() :
	m_ring(DefaultCapacityEvents)
{
}


bool RetroMidiCapture::recordable(std::uint8_t type) noexcept
{
	// The channel messages, whose status byte is the MidiEventTypes value
	// (include/Midi.h:33-61). SysEx is intentionally excluded: the raw parser
	// discards its variable-length payload and this fixed-size ring has no byte
	// store, so the capture contract must not promise a placeholder it cannot
	// receive from that backend. Everything else is transport/system data.
	return type >= MidiNoteOff && type <= MidiPitchBend;
}


void RetroMidiCapture::capture(const MidiEvent& event, tick_t tick, std::uint16_t source) noexcept
{
	// While nothing is armed this is the whole cost: one relaxed atomic load.
	if (!m_armed.load(std::memory_order_relaxed)) { return; }

	const auto type = static_cast<std::uint8_t>(event.type());
	if (!recordable(type)) { return; }

	RetroMidiEvent recorded{};
	recorded.tick = tick;
	recorded.source = source;
	recorded.type = type;
	recorded.channel = static_cast<std::uint8_t>(event.channel());
	if (event.source() == MidiEvent::Source::External)
	{
		recorded.flags |= RetroMidiFlagExternal;
	}
	storeEventParams(recorded, event);

	m_ring.push(recorded);
}


void RetroMidiCapture::publishTick(tick_t tick) noexcept
{
	g_publishedTick.store(tick, std::memory_order_relaxed);
}


tick_t RetroMidiCapture::publishedTick() noexcept
{
	return g_publishedTick.load(std::memory_order_relaxed);
}


} // namespace lmms
