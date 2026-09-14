/*
 * RetroMidiCapture.h - retrospective MIDI capture: one bounded recent-event
 *                      ring per open MIDI client, off by default
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

#ifndef LMMS_RETRO_MIDI_CAPTURE_H
#define LMMS_RETRO_MIDI_CAPTURE_H

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "RetroMidiRing.h"

namespace lmms
{

class MidiEvent;


//! The MIDI side of retrospective capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md).
/*!
 * OFF BY DEFAULT. Nothing is recorded until arm(true) is called, and the whole
 * cost on the MIDI input thread while disarmed is one relaxed atomic load per
 * event - the contract MidiLearn documents for the same thread
 * (include/MidiLearn.h:58-63).
 *
 * Threading contract:
 *  - capture() runs on the MIDI input thread: MidiAlsaSeq::run() for the
 *    sequencer client, MidiClientRaw::processParsedEvent() for the raw clients.
 *    It allocates nothing, takes no lock, makes no syscall, never waits and
 *    touches no Model, Song or MidiPort. It must never be called from anywhere
 *    else;
 *  - arm()/isArmed() and the ring's snapshot methods are called from the
 *    GUI/control thread;
 *  - the ring's storage is allocated exactly once, in this object's
 *    constructor, which must run off the MIDI thread and off the audio thread.
 *
 * The arm flag is runtime state, not project state, so arming records no
 * transaction - the MidiLearn precedent (src/core/ControlCommandsSettings.cpp).
 * It is deliberately NOT persisted yet: RetroMidiCapture is a member of
 * MidiClient, one MidiClient in this tree is a file-static global
 * (`static MidiDummy s_dummyClient;`, src/core/midi/MidiPort.cpp:41) built before
 * main(), and ConfigManager::inst() dereferences qApp in its constructor
 * (src/core/ConfigManager.cpp:65) - so a ConfigManager read here would be a null
 * dereference at static-initialisation time. The persisted `midi/retrocapture`
 * key belongs with the command surface that can write it, where qApp exists.
 */
class RetroMidiCapture
{
public:
	//! 8192 events x 16 B = 128 KiB, paid once per open client - about 7-13
	//! minutes of the densest two-hand playing at 10-20 events/s.
	static constexpr std::size_t DefaultCapacityEvents = 1u << 13;

	RetroMidiCapture();

	RetroMidiCapture(const RetroMidiCapture&) = delete;
	RetroMidiCapture& operator=(const RetroMidiCapture&) = delete;

	//! Arm or disarm the capture. Returns the new state. Mode state, never part
	//! of a project, never a transaction.
	bool arm(bool enabled) noexcept
	{
		m_armed.store(enabled, std::memory_order_relaxed);
		return enabled;
	}

	//! True while events are being recorded. False on construction.
	bool isArmed() const noexcept { return m_armed.load(std::memory_order_relaxed); }

	//! MIDI input thread: record one received event. Realtime-safe (see the
	//! class comment). \a tick is the transport tick the caller can stamp the
	//! event with - the ALSA sequencer's own event tick, or publishedTick() for
	//! the raw clients, which have no tick of their own. \a source is the ALSA
	//! source port (0 for the raw clients).
	//!
	//! Channel messages are recorded, and a SysEx - which is variable-length and
	//! would need a second bounded byte store - is recorded as one flagged
	//! placeholder rather than dropped silently. System real-time and system
	//! common bytes (clock, active sensing, ...) are not recorded: they are not
	//! music, and in a window that holds only the most recent events they would
	//! evict the notes a capture exists to keep.
	void capture(const MidiEvent& event, std::uint32_t tick, std::uint16_t source = 0) noexcept;

	//! Audio thread: publish this period's play position, once per period, so a
	//! raw MIDI client can stamp an event without reading Song state (Song's
	//! play position is not thread-safe: include/Song.h has no atomics and
	//! getPlayPos() hands out a reference to live state). One relaxed store.
	//!
	//! It is a single process-wide value rather than a per-client one on
	//! purpose: the alternative - dereferencing AudioEngine::midiClient() on the
	//! audio thread - would race the GUI thread that swaps and deletes clients.
	static void publishTick(std::uint32_t tick) noexcept;
	static std::uint32_t publishedTick() noexcept;

	//! The ring behind this capture (the consumer side, the GUI/control thread).
	RetroMidiRing& ring() noexcept { return m_ring; }
	const RetroMidiRing& ring() const noexcept { return m_ring; }

private:
	//! True for the event types this capture stores (channel messages and the
	//! SysEx placeholder).
	static bool recordable(std::uint8_t type) noexcept;

	RetroMidiRing m_ring;
	std::atomic<bool> m_armed{false};
};


} // namespace lmms

#endif // LMMS_RETRO_MIDI_CAPTURE_H
