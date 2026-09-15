/*
 * RetroAudioRing.h - bounded ring of the most recent AUDIO frames, off by
 *                    default, single producer / single consumer: the audio
 *                    counterpart of include/RetroMidiRing.h (0.3.0, feature
 *                    row 16 "Retrospective audio capture").
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
 */

#ifndef LMMS_RETRO_AUDIO_RING_H
#define LMMS_RETRO_AUDIO_RING_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "SampleFrame.h"

namespace lmms
{


//! Bounded ring of the most recent AUDIO frames; single producer, single
//! consumer.
/*!
 * The properties are copied from RetroMidiRing, which itself copies them from
 * RecordRingBuffer, the programme's precedent (include/RecordRingBuffer.h):
 * storage allocated exactly once in the constructor, which must run off the
 * audio thread; one producer-owned head; release/acquire hand-off; a monotonic
 * counter for what was lost; no locks, no syscalls, no allocation and O(1) work
 * per frame on the producer path.
 *
 * The two properties that matter for AUDIO, and why they are not the MIDI
 * ring's:
 *
 *  - the element is a SampleFrame (two floats, include/SampleFrame.h), so the
 *    window is 8 bytes per frame and the capacity is a FRAME count. The point
 *    of retrospective capture on the audio side is the seconds you played
 *    before you pressed record, so the default capacity is expressed in
 *    seconds at 48 kHz (see RetroAudioCapture::DefaultCapacityFrames);
 *  - the policy is drop-OLDEST, exactly as the MIDI ring's: the producer owns
 *    one monotonically increasing head and overwrites slot (head & mask), so
 *    the ring always holds the most recent capacity() frames and the consumer
 *    clamps the window it may read to the last capacity() frames by arithmetic.
 *
 * The consumer's copy is guarded by a publication sequence, so a copy is never
 * torn even while the producer is running (see copyOut()).
 */
class RetroAudioRing
{
public:
	//! Allocates storage for at least \a minCapacityFrames frames, rounded up
	//! to a power of two. Must run off the audio thread.
	explicit RetroAudioRing(std::size_t minCapacityFrames) :
		m_capacity(nextPowerOfTwo(minCapacityFrames == 0 ? 1 : minCapacityFrames)),
		m_mask(m_capacity - 1),
		m_slots(std::make_unique<SampleFrame[]>(m_capacity))
	{
	}

	RetroAudioRing(const RetroAudioRing&) = delete;
	RetroAudioRing& operator=(const RetroAudioRing&) = delete;

	//! Producer: record \a count frames. Returns the number of frames stored
	//! (0 when a snapshot is in flight). Realtime-safe: no allocation, no lock,
	//! no syscall, O(frames); one relaxed atomic load on the fast path.
	std::size_t push(const SampleFrame* frames, std::size_t count) noexcept
	{
		if (frames == nullptr || count == 0)
		{
			return 0;
		}
		if (m_snapshotPending.load(std::memory_order_relaxed))
		{
			m_pausedDrops.fetch_add(static_cast<std::uint64_t>(count), std::memory_order_relaxed);
			m_writerAck.store(m_snapshotEpoch.load(std::memory_order_acquire),
				std::memory_order_release);
			return 0;
		}

		const auto head = m_head.load(std::memory_order_relaxed);
		const auto sequence = m_sequence.load(std::memory_order_relaxed);
		m_sequence.store(sequence + 1, std::memory_order_release);  // odd: writing
		for (std::size_t i = 0; i < count; ++i)
		{
			m_slots[(head + i) & m_mask] = frames[i];
		}
		m_head.store(head + count, std::memory_order_release);
		m_sequence.store(sequence + 2, std::memory_order_release);  // even: consistent

		if (head + count > m_capacity)
		{
			// The part of the window that fell out of the past. Counted per
			// frame, because for audio the LOST TIME is the honest unit.
			const auto lost = std::min<std::uint64_t>(count, head + count - m_capacity);
			m_overwritten.fetch_add(lost, std::memory_order_relaxed);
		}
		return count;
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

	//! Consumer: copy the retained window, oldest first, into \a dst - at most
	//! \a maxFrames of the oldest retained frames. Returns the number copied; 0
	//! when the ring is empty or no consistent window could be read within the
	//! retry budget (counted in refusedSnapshots()). Safe to call with or
	//! without a snapshot in flight.
	std::size_t copyOut(SampleFrame* dst, std::size_t maxFrames) noexcept
	{
		if (dst == nullptr || maxFrames == 0)
		{
			return 0;
		}

		for (int attempt = 0; attempt < kSnapshotAttempts; ++attempt)
		{
			const auto sequence = m_sequence.load(std::memory_order_acquire);
			if ((sequence & 1u) != 0u) { continue; }  // the producer is writing

			const auto head = m_head.load(std::memory_order_acquire);
			const auto first = head > m_capacity ? head - m_capacity : 0;
			const auto retained = static_cast<std::size_t>(head - first);
			const auto count = std::min(retained, maxFrames);
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
	//! copyOut() was not reached - otherwise the producer drops every frame.
	void endSnapshot() noexcept
	{
		m_snapshotPending.store(false, std::memory_order_release);
	}

	//! Frames retained right now, clamped to capacity().
	std::size_t bufferedCount() const noexcept
	{
		const auto head = m_head.load(std::memory_order_acquire);
		return static_cast<std::size_t>(std::min<std::uint64_t>(head, m_capacity));
	}

	std::size_t capacity() const noexcept { return m_capacity; }

	//! Frames a later push() overwrote: the part of the past that fell out of
	//! the window. This is the number that says "your window is shorter than
	//! the time you have been playing".
	std::uint64_t overwrittenCount() const noexcept
	{
		return m_overwritten.load(std::memory_order_relaxed);
	}

	//! Frames dropped because a snapshot was in flight.
	std::uint64_t pausedDropCount() const noexcept
	{
		return m_pausedDrops.load(std::memory_order_relaxed);
	}

	//! Copies that could not obtain a consistent window within the retry budget.
	std::uint64_t refusedSnapshots() const noexcept
	{
		return m_refused.load(std::memory_order_relaxed);
	}

	//! Consumer, with neither side running: discard the window.
	void reset() noexcept
	{
		m_head.store(0, std::memory_order_release);
		m_sequence.store(0, std::memory_order_release);
		m_overwritten.store(0, std::memory_order_relaxed);
		m_pausedDrops.store(0, std::memory_order_relaxed);
		m_refused.store(0, std::memory_order_relaxed);
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

	std::unique_ptr<SampleFrame[]> m_slots;

	// One cache line each for the indices the two sides own, and one for the
	// counters, so a producer write never invalidates the consumer's.
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
} ;


} // namespace lmms

#endif // LMMS_RETRO_AUDIO_RING_H
