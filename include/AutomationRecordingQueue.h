/*
 * AutomationRecordingQueue.h - fixed SPSC handoff for recorded automation
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_AUTOMATION_RECORDING_QUEUE_H
#define LMMS_AUTOMATION_RECORDING_QUEUE_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "LmmsTypes.h"
#include "TimePos.h"

namespace lmms
{

//! Preallocated SPSC queue shared by one audio producer and the control thread.
class AutomationRecordingQueue
{
public:
	static constexpr std::size_t Capacity = 4096;
	static_assert((Capacity & (Capacity - 1)) == 0, "queue capacity must be a power of two");
	static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
		"automation recording cursors must be lock-free");

	struct Event
	{
		jo_id_t clipId = 0;
		TimePos time;
		float value = 0.0f;
	};

	//! Audio producer. Drop-newest at capacity; no allocation, lock, or wait.
	bool push(jo_id_t clipId, const TimePos& time, float value) noexcept
	{
		const auto write = m_writeCursor.load(std::memory_order_relaxed);
		const auto read = m_readCursor.load(std::memory_order_acquire);
		if (write - read >= Capacity)
		{
			m_overflow.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		m_events[write & (Capacity - 1)] = Event{clipId, time, value};
		m_writeCursor.store(write + 1, std::memory_order_release);
		return true;
	}

	//! Control consumer. The event remains in the ring until its snapshot is published.
	bool peekApplied(Event& event, std::uint64_t& sequence) const noexcept
	{
		const auto write = m_writeCursor.load(std::memory_order_acquire);
		if (m_appliedCursor >= write) { return false; }
		event = m_events[m_appliedCursor & (Capacity - 1)];
		sequence = m_appliedCursor + 1;
		return true;
	}

	void markApplied() noexcept { ++m_appliedCursor; }

	//! Reuse published slots only after the snapshot readers that could see old watermarks leave.
	void reclaimApplied(bool readersQuiescent) noexcept
	{
		if (readersQuiescent) { m_readCursor.store(m_appliedCursor, std::memory_order_release); }
	}

	//! Find the newest not-yet-published event for a clip at or before a read position.
	bool latestPending(jo_id_t clipId, std::uint64_t appliedSequence, const TimePos& time,
		Event& event, std::uint64_t& sequence) const noexcept
	{
		const auto read = m_readCursor.load(std::memory_order_acquire);
		const auto write = m_writeCursor.load(std::memory_order_acquire);
		const auto begin = std::max(read, appliedSequence);
		for (auto cursor = write; cursor > begin; --cursor)
		{
			const auto index = cursor - 1;
			const Event candidate = m_events[index & (Capacity - 1)];
			if (candidate.clipId == clipId && candidate.time <= time)
			{
				event = candidate;
				sequence = index + 1;
				return true;
			}
		}
		return false;
	}

	//! Find the closest pending point on each side of a read position.
	void pendingBounds(jo_id_t clipId, std::uint64_t appliedSequence, const TimePos& time,
		Event& before, std::uint64_t& beforeSequence, bool& hasBefore,
		Event& after, std::uint64_t& afterSequence, bool& hasAfter) const noexcept
	{
		hasBefore = false;
		hasAfter = false;
		const auto read = m_readCursor.load(std::memory_order_acquire);
		const auto write = m_writeCursor.load(std::memory_order_acquire);
		const auto begin = std::max(read, appliedSequence);
		for (auto cursor = begin; cursor < write; ++cursor)
		{
			const Event candidate = m_events[cursor & (Capacity - 1)];
			if (candidate.clipId != clipId) { continue; }
			const auto sequence = cursor + 1;
			if (candidate.time <= time)
			{
				updateBefore(candidate, sequence, before, beforeSequence, hasBefore);
			}
			else
			{
				updateAfter(candidate, sequence, after, afterSequence, hasAfter);
			}
		}
	}

	std::uint64_t overflowCount() const noexcept { return m_overflow.load(std::memory_order_relaxed); }
	std::uint64_t readCursor() const noexcept { return m_readCursor.load(std::memory_order_acquire); }
	std::uint64_t writeCursor() const noexcept { return m_writeCursor.load(std::memory_order_acquire); }

private:
	static void updateBefore(const Event& candidate, std::uint64_t sequence, Event& before,
		std::uint64_t& beforeSequence, bool& hasBefore) noexcept
	{
		if (!hasBefore || candidate.time > before.time
			|| (candidate.time == before.time && sequence > beforeSequence))
		{
			before = candidate;
			beforeSequence = sequence;
			hasBefore = true;
		}
	}

	static void updateAfter(const Event& candidate, std::uint64_t sequence, Event& after,
		std::uint64_t& afterSequence, bool& hasAfter) noexcept
	{
		if (!hasAfter || candidate.time < after.time
			|| (candidate.time == after.time && sequence > afterSequence))
		{
			after = candidate;
			afterSequence = sequence;
			hasAfter = true;
		}
	}

	std::array<Event, Capacity> m_events{};
	alignas(64) std::atomic<std::uint64_t> m_writeCursor{0};
	alignas(64) std::atomic<std::uint64_t> m_readCursor{0};
	std::atomic<std::uint64_t> m_overflow{0};
	//! Control-thread-only cursor: entries here have already been applied and published.
	std::uint64_t m_appliedCursor = 0;
};

} // namespace lmms

#endif // LMMS_AUTOMATION_RECORDING_QUEUE_H
