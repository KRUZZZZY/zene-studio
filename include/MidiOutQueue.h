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
	static constexpr std::size_t Capacity = 1024;

	struct Command
	{
		MidiEvent event;
		TimePos time;
		const MidiPort* port = nullptr;
	};

	static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
		"MidiOutQueue requires lock-free 64-bit atomics");

	//! Producer side (the audio thread). Never waits, allocates, or locks.
	bool push(const MidiEvent& event, const TimePos& time, const MidiPort* port) noexcept
	{
		const auto write = m_write.load(std::memory_order_relaxed);
		const auto read = m_read.load(std::memory_order_acquire);
		if (write - read >= Capacity)
		{
			m_overflow.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		m_slots[write & (Capacity - 1)] = Command{event, time, port};
		m_write.store(write + 1, std::memory_order_release);
		return true;
	}

	//! Consumer side (the MIDI/sequencer thread). Never allocates or locks.
	bool pop(Command& command) noexcept
	{
		const auto read = m_read.load(std::memory_order_relaxed);
		if (read == m_write.load(std::memory_order_acquire)) { return false; }
		command = m_slots[read & (Capacity - 1)];
		m_read.store(read + 1, std::memory_order_release);
		return true;
	}

	std::uint64_t overflowCount() const noexcept
	{
		return m_overflow.load(std::memory_order_relaxed);
	}

private:
	std::array<Command, Capacity> m_slots{};
	alignas(64) std::atomic<std::uint64_t> m_write{0};
	alignas(64) std::atomic<std::uint64_t> m_read{0};
	std::atomic<std::uint64_t> m_overflow{0};
};

} // namespace lmms

#endif // LMMS_MIDI_OUT_QUEUE_H
