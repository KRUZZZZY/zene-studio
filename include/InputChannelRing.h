/*
 * InputChannelRing.h - lock-free SPSC ring buffer of INTERLEAVED input frames
 *                      whose channel count is part of the ring (0.3.0, feature
 *                      row 64 "Arbitrary input count / multiple simultaneous
 *                      inputs").
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

#ifndef LMMS_INPUT_CHANNEL_RING_H
#define LMMS_INPUT_CHANNEL_RING_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lmms
{


//! Lock-free SPSC ring buffer of interleaved input frames, N channels wide.
/*!
 * WHY THIS EXISTS. The engine's capture staging ring
 * (include/SampleFrameRingBuffer.h) stores SampleFrame - which is exactly two
 * floats (include/SampleFrame.h's m_samples is std::array<sample_t, 2>), so the
 * whole engine input path is stereo by construction. Feature row 64 asks for an
 * ARBITRARY input count: an interface with eight inputs must be able to feed
 * eight record routes. The width therefore has to travel with the samples, and
 * it cannot travel in a SampleFrame; this ring is where it does.
 *
 * THE PROPERTIES ARE THE PROGRAMME'S PRECEDENT (include/SampleFrameRingBuffer.h,
 * include/RecordRingBuffer.h), not a new contract:
 *  - the storage is allocated exactly once, in the constructor, which must run
 *    off the audio thread and off the capture thread;
 *  - the producer side (writeBlock) performs no allocation, no locking and no
 *    syscalls - it copies samples and updates one producer-owned atomic index;
 *  - the consumer side (read) owns the other atomic index;
 *  - both indices are monotonically increasing 64-bit FRAME counters masked to
 *    the power-of-two capacity, so wraparound is unsigned arithmetic;
 *  - overflow policy is drop-newest: frames that do not fit are discarded and
 *    counted in overflowCount(); unread data is never overwritten and the
 *    producer never blocks.
 *
 * The one difference from SampleFrameRingBuffer is that the element is
 * `channels()` floats rather than one SampleFrame, and the channel count is
 * fixed for the ring's whole life. A backend that changes its channel count
 * must build a new ring (which is what reopening the device does).
 */
class InputChannelRing
{
public:
	//! Allocates storage for at least \a minCapacityFrames frames of
	//! \a channels channels each. Must run off the audio thread.
	InputChannelRing(std::size_t minCapacityFrames, int channels) :
		m_capacity(nextPowerOfTwo(minCapacityFrames == 0 ? 1 : minCapacityFrames)),
		m_mask(m_capacity - 1),
		m_channels(channels < 1 ? 1 : channels),
		m_data(m_capacity * static_cast<std::size_t>(m_channels < 1 ? 1 : channels))
	{
	}

	InputChannelRing(const InputChannelRing&) = delete;
	InputChannelRing& operator=(const InputChannelRing&) = delete;

	//! Producer: append \a frames interleaved frames. Returns the number of
	//! frames actually written; the rest are dropped and counted. The ring's
	//! own channel count wins: a block with a different width is REFUSED
	//! whole (0 returned), because re-interpreting it would silently rotate
	//! the channels. Realtime-safe.
	std::size_t writeBlock(const float* interleaved, int channels, std::size_t frames) noexcept
	{
		if (interleaved == nullptr || frames == 0) { return 0; }
		if (channels != m_channels) { return 0; }

		const auto writePos = m_writePos.load(std::memory_order_relaxed);
		const auto readPos = m_readPos.load(std::memory_order_acquire);
		const auto freeFrames = m_capacity - static_cast<std::size_t>(writePos - readPos);
		const auto toWrite = std::min(frames, freeFrames);
		const auto width = static_cast<std::size_t>(m_channels);
		for (std::size_t frame = 0; frame < toWrite; ++frame)
		{
			const auto dstBase = ((writePos + frame) & m_mask) * width;
			const auto srcBase = frame * width;
			for (std::size_t channel = 0; channel < width; ++channel)
			{
				m_data[dstBase + channel] = interleaved[srcBase + channel];
			}
		}
		m_writePos.store(writePos + toWrite, std::memory_order_release);
		if (toWrite < frames)
		{
			m_overflow.fetch_add(frames - toWrite, std::memory_order_relaxed);
		}
		return toWrite;
	}

	//! Consumer: read up to \a frames frames into \a dst, which must hold at
	//! least frames * channels() floats. Returns the number of frames read.
	//! Realtime-safe (it runs on the render thread, where the same rule holds).
	std::size_t read(float* dst, std::size_t frames) noexcept
	{
		if (dst == nullptr) { return 0; }
		const auto readPos = m_readPos.load(std::memory_order_relaxed);
		const auto writePos = m_writePos.load(std::memory_order_acquire);
		const auto available = writePos - readPos;
		const auto toRead = std::min(frames, static_cast<std::size_t>(available));
		const auto width = static_cast<std::size_t>(m_channels);
		for (std::size_t frame = 0; frame < toRead; ++frame)
		{
			const auto srcBase = ((readPos + frame) & m_mask) * width;
			const auto dstBase = frame * width;
			for (std::size_t channel = 0; channel < width; ++channel)
			{
				dst[dstBase + channel] = m_data[srcBase + channel];
			}
		}
		m_readPos.store(readPos + toRead, std::memory_order_release);
		return toRead;
	}

	//! Frames currently available for the consumer.
	std::size_t available() const noexcept
	{
		return static_cast<std::size_t>(m_writePos.load(std::memory_order_acquire)
			- m_readPos.load(std::memory_order_acquire));
	}

	int channels() const noexcept { return m_channels; }
	std::size_t capacity() const noexcept { return m_capacity; }

	//! Total number of frames dropped by the producer so far.
	std::uint64_t overflowCount() const noexcept
	{
		return m_overflow.load(std::memory_order_relaxed);
	}

	//! Discard all buffered frames and clear the overflow counter. Only valid
	//! while neither producer nor consumer is running.
	void reset() noexcept
	{
		const auto writePos = m_writePos.load(std::memory_order_acquire);
		m_readPos.store(writePos, std::memory_order_release);
		m_overflow.store(0, std::memory_order_relaxed);
	}

private:
	static std::size_t nextPowerOfTwo(std::size_t value)
	{
		std::size_t result = 1;
		while (result < value) { result <<= 1; }
		return result;
	}

	const std::size_t m_capacity;
	const std::size_t m_mask;
	const int m_channels;

	std::vector<float> m_data;

	// One cache line each: the producer and the consumer only ever write
	// their own index.
	alignas(64) std::atomic<std::uint64_t> m_writePos{0};
	alignas(64) std::atomic<std::uint64_t> m_readPos{0};
	alignas(64) std::atomic<std::uint64_t> m_overflow{0};
} ;


} // namespace lmms

#endif // LMMS_INPUT_CHANNEL_RING_H
