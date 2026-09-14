/*
 * RetroMidiRing.h - bounded, event-typed ring of the most recent MIDI input
 *                   events (retrospective MIDI capture, owner item 14)
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

#ifndef LMMS_RETRO_MIDI_RING_H
#define LMMS_RETRO_MIDI_RING_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace lmms
{


//! One received MIDI event, snapshotted into POD form.
/*!
 * Deliberately NOT a MidiEvent: that type carries two raw pointers
 * (include/MidiEvent.h:217-218), and on the ALSA-sequencer path the source
 * pointer handed to the event is the address of a field of the sequencer's own
 * event struct (`&ev->source`, src/core/midi/MidiAlsaSeq.cpp:531), so
 * retaining it across a bounded window would retain a dangling pointer.
 *
 * The two seven-bit parameters are lossless for channel messages - MidiEvent's
 * own accessors already mask to seven bits (velocity(), controllerNumber(),
 * include/MidiEvent.h) - and the one 14-bit message, pitch bend, is split by
 * the writer: param1 = value & 0x7F, param2 = (value >> 7) & 0x7F.
 */
struct RetroMidiEvent
{
	std::uint32_t tick;      //!< transport tick the event was stamped with
	std::uint16_t source;    //!< ALSA source port; 0 for the raw clients
	std::uint8_t type;       //!< MidiEventTypes (include/Midi.h)
	std::uint8_t channel;    //!< widened from MidiEvent's int8_t m_channel
	std::uint8_t param1;     //!< key / controller / program / pressure / bend lo
	std::uint8_t param2;     //!< velocity / value / pitch-bend hi
	std::uint8_t flags;      //!< bits below
	std::uint8_t pad;        //!< keeps the snapshot 16 bytes for every target
	std::uint16_t reserved;
};

static_assert(sizeof(RetroMidiEvent) == 16,
		"the slot is the unit of the capacity budget (docs/MIDI-RETRO-CAPTURE.md section 3.2)");

// RetroMidiEvent::flags
constexpr std::uint8_t RetroMidiFlagExternal = 1u << 0;      //!< MidiEvent::Source::External
constexpr std::uint8_t RetroMidiFlagSysExDropped = 1u << 1;  //!< a SysEx was seen and not stored


//! Bounded ring of the most recent MIDI events; single producer, single consumer.
/*!
 * The properties are copied from RecordRingBuffer, the programme's precedent
 * (include/RecordRingBuffer.h): storage allocated exactly once in the
 * constructor, which must run off the MIDI thread and off the audio thread; one
 * producer-owned index; release/acquire hand-off; a monotonic counter for what
 * was lost; no locks, no syscalls, no allocation and O(1) work per event on the
 * producer path.
 *
 * Three deliberate departures from that precedent, each argued in
 * docs/MIDI-RETRO-CAPTURE.md section 1:
 *
 *  - the element is a 16-byte POD event, not a sample_t frame;
 *  - the policy is drop-OLDEST. The producer owns one monotonically increasing
 *    head and overwrites slot (head & mask), so the ring always holds the most
 *    recent capacity() events; a push that overlaps an older event counts it in
 *    overwrittenCount() and the consumer clamps the window it may read to the
 *    last capacity() events by arithmetic;
 *  - the consumer's copy is guarded by a publication sequence, so a copy is
 *    never torn even while the producer is running (see copyOut()).
 */
class RetroMidiRing
{
public:
	//! Allocates storage for at least \a minCapacityEvents events, rounded up
	//! to a power of two. Must run off the MIDI thread and off the audio thread.
	explicit RetroMidiRing(std::size_t minCapacityEvents) :
		m_capacity(nextPowerOfTwo(minCapacityEvents == 0 ? 1 : minCapacityEvents)),
		m_mask(m_capacity - 1),
		m_slots(std::make_unique<RetroMidiEvent[]>(m_capacity))
	{
	}

	RetroMidiRing(const RetroMidiRing&) = delete;
	RetroMidiRing& operator=(const RetroMidiRing&) = delete;

	//! Producer: record one event. Returns false when the event was dropped
	//! because a snapshot was in flight. Realtime-safe: no allocation, no lock,
	//! no syscall, O(1); one relaxed atomic load on the fast path.
	bool push(const RetroMidiEvent& event) noexcept
	{
		if (m_snapshotPending.load(std::memory_order_relaxed))
		{
			m_pausedDrops.fetch_add(1, std::memory_order_relaxed);
			m_writerAck.store(m_snapshotEpoch.load(std::memory_order_acquire),
					std::memory_order_release);
			return false;
		}

		const auto head = m_head.load(std::memory_order_relaxed);
		const auto sequence = m_sequence.load(std::memory_order_relaxed);
		m_sequence.store(sequence + 1, std::memory_order_release);  // odd: a slot is being written
		m_slots[head & m_mask] = event;
		m_head.store(head + 1, std::memory_order_release);
		m_sequence.store(sequence + 2, std::memory_order_release);  // even: the window is consistent

		if (head >= m_capacity)
		{
			m_overwritten.fetch_add(1, std::memory_order_relaxed);
		}
		return true;
	}

	//! Consumer: ask the producer to stop writing while the window is read.
	//! The producer acknowledges inside its next push() - and if it never
	//! pushes again it is idle, which copyOut()'s sequence check also accepts.
	void beginSnapshot() noexcept
	{
		m_snapshotEpoch.fetch_add(1, std::memory_order_relaxed);
		m_snapshotPending.store(true, std::memory_order_release);
	}

	//! Consumer: true once the producer has seen this snapshot request.
	bool writerIdle() const noexcept
	{
		return m_snapshotPending.load(std::memory_order_acquire)
				&& m_writerAck.load(std::memory_order_acquire)
					== m_snapshotEpoch.load(std::memory_order_relaxed);
	}

	//! Consumer: copy the retained window, oldest first in arrival order, into
	//! \a dst - at most \a maxEvents of the oldest retained events. Returns the
	//! number copied; 0 when the ring is empty or no consistent window could be
	//! read within the retry budget (counted in refusedSnapshots()). Safe to
	//! call with or without a snapshot in flight.
	std::size_t copyOut(RetroMidiEvent* dst, std::size_t maxEvents) noexcept
	{
		if (dst == nullptr || maxEvents == 0) { return 0; }

		for (int attempt = 0; attempt < kSnapshotAttempts; ++attempt)
		{
			const auto sequence = m_sequence.load(std::memory_order_acquire);
			if ((sequence & 1u) != 0u) { continue; }  // a slot is being written

			const auto head = m_head.load(std::memory_order_acquire);
			const auto first = head > m_capacity ? head - m_capacity : 0;
			const auto retained = static_cast<std::size_t>(head - first);
			const auto count = std::min(retained, maxEvents);
			for (std::size_t i = 0; i < count; ++i)
			{
				dst[i] = m_slots[(first + i) & m_mask];
			}

			if (m_sequence.load(std::memory_order_acquire) == sequence)
			{
				return count;  // no producer write overlapped the copy
			}
		}

		m_refused.fetch_add(1, std::memory_order_relaxed);
		return 0;
	}

	//! Consumer: release the producer. Must follow beginSnapshot(), even when
	//! copyOut() was not reached - otherwise the producer drops every event.
	void endSnapshot() noexcept
	{
		m_snapshotPending.store(false, std::memory_order_release);
	}

	//! Events retained right now, clamped to capacity().
	std::size_t bufferedCount() const noexcept
	{
		const auto head = m_head.load(std::memory_order_acquire);
		return static_cast<std::size_t>(std::min<std::uint64_t>(head, m_capacity));
	}

	std::size_t capacity() const noexcept { return m_capacity; }

	//! Events a later push() overlapped: the part of the past that fell out of
	//! the window.
	std::uint64_t overwrittenCount() const noexcept
	{
		return m_overwritten.load(std::memory_order_relaxed);
	}

	//! Events dropped because a snapshot was in flight.
	std::uint64_t pausedDropCount() const noexcept
	{
		return m_pausedDrops.load(std::memory_order_relaxed);
	}

	//! Copies that could not obtain a consistent window within the retry budget.
	//! Reported rather than hidden: 0 in practice at MIDI event rates.
	std::uint64_t refusedSnapshots() const noexcept
	{
		return m_refused.load(std::memory_order_relaxed);
	}

private:
	//! Bounded attempts, so a consumer can never spin on a producer that is
	//! writing every slot faster than it can copy the window.
	static constexpr int kSnapshotAttempts = 64;

	static std::size_t nextPowerOfTwo(std::size_t value)
	{
		std::size_t result = 1;
		while (result < value) { result <<= 1; }
		return result;
	}

	const std::size_t m_capacity;
	const std::size_t m_mask;

	std::unique_ptr<RetroMidiEvent[]> m_slots;

	// One cache line each for the indices the two sides own, and one for the
	// three counters, so a producer write never invalidates the consumer's.
	alignas(64) std::atomic<std::uint64_t> m_head{0};
	alignas(64) std::atomic<std::uint64_t> m_sequence{0};
	alignas(64) std::atomic<std::uint64_t> m_overwritten{0};
	std::atomic<std::uint64_t> m_pausedDrops{0};
	std::atomic<std::uint64_t> m_refused{0};

	// The snapshot handshake: the consumer owns the epoch and the pending flag,
	// the producer owns the acknowledgement.
	alignas(64) std::atomic<std::uint32_t> m_snapshotEpoch{0};
	std::atomic<std::uint32_t> m_writerAck{0};
	std::atomic<bool> m_snapshotPending{false};
};


} // namespace lmms

#endif // LMMS_RETRO_MIDI_RING_H
