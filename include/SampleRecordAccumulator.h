/*
 * SampleRecordAccumulator.h - background take builder for the clip record path
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

#ifndef LMMS_SAMPLE_RECORD_ACCUMULATOR_H
#define LMMS_SAMPLE_RECORD_ACCUMULATOR_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "LmmsTypes.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "SampleFrameRingBuffer.h"

namespace lmms
{


//! Take state shared between an accumulator and its drain thread through a
//! shared_ptr, so the thread never dereferences the accumulator (which may be
//! being destroyed) after finish() has been signalled.
struct SampleRecordTake
{
	std::vector<SampleFrame> frames;
	std::shared_ptr<const SampleBuffer> buffer;
};


//! A run of frames the capture ring could not take: \a length frames were lost after the first
//! \a position staged frames. The drain thread pads \a length silent frames at that point.
struct SampleRecordGap
{
	std::uint64_t position = 0;
	std::uint64_t length = 0;
};


//! The gap table between the audio thread (note) and the drain thread (takeDue).
//!
//! Records are published in order. The newest record stays open: a loss that finds the table
//! full is added to it, so no frame is ever uncounted, and the take keeps its length. The
//! consumer never reads the newest record until a newer one exists or the take is finishing, so
//! a merge can never race a read. A merged loss sits at the newest record's position: when the
//! table is full and frames were staged after that record, the padding lands earlier than the
//! loss by those frames (only after the drain has stalled for that many records).
class SampleRecordGaps
{
public:
	static constexpr std::size_t DefaultSlots = 256;

	explicit SampleRecordGaps(std::size_t capacity = DefaultSlots) :
		m_slots(std::make_unique<Slot[]>(nonZero(capacity))),
		m_size(nonZero(capacity))
	{
	}

	//! Audio thread, single producer, never blocks and never allocates.
	void note(std::uint64_t position, std::uint64_t length) noexcept
	{
		const auto published = m_published.load(std::memory_order_relaxed);
		if (published - m_read.load(std::memory_order_acquire) < m_size)
		{
			Slot& slot = m_slots[published % m_size];
			slot.position = position;
			slot.length.store(length, std::memory_order_relaxed);
			m_published.store(published + 1, std::memory_order_release);
			return;
		}
		// Full: the newest record is unread (see the class comment), so it is open to merge into.
		m_slots[(published - 1) % m_size].length.fetch_add(length, std::memory_order_relaxed);
		m_merged.fetch_add(1, std::memory_order_relaxed);
	}

	//! Drain thread, single consumer: the oldest gap that is due, i.e. whose position the drain
	//! has reached (\a framesRead) and that cannot grow any more (it is not the newest record,
	//! unless \a finishing). Pops it.
	std::optional<SampleRecordGap> takeDue(std::uint64_t framesRead, bool finishing) noexcept
	{
		const auto read = m_read.load(std::memory_order_relaxed);
		const auto published = m_published.load(std::memory_order_acquire);
		if (read == published) { return std::nullopt; }
		if (read + 1 == published && !finishing) { return std::nullopt; }
		const Slot& slot = m_slots[read % m_size];
		if (slot.position > framesRead) { return std::nullopt; }
		const SampleRecordGap gap{slot.position, slot.length.load(std::memory_order_acquire)};
		m_read.store(read + 1, std::memory_order_release);
		return gap;
	}

	//! Losses that were merged into an open record because the table was full.
	std::uint64_t merged() const noexcept
	{
		return m_merged.load(std::memory_order_relaxed);
	}

private:
	static constexpr std::size_t nonZero(std::size_t n) noexcept { return n > 0 ? n : 1; }

	struct Slot
	{
		std::uint64_t position = 0;
		std::atomic<std::uint64_t> length{0};
	};
	std::unique_ptr<Slot[]> m_slots;
	std::size_t m_size;
	std::atomic<std::uint64_t> m_published{0};  //!< producer: records stored
	std::atomic<std::uint64_t> m_read{0};       //!< consumer: records popped
	std::atomic<std::uint64_t> m_merged{0};
};


//! Accumulates a clip recording without doing RT-unsafe work on the audio thread.
/*!
 * The pre-existing clip record path (`SampleRecordHandle`) allocated one
 * `new SampleFrame[framesPerPeriod]` **per rendered period** on the audio
 * thread and kept every block in a `QList`, then assembled the whole take
 * into a `SampleBuffer` on the audio thread when the handle was destroyed.
 * That is an allocation on a real-time thread ~86 times a second, plus
 * unbounded growth and a take-sized allocation at stop.
 *
 * This class replaces both halves:
 *
 * - the audio thread only calls append(), which is a store into a
 *   fixed-capacity, pre-allocated `SampleFrameRingBuffer` - no allocation,
 *   no locking, no syscalls, no unbounded growth. Frames that do not fit are
 *   dropped, counted in framesDropped(), and recorded as a gap;
 * - a dedicated drain thread owns the growing take, pads each gap with silence
 *   where it was lost, and builds the `SampleBuffer`, so every large allocation
 *   happens off the audio thread.
 *
 * The result is handed back by finish(), which performs no allocation on the
 * calling thread. SampleBuffer's sample rate is captured at construction time
 * (the caller must build the accumulator while the engine's input rate is the
 * rate the take is recorded at).
 */
class SampleRecordAccumulator
{
public:
	//! Ring capacity: ~0.34 s at 48 kHz, drained every 20 ms when idle by the drain
	//! thread, so it only overflows if the drain thread is starved for that
	//! long. Two channels * 4 bytes * 16384 = 128 KiB, allocated once.
	static constexpr std::size_t DefaultRingFrames = 16384;
	//! Drain scratch size, owned by the drain thread.
	static constexpr std::size_t DrainBatchFrames = 4096;

	explicit SampleRecordAccumulator(sample_rate_t sampleRate,
			std::size_t ringFrames = DefaultRingFrames,
			std::size_t gapSlots = SampleRecordGaps::DefaultSlots) :
		m_sampleRate(sampleRate),
		m_ring(std::make_unique<SampleFrameRingBuffer>(ringFrames)),
		m_scratch(DrainBatchFrames),
		m_take(std::make_shared<SampleRecordTake>()),
		m_gaps(std::make_unique<SampleRecordGaps>(gapSlots))
	{
		m_drainThread = std::thread(&SampleRecordAccumulator::drainLoop, this);
	}

	~SampleRecordAccumulator()
	{
		finishAndJoin();
	}

	SampleRecordAccumulator(const SampleRecordAccumulator&) = delete;
	SampleRecordAccumulator& operator=(const SampleRecordAccumulator&) = delete;

	//! Audio-thread entry point: stage \a frames stereo frames for the take.
	//! Frames the ring cannot take are recorded as a gap, to be padded with silence.
	//! Realtime-safe by contract - see the class comment.
	void append(const SampleFrame* frames, f_cnt_t count) noexcept
	{
		const auto written = m_ring->writeBlock(frames, static_cast<std::size_t>(count));
		if (written > 0)
		{
			m_framesStaged.fetch_add(written, std::memory_order_relaxed);
		}
		if (written < static_cast<std::size_t>(count))
		{
			m_gaps->note(m_framesStaged.load(std::memory_order_relaxed),
				static_cast<std::uint64_t>(count) - written);
		}
	}

	//! Frames of silence the take holds in place of dropped input. Valid after finish().
	std::uint64_t framesPadded() const noexcept
	{
		return m_framesPadded.load(std::memory_order_relaxed);
	}

	//! Losses merged into an open gap record because the gap table was full (their silence is
	//! padded, see SampleRecordGaps). Valid after finish().
	std::uint64_t gapsMerged() const noexcept
	{
		return m_gaps->merged();
	}

	//! Frames accepted from the audio thread so far, including the ones still
	//! in flight in the ring.
	std::uint64_t framesStaged() const noexcept
	{
		return m_framesStaged.load(std::memory_order_relaxed);
	}

	//! Frames the audio thread had to drop because the ring was full.
	std::uint64_t framesDropped() const noexcept
	{
		return m_ring->overflowCount();
	}

	//! Audio-thread safe: ask the drain thread to finish the take and return
	//! at once. It neither joins nor touches the take; finish() collects it.
	void requestFinish() noexcept
	{
		m_finishing.store(true, std::memory_order_release);
	}

	//! Stop the drain thread, wait for it, and return the finished take.
	//! All take-sized work (vector growth, SampleBuffer construction) has
	//! already happened on the drain thread, so this call allocates nothing.
	//! Returns nullptr when no frames were recorded.
	//! Joins the drain thread (a few ms at most): call it from the main thread,
	//! never from the audio thread.
	std::shared_ptr<const SampleBuffer> finish() noexcept
	{
		finishAndJoin();
		return m_result;
	}

private:
	void finishAndJoin() noexcept
	{
		if (!m_drainThread.joinable()) { return; }
		m_finishing.store(true, std::memory_order_release);
		m_drainThread.join();
		m_result = m_take->buffer;
	}

	//! Pad every gap that is due into the take. A gap at staged position P sits after the first
	//! P real frames, i.e. at take index P + (silence already inserted before it).
	//! Drain thread only.
	void padDueGaps(bool finishing)
	{
		auto& take = m_take->frames;
		while (const auto gap = m_gaps->takeDue(m_framesRead, finishing))
		{
			const auto index = static_cast<std::ptrdiff_t>(gap->position + m_framesPaddedLocal);
			take.insert(take.begin() + index, static_cast<std::size_t>(gap->length), SampleFrame{});
			m_framesPaddedLocal += gap->length;
			m_framesPadded.store(m_framesPaddedLocal, std::memory_order_relaxed);
		}
	}

	//! Read everything the ring holds, then pad the gaps that are due. Returns the staged
	//! frames read (padding is not counted). Drain thread only.
	std::size_t drainOnce(bool finishing)
	{
		auto& take = m_take->frames;
		std::size_t staged = 0;
		for (;;)
		{
			const auto frames = m_ring->read(m_scratch.data(), m_scratch.size());
			if (frames == 0) { break; }
			take.insert(take.end(), m_scratch.begin(), m_scratch.begin() + static_cast<std::ptrdiff_t>(frames));
			m_framesRead += frames;
			staged += frames;
		}
		padDueGaps(finishing);
		return staged;
	}

	void drainLoop()
	{
		while (!m_finishing.load(std::memory_order_acquire))
		{
			if (drainOnce(false) == 0)
			{
				// Idle: a standby take wakes 50 times a second, not 500. The ring holds ~0.34 s of input.
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
		// Take the tail that was pushed before the finish flag became visible, and the
		// newest gap, which can no longer grow.
		while (drainOnce(true) > 0) { }

		auto& take = m_take->frames;
		if (!take.empty())
		{
			m_take->buffer = std::make_shared<const SampleBuffer>(
				std::move(take), static_cast<int>(m_sampleRate));
		}
	}

	const sample_rate_t m_sampleRate;
	const std::unique_ptr<SampleFrameRingBuffer> m_ring;
	std::vector<SampleFrame> m_scratch;

	const std::shared_ptr<SampleRecordTake> m_take;

	std::atomic<bool> m_finishing{false};
	std::atomic<std::uint64_t> m_framesStaged{0};
	std::shared_ptr<const SampleBuffer> m_result;

	const std::unique_ptr<SampleRecordGaps> m_gaps;
	std::atomic<std::uint64_t> m_framesPadded{0};
	std::uint64_t m_framesPaddedLocal = 0;  //!< drain thread only: silence inserted so far
	std::uint64_t m_framesRead = 0;         //!< drain thread only: staged frames read

	std::thread m_drainThread;
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_RECORD_ACCUMULATOR_H
