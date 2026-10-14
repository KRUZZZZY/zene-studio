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
#include <limits>

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
 *  - push() makes at most MaxPushAttempts reservation attempts. A full queue or
 *    exhausted contention budget refuses the event, counts it in dropped() and
 *    returns false, so a producer that cannot be served is never blocked;
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
	//! Maximum reservation attempts made by one push(), including the first attempt.
	static constexpr std::uint32_t MaxPushAttempts = 8;

	MidiHandoffQueue()
	{
		for (std::size_t i = 0; i < Capacity; ++i)
		{
			m_cells[i].sequence.store(i, std::memory_order_relaxed);
		}
	}

	MidiHandoffQueue(const MidiHandoffQueue&) = delete;
	MidiHandoffQueue& operator=(const MidiHandoffQueue&) = delete;

	//! Queues \a event. @return false when the queue was full or contention exhausted the retry budget
	auto push(const HandoffMidiEvent& event, std::uint64_t* refusedAt = nullptr) noexcept -> bool
	{
		return pushImpl(event, refusedAt, nullptr, [](std::uint32_t) noexcept {});
	}

#ifdef LMMS_TESTING
	//! Test seam that lets a queue test schedule a competing reservation before a producer's compare-exchange.
	template<typename BeforeCompareExchange>
	auto pushWithTestHook(const HandoffMidiEvent& event, std::uint64_t* refusedAt,
		std::uint32_t* attemptsUsed, BeforeCompareExchange&& beforeCompareExchange) noexcept -> bool
	{
		return pushImpl(event, refusedAt, attemptsUsed, static_cast<BeforeCompareExchange&&>(beforeCompareExchange));
	}
	#endif

private:
	template<typename BeforeCompareExchange>
	auto pushImpl(const HandoffMidiEvent& event, std::uint64_t* refusedAt, std::uint32_t* attemptsUsed,
		BeforeCompareExchange&& beforeCompareExchange) noexcept -> bool
	{
		std::size_t position = m_enqueue.load(std::memory_order_relaxed);
		for (std::uint32_t attempt = 1; attempt <= MaxPushAttempts; ++attempt)
		{
			if (attemptsUsed) { *attemptsUsed = attempt; }
			Cell& cell = m_cells[position & Mask];
			const std::size_t sequence = cell.sequence.load(std::memory_order_acquire);
			const auto difference = static_cast<std::intptr_t>(sequence)
				- static_cast<std::intptr_t>(position);
			if (difference == 0)
			{
				beforeCompareExchange(attempt);
				if (m_enqueue.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
				{
					cell.event = event;
					cell.sequence.store(position + 1, std::memory_order_release);
					return true;
				}
			}
			else if (difference < 0)
			{
				// The cell still holds an event that has not been popped: the queue is full. The position is the number
				// of events queued before this refusal, exactly.
				if (refusedAt) { *refusedAt = position; }
				m_dropped.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			else
			{
				position = m_enqueue.load(std::memory_order_relaxed);
			}
		}

		// Contention used the entire retry budget. Count this refusal just like a full-queue refusal and return
		// the latest reservation position so pushOrRecord() can preserve the overflow cut.
		if (refusedAt) { *refusedAt = m_enqueue.load(std::memory_order_relaxed); }
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

public:
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

	//! The number of events the queue holds at most.
	static constexpr std::size_t capacity() noexcept { return Capacity; }

	//! Events pushed successfully so far. A position in the stream: events are numbered in push order.
	auto enqueued() const noexcept -> std::uint64_t
	{
		return m_enqueue.load(std::memory_order_acquire);
	}

	//! Events popped so far (single consumer).
	auto consumed() const noexcept -> std::uint64_t
	{
		return m_dequeue.load(std::memory_order_acquire);
	}

	//! Events refused by push() because the queue was full or contention exhausted its retry budget.
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

//! Channels whose MIDI the full queue refused, each with the point in the stream where its cut belongs.
/*!
 * When push() refuses an event, the event's channel is recorded together with the number of events queued
 * before the refusal. The cut (all-notes-off for the channel) is due once the consumer has taken that many
 * events, so it lands exactly where the refused event would have been: after the events queued before it,
 * and before any queued after it. A later refusal on the same channel moves the cut later, never earlier.
 *
 * All operations are lock-free and allocate nothing. Each cut-stamp update makes at most
 * MaxStampUpdateAttempts compare-exchange attempts; a refusal is still counted if contention prevents its stamp
 * from being advanced. Producers record; the single consumer takes the due cuts with drainMidi().
 */
class MidiChannelOverflow
{
public:
	//! Bit 16 stands for "no channel" (an event with a negative channel): all channels are affected.
	static constexpr std::uint32_t AllChannels = 1u << 16;
	//! Maximum compare-exchange attempts used to advance one channel's cut stamp.
	static constexpr std::uint32_t MaxStampUpdateAttempts = 8;

	//! Records a refused event on \a channel. \a queuedBefore is the queue's enqueued() count at the refusal.
	void record(int channel, std::uint64_t queuedBefore) noexcept
	{
		recordImpl(channel, queuedBefore, nullptr,
			[](std::atomic<std::uint64_t>&, std::uint64_t, std::uint64_t, std::uint32_t) noexcept {});
	}

#ifdef LMMS_TESTING
	//! Test seam that creates deterministic stamp contention while using the normal refusal accounting path.
	template<typename BeforeCompareExchange>
	void recordWithTestHook(int channel, std::uint64_t queuedBefore, std::uint32_t* attemptsUsed,
		BeforeCompareExchange&& beforeCompareExchange) noexcept
	{
		recordImpl(channel, queuedBefore, attemptsUsed,
			static_cast<BeforeCompareExchange&&>(beforeCompareExchange));
	}
	#endif

	//! The stamp of the channel's pending cut, or 0. A cut is due when consumed() reaches stamp - 1.
	auto stamp(int channel) const noexcept -> std::uint64_t
	{
		return m_cutAt[static_cast<std::size_t>(channel)].load(std::memory_order_acquire);
	}

	//! Whether the channel's cut is due, given that \a consumed events have been taken from the queue.
	auto due(int channel, std::uint64_t consumed) const noexcept -> bool
	{
		const auto value = stamp(channel);
		return value != 0 && value - 1 <= consumed;
	}

	//! Clears a cut once it has been sent. A later refusal that moved the cut is kept.
	void clear(int channel, std::uint64_t sentStamp) noexcept
	{
		auto& cut = m_cutAt[static_cast<std::size_t>(channel)];
		std::uint64_t expected = sentStamp;
		cut.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
	}

	//! Events refused by a full queue, since construction.
	auto recorded() const noexcept -> std::uint64_t
	{
		return m_recorded.load(std::memory_order_relaxed);
	}

#ifdef LMMS_TESTING
	//! Refused events for which stamp contention exhausted the bounded update budget.
	auto failedStampUpdates() const noexcept -> std::uint64_t
	{
		return m_failedStampUpdates.load(std::memory_order_relaxed);
	}
#endif

private:
	template<typename BeforeCompareExchange>
	void recordImpl(int channel, std::uint64_t queuedBefore, std::uint32_t* attemptsUsed,
		BeforeCompareExchange&& beforeCompareExchange) noexcept
	{
		const std::uint64_t stamp = queuedBefore + 1; // 0 is "nothing pending"
		bool updated = true;
		if (channel >= 0 && channel < 16)
		{
			updated = raise(m_cutAt[static_cast<std::size_t>(channel)], stamp, attemptsUsed,
				static_cast<BeforeCompareExchange&&>(beforeCompareExchange));
		}
		else
		{
			for (auto& cut : m_cutAt)
			{
				if (!raise(cut, stamp, attemptsUsed, beforeCompareExchange)) { updated = false; }
			}
		}
		if (!updated) { m_failedStampUpdates.fetch_add(1, std::memory_order_relaxed); }
		m_recorded.fetch_add(1, std::memory_order_relaxed);
	}

	template<typename BeforeCompareExchange>
	static auto raise(std::atomic<std::uint64_t>& cut, std::uint64_t stamp, std::uint32_t* attemptsUsed,
		BeforeCompareExchange&& beforeCompareExchange) noexcept -> bool
	{
		auto current = cut.load(std::memory_order_relaxed);
		for (std::uint32_t attempt = 1; current < stamp && attempt <= MaxStampUpdateAttempts; ++attempt)
		{
			if (attemptsUsed) { *attemptsUsed = attempt; }
			beforeCompareExchange(cut, current, stamp, attempt);
			if (cut.compare_exchange_weak(current, stamp, std::memory_order_acq_rel)) { return true; }
		}
		return current >= stamp;
	}

	std::array<std::atomic<std::uint64_t>, 16> m_cutAt{};
	std::atomic<std::uint64_t> m_recorded{0};
	std::atomic<std::uint64_t> m_failedStampUpdates{0};
};

//! Pushes \a event, or records its channel (with the queue's position) when push() refuses it. Never blocks.
template<std::size_t Capacity>
auto pushOrRecord(MidiHandoffQueue<Capacity>& queue, MidiChannelOverflow& overflow,
	const HandoffMidiEvent& event) noexcept -> bool
{
	std::uint64_t queuedBefore = 0;
	if (queue.push(event, &queuedBefore)) { return true; }
	overflow.record(event.channel, queuedBefore);
	return false;
}

#ifdef LMMS_TESTING
//! Exercises queue exhaustion and overflow recording while injecting deterministic contention in both stages.
template<std::size_t Capacity, typename BeforeQueueCompareExchange, typename BeforeStampCompareExchange>
auto pushOrRecordWithTestHooks(MidiHandoffQueue<Capacity>& queue, MidiChannelOverflow& overflow,
	const HandoffMidiEvent& event, std::uint32_t* pushAttempts, std::uint32_t* stampAttempts,
	BeforeQueueCompareExchange&& beforeQueueCompareExchange,
	BeforeStampCompareExchange&& beforeStampCompareExchange) noexcept -> bool
{
	std::uint64_t queuedBefore = 0;
	if (queue.pushWithTestHook(event, &queuedBefore, pushAttempts,
		static_cast<BeforeQueueCompareExchange&&>(beforeQueueCompareExchange))) { return true; }
	overflow.recordWithTestHook(event.channel, queuedBefore, stampAttempts,
		static_cast<BeforeStampCompareExchange&&>(beforeStampCompareExchange));
	return false;
}
#endif

//! Drains the queue in order, for the single consumer. At most \a maxEvents events are taken per call; the rest
//! stay queued, so the work per period is bounded.
/*!
 * \a emitEvent(const HandoffMidiEvent&) -> bool sends one event. It returns false when the event could not be
 * sent; the emitter then keeps it (for instance in a pending frame), and the drain stops.
 * \a emitCut(int channel) -> bool sends a channel's all-notes-off. It returns false when it could not be sent,
 * and the drain stops with the cut still pending.
 *
 * A cut is sent before the first event that was queued after its refusal, so a note started after the refusal
 * is not cut by it. \a maxEvents caps queue events taken; \a maxEmittedEvents caps both queue events and cuts
 * successfully emitted. Remaining events and cuts stay queued or pending for the next call.
 */
template<std::size_t Capacity, typename EmitEvent, typename EmitCut>
void drainMidi(MidiHandoffQueue<Capacity>& queue, MidiChannelOverflow& overflow, std::size_t maxEvents,
	EmitEvent&& emitEvent, EmitCut&& emitCut,
	std::size_t maxEmittedEvents = std::numeric_limits<std::size_t>::max())
{
	std::size_t taken = 0;
	std::size_t emitted = 0;
	for (;;)
	{
		const auto consumed = queue.consumed();
		for (int channel = 0; channel < 16; ++channel)
		{
			if (!overflow.due(channel, consumed)) { continue; }
			if (emitted >= maxEmittedEvents) { return; }
			const auto sentStamp = overflow.stamp(channel);
			if (!emitCut(channel)) { return; }
			++emitted;
			overflow.clear(channel, sentStamp);
		}
		if (taken >= maxEvents || emitted >= maxEmittedEvents) { return; }

		HandoffMidiEvent event;
		if (!queue.pop(event)) { return; }
		++taken;
		if (!emitEvent(event)) { return; }
		++emitted;
	}
}

} // namespace lmms

#endif // LMMS_MIDI_HANDOFF_QUEUE_H
