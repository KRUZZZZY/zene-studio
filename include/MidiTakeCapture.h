/*
 * MidiTakeCapture.h - the realtime half of arrangement MIDI take recording: one
 *                     bounded lock-free event queue per armed instrument track
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

#ifndef LMMS_MIDI_TAKE_CAPTURE_H
#define LMMS_MIDI_TAKE_CAPTURE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class MidiEvent;

//! One captured note edge, stamped with the transport position it arrived at. Plain data: it holds no
//! pointer into the event that produced it (MidiEvent carries a sequencer-owned source pointer).
struct MidiTakeEvent
{
	tick_t tick = 0;          //!< song tick of the audio period the event arrived in
	std::uint32_t wraps = 0;  //!< Song::loopWraps() of that period (low 32 bits): the loop pass
	std::uint8_t on = 0;      //!< 1 = note-on, 0 = note-off (a note-on with velocity 0 is an off)
	std::uint8_t key = 0;
	std::uint8_t velocity = 0;
	std::uint8_t channel = 0;
};

/*! Q050-054.1: the capture an armed instrument track's live MIDI input feeds while transport.record runs.
 *
 *  Producers are whatever thread delivers MIDI to InstrumentTrack::processInEvent - the ALSA sequencer
 *  thread, a JACK MIDI port on the audio thread, the GUI's on-screen keyboard - possibly at once, so the
 *  queue is a bounded multi-producer / single-consumer ring (Vyukov's bounded queue): capture() takes no
 *  lock, never allocates and never waits; when the ring is full, or a producer loses MaxClaimAttempts
 *  races for a slot in a row, the event is dropped and counted. The
 *  single consumer is the main thread (MidiTakeRecorder), which drains while recording and once at Stop.
 *
 *  The storage is allocated by arm() on the main thread the first time the track is armed and is kept
 *  until the track is destroyed, so a producer that passed the capturing() check never sees it freed. */
class LMMS_EXPORT MidiTakeCapture
{
public:
	//! 4096 events x 24 B = 96 KiB per armed track; drained every MidiTakeRecorder::DrainIntervalMs, so the
	//! bound is ~80 000 events/s - far above two-handed playing.
	static constexpr std::size_t CapacityEvents = 4096;
	//! A producer that loses the race for a slot this many times in a row drops its event (counted in
	//! dropped()) rather than retrying: capture() finishes in bounded time even on the audio thread.
	static constexpr int MaxClaimAttempts = 64;

	MidiTakeCapture() = default;
	~MidiTakeCapture();
	MidiTakeCapture(const MidiTakeCapture&) = delete;
	MidiTakeCapture& operator=(const MidiTakeCapture&) = delete;

	//! Main thread: arm or disarm the track for MIDI take recording (mode state, not saved). Arming
	//! allocates the ring once.
	void arm(bool armed);
	bool isArmed() const noexcept { return m_armed.load(std::memory_order_relaxed); }

	//! Main thread: open a recording session - stale events are discarded, then capture() starts
	//! queueing. False when the track is not armed.
	bool startCapture();
	//! Main thread: close the session; capture() stops queueing (call drain() afterwards).
	void stopCapture() noexcept { m_capturing.store(false, std::memory_order_release); }
	bool isCapturing() const noexcept { return m_capturing.load(std::memory_order_acquire); }

	//! Any MIDI input thread: queue a note edge of @a event if a session is open. Realtime-safe.
	void capture(const MidiEvent& event) noexcept;

	//! Main thread (the single consumer): move every queued event to @a out; answers how many.
	std::size_t drain(std::vector<MidiTakeEvent>* out);
	//! Events refused because the ring was full, since the track was first armed.
	std::uint64_t dropped() const noexcept { return m_dropped.load(std::memory_order_relaxed); }
	std::size_t capacity() const noexcept { return m_cells ? CapacityEvents : 0; }

	//! Audio thread, once per rendered period (Song::processNextBuffer): the transport position and
	//! the loop pass capture() stamps events with - Song's play position is not readable off the
	//! render thread. One relaxed store of a packed word, so tick and pass never tear.
	static void publishPosition(tick_t tick, std::uint64_t loopWraps) noexcept;
	static tick_t publishedTick() noexcept;
	static std::uint32_t publishedWraps() noexcept;

private:
	struct Cell
	{
		std::atomic<std::uint64_t> sequence{0};
		MidiTakeEvent event;
	};

	bool push(const MidiTakeEvent& event) noexcept;
	bool pop(MidiTakeEvent* event) noexcept;

	std::unique_ptr<Cell[]> m_cells;
	alignas(64) std::atomic<std::uint64_t> m_enqueue{0};
	alignas(64) std::uint64_t m_dequeue = 0;
	std::atomic<bool> m_ready{false};  //!< m_cells is allocated (release-published by arm())
	std::atomic<bool> m_armed{false};
	std::atomic<bool> m_capturing{false};
	std::atomic<std::uint64_t> m_dropped{0};
};

} // namespace lmms

#endif // LMMS_MIDI_TAKE_CAPTURE_H
