/*
 * MpscQueue.h - a bounded, lock-free multi-producer single-consumer queue
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

#ifndef LMMS_MPSC_QUEUE_H
#define LMMS_MPSC_QUEUE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace lmms
{

/*! The standard bounded MPSC ring with a sequence number per slot: push() from any number of
 *  threads (the audio thread included), pop() from one. Each operation is one compare-exchange
 *  plus plain/release stores: no allocation, no lock, and a full queue refuses instead of
 *  growing or blocking. @a Capacity must be a power of two. */
template <typename T, std::size_t Capacity>
class MpscQueue
{
	static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");  // 0 passed the mask test
	static_assert(std::is_trivially_copyable_v<T>, "T is copied into fixed storage");

public:
	MpscQueue()
	{
		for (std::size_t i = 0; i < Capacity; ++i) { m_slots[i].sequence.store(i, std::memory_order_relaxed); }
	}

	//! Any thread. False when the queue is full (the value is dropped).
	bool push(const T& value) noexcept
	{
		std::size_t position = m_tail.load(std::memory_order_relaxed);
		for (;;)
		{
			Slot& slot = m_slots[position & (Capacity - 1)];
			const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
			const auto difference = static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(position);
			if (difference == 0)
			{
				if (m_tail.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
				{
					slot.value = value;
					slot.sequence.store(position + 1, std::memory_order_release);
					return true;
				}
			}
			else if (difference < 0) { return false; }
			else { position = m_tail.load(std::memory_order_relaxed); }
		}
	}

	//! The one consumer thread. False when empty.
	bool pop(T* value) noexcept
	{
		Slot& slot = m_slots[m_head & (Capacity - 1)];
		const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
		if (static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(m_head + 1) < 0) { return false; }
		*value = slot.value;
		slot.sequence.store(m_head + Capacity, std::memory_order_release);
		++m_head;
		return true;
	}

private:
	struct Slot
	{
		std::atomic<std::size_t> sequence{0};
		T value{};
	};
	std::array<Slot, Capacity> m_slots;
	alignas(64) std::atomic<std::size_t> m_tail{0};
	alignas(64) std::size_t m_head = 0;
};

} // namespace lmms

#endif // LMMS_MPSC_QUEUE_H
