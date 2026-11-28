/*
 * MidiOutQueue.h - fixed-capacity SPSC queue for realtime MIDI output.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or later.
 */

#ifndef LMMS_MIDI_OUT_QUEUE_H
#define LMMS_MIDI_OUT_QUEUE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "MidiEvent.h"
#include "TimePos.h"

namespace lmms
{

class MidiPort;

//! A preallocated, lock-free single-producer/single-consumer MIDI output queue.
class MidiOutQueue
{
public:
	//! How many events the producer may have outstanding before push() refuses an ordinary one. This is the
	//! limit the queue has always had: a producer of note-ons, controllers, clock or sysex sees no change.
	static constexpr std::size_t Capacity = 1024;

	/*! Q3244: slots past Capacity that only a note-off may take. A refused note-off is the one refusal the
	 *  receiver cannot undo by itself: it holds the note until something releases it, and nothing ever sends a
	 *  second note-off for it, so a burst that fills the output queue used to leave that note sounding until
	 *  the device was reset. The reserve is note-off priority: with the ordinary slots full a note-off still
	 *  enters, in the SAME ring and therefore still in production order - a note-off can never overtake an
	 *  event produced after it, nor be overtaken by one produced before it. Only when the reserve is full too
	 *  is a note-off refused, and push() counts that; that needs Capacity pending note-offs, which at any real
	 *  event rate is a stall of seconds rather than a burst. */
	static constexpr std::size_t NoteOffReserve = Capacity;

	//! The ring's real size: the ordinary slots plus the note-off reserve. A power of two, so the slot index
	//! stays a mask on the audio thread.
	static constexpr std::size_t Slots = Capacity + NoteOffReserve;
	static_assert( ( Slots & ( Slots - 1 ) ) == 0, "MidiOutQueue needs a power-of-two ring" );

	struct Command
	{
		MidiEvent event;
		TimePos time;
		const MidiPort* port = nullptr;
		//! Q050-246: the steady-clock instant (ns since its epoch) the event is due, from the frame offset it was
		//! produced at inside its render period; 0 = no frame position (sent the way it always was).
		std::int64_t dueNs = 0;
	};

	static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
		"MidiOutQueue requires lock-free 64-bit atomics");

	//! Producer side (the audio thread). Never waits, allocates, or locks.
	bool push(const MidiEvent& event, const TimePos& time, const MidiPort* port, std::int64_t dueNs = 0) noexcept
	{
		const auto write = m_write.load(std::memory_order_relaxed);
		const auto read = m_read.load(std::memory_order_acquire);
		// Q3244: a note-off may also use the reserve, so a full queue (and the burst behind it) costs no note
		// its release. Every other event stops at Capacity, exactly as before.
		const std::size_t limit = event.type() == MidiNoteOff ? Slots : Capacity;
		if (write - read >= limit)
		{
			m_overflow.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		m_slots[write & (Slots - 1)] = Command{event, time, port, dueNs};
		m_write.store(write + 1, std::memory_order_release);
		return true;
	}

	//! Producer side, for a consumer that sleeps in poll() (cluster 5129). Pushes exactly as push() does and
	//! returns true when the producer must wake the consumer: the push was accepted and no wake has been
	//! requested since the consumer's last acknowledgeWake(). One wake per drain, not one per event.
	//! Never waits, allocates, or locks.
	bool pushAndRequestWake(const MidiEvent& event, const TimePos& time, const MidiPort* port,
		std::int64_t dueNs = 0) noexcept
	{
		if (!push(event, time, port, dueNs)) { return false; }
		return !m_wakePending.exchange(true, std::memory_order_seq_cst);
	}

	//! Consumer side: call before draining, so the next accepted push requests a wake again.
	void acknowledgeWake() noexcept
	{
		m_wakePending.store(false, std::memory_order_seq_cst);
	}

	//! Consumer side (the MIDI/sequencer thread). Never allocates or locks.
	bool pop(Command& command) noexcept
	{
		const auto read = m_read.load(std::memory_order_relaxed);
		if (read == m_write.load(std::memory_order_acquire)) { return false; }
		command = m_slots[read & (Slots - 1)];
		m_read.store(read + 1, std::memory_order_release);
		return true;
	}

	//! How many events the queue refused: an ordinary event past Capacity, a note-off past Capacity plus the
	//! note-off reserve. A refusal is the only event the queue cannot deliver.
	std::uint64_t overflowCount() const noexcept
	{
		return m_overflow.load(std::memory_order_relaxed);
	}

private:
	std::array<Command, Slots> m_slots{};
	alignas(64) std::atomic<std::uint64_t> m_write{0};
	alignas(64) std::atomic<std::uint64_t> m_read{0};
	std::atomic<std::uint64_t> m_overflow{0};
	//! Set by the first accepted push after an acknowledgeWake(); see pushAndRequestWake().
	std::atomic<bool> m_wakePending{false};
};

} // namespace lmms

#endif // LMMS_MIDI_OUT_QUEUE_H
