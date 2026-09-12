/*
 * SampleRecordAccumulator.h - background take builder for the clip record path
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

#ifndef LMMS_SAMPLE_RECORD_ACCUMULATOR_H
#define LMMS_SAMPLE_RECORD_ACCUMULATOR_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
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
 *   dropped and counted in framesDropped();
 * - a dedicated drain thread owns the growing take and the `SampleBuffer`
 *   construction, so every large allocation happens off the audio thread.
 *
 * The result is handed back by finish(), which performs no allocation on the
 * calling thread. SampleBuffer's sample rate is captured at construction time
 * (the caller must build the accumulator while the engine's input rate is the
 * rate the take is recorded at).
 */
class SampleRecordAccumulator
{
public:
	//! Ring capacity: ~0.34 s at 48 kHz, drained every 2 ms by the drain
	//! thread, so it only overflows if the drain thread is starved for that
	//! long. Two channels * 4 bytes * 16384 = 128 KiB, allocated once.
	static constexpr std::size_t DefaultRingFrames = 16384;
	//! Drain scratch size, owned by the drain thread.
	static constexpr std::size_t DrainBatchFrames = 4096;

	explicit SampleRecordAccumulator(sample_rate_t sampleRate,
			std::size_t ringFrames = DefaultRingFrames) :
		m_sampleRate(sampleRate),
		m_ring(std::make_unique<SampleFrameRingBuffer>(ringFrames)),
		m_scratch(DrainBatchFrames),
		m_take(std::make_shared<SampleRecordTake>())
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
	//! Realtime-safe by contract - see the class comment.
	void append(const SampleFrame* frames, f_cnt_t count) noexcept
	{
		const auto written = m_ring->writeBlock(frames, static_cast<std::size_t>(count));
		if (written > 0)
		{
			m_framesStaged.fetch_add(written, std::memory_order_relaxed);
		}
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

	//! Stop the drain thread, wait for it, and return the finished take.
	//! All take-sized work (vector growth, SampleBuffer construction) has
	//! already happened on the drain thread, so this call allocates nothing on
	//! the calling thread even when that thread is the audio thread.
	//! Returns nullptr when no frames were recorded.
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

	//! Drain whatever the ring holds right now. Returns the frames taken.
	//! Drain thread only.
	std::size_t drainOnce()
	{
		const auto frames = m_ring->read(m_scratch.data(), m_scratch.size());
		if (frames > 0)
		{
			auto& take = m_take->frames;
			take.insert(take.end(), m_scratch.begin(),
				m_scratch.begin() + static_cast<std::ptrdiff_t>(frames));
		}
		return frames;
	}

	void drainLoop()
	{
		while (!m_finishing.load(std::memory_order_acquire))
		{
			if (drainOnce() == 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
		}
		// Take the tail that was pushed before the finish flag became visible.
		while (drainOnce() > 0) { }

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

	std::thread m_drainThread;
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_RECORD_ACCUMULATOR_H
