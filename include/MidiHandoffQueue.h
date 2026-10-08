/*
 * MidiHandoffQueue.h - bounded, lock-free hand-off of MIDI events between the
 *                      threads that send them and the audio thread that plays them
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

#ifndef LMMS_MIDI_HANDOFF_QUEUE_H
#define LMMS_MIDI_HANDOFF_QUEUE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace lmms
{

//! One MIDI event as plain data, so it can cross a thread boundary without pointers or allocation.
/*!
 * It carries what the instrument hosts forward to their plugins: the type, the
 * channel, the two data bytes (MidiEvent::param()) and the frame offset within
 * the period. The source pointer and the SysEx payload are not kept.
 */
struct HandoffMidiEvent
{
	std::uint8_t type = 0;
	std::int8_t channel = 0;
	std::int16_t param1 = 0;
	std::int16_t param2 = 0;
	std::uint32_t offset = 0;
};

/*!
 * Bounded multi-producer queue (Vyukov's bounded queue; each cell carries its own
 * sequence number, so producers and the consumer never wait for each other).
 *
 * The contract, which is what makes it safe to use from the audio thread:
 *  - storage is the fixed array inside the object: nothing allocates, nothing locks;
 *  - push() never waits. A full queue refuses the event, counts it in dropped()
 *    and returns false, so a producer that cannot be served is never blocked;
 *  - pop() never waits and returns false when the queue is empty.
 *
 * Events come out in the order their pushes completed. Any number of threads may
 * push, and any number may pop, although the hosts pop from one thread.
 *
 * @tparam Capacity number of events; must be a power of two
 */
template<std::size_t Capacity>
class MidiHandoffQueue
{
	static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
		"the capacity must be a power of two, at least 2");

public:
	MidiHandoffQueue()
	{
		for (std::size_t i = 0; i < Capacity; ++i)
		{
			m_cells[i].sequence.store(i, std::memory_order_relaxed);
		}
	}

	MidiHandoffQueue(const MidiHandoffQueue&) = delete;
	MidiHandoffQueue& operator=(const MidiHandoffQueue&) = delete;

	//! Queues \a event. @return false when the queue was full and the event was dropped
	auto push(const HandoffMidiEvent& event) noexcept -> bool
	{
		std::size_t position = m_enqueue.load(std::memory_order_relaxed);
		for (;;)
		{
			Cell& cell = m_cells[position & Mask];
			const std::size_t sequence = cell.sequence.load(std::memory_order_acquire);
			const auto difference = static_cast<std::intptr_t>(sequence)
				- static_cast<std::intptr_t>(position);
			if (difference == 0)
			{
				if (m_enqueue.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
				{
					cell.event = event;
					cell.sequence.store(position + 1, std::memory_order_release);
					return true;
				}
			}
			else if (difference < 0)
			{
				// The cell still holds an event that has not been popped: the queue is full.
				m_dropped.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			else
			{
				position = m_enqueue.load(std::memory_order_relaxed);
			}
		}
	}

	//! Takes the oldest queued event into \a event. @return false when the queue is empty
	auto pop(HandoffMidiEvent& event) noexcept -> bool
	{
		std::size_t position = m_dequeue.load(std::memory_order_relaxed);
		for (;;)
		{
			Cell& cell = m_cells[position & Mask];
			const std::size_t sequence = cell.sequence.load(std::memory_order_acquire);
			const auto difference = static_cast<std::intptr_t>(sequence)
				- static_cast<std::intptr_t>(position + 1);
			if (difference == 0)
			{
				if (m_dequeue.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
				{
					event = cell.event;
					cell.sequence.store(position + Capacity, std::memory_order_release);
					return true;
				}
			}
			else if (difference < 0)
			{
				return false;
			}
			else
			{
				position = m_dequeue.load(std::memory_order_relaxed);
			}
		}
	}

	//! Events refused by push() because the queue was full, since construction.
	auto dropped() const noexcept -> std::uint64_t
	{
		return m_dropped.load(std::memory_order_relaxed);
	}

private:
	static constexpr std::size_t Mask = Capacity - 1;

	struct Cell
	{
		std::atomic<std::size_t> sequence{0};
		HandoffMidiEvent event{};
	};

	std::array<Cell, Capacity> m_cells{};
	alignas(64) std::atomic<std::size_t> m_enqueue{0};
	alignas(64) std::atomic<std::size_t> m_dequeue{0};
	std::atomic<std::uint64_t> m_dropped{0};
};

} // namespace lmms

#endif // LMMS_MIDI_HANDOFF_QUEUE_H
