/*
 * RecordRingBuffer.h - lock-free single-producer/single-consumer ring buffer
 *                      used by the two-track recording prototype
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

#ifndef LMMS_RECORD_RING_BUFFER_H
#define LMMS_RECORD_RING_BUFFER_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "LmmsTypes.h"

namespace lmms
{


//! Lock-free SPSC ring buffer of mono sample frames (prototype, task #556).
/*!
 * The storage is allocated exactly once, in the constructor, which must run
 * off the audio thread. From then on:
 *
 * - the producer side (write()/writeBlock()/writeStrided()) performs **no
 *   allocation, no locking and no syscalls** - it stores sample data and
 *   updates one producer-owned atomic index;
 * - the consumer side (read()) owns the other atomic index;
 * - both indices are monotonically increasing 64-bit frame counters, masked
 *   to the power-of-two capacity, so wraparound is handled by unsigned
 *   arithmetic.
 *
 * Overflow policy: drop-newest. Frames that do not fit are discarded and
 * counted in overflowCount(); unread data is never overwritten and the
 * producer never blocks.
 */
class RecordRingBuffer
{
public:
	//! Allocates storage for at least \a minCapacityFrames frames.
	explicit RecordRingBuffer(std::size_t minCapacityFrames) :
		m_capacity(nextPowerOfTwo(minCapacityFrames)),
		m_mask(m_capacity - 1),
		m_data(m_capacity, 0.f)
	{
	}

	RecordRingBuffer(const RecordRingBuffer&) = delete;
	RecordRingBuffer& operator=(const RecordRingBuffer&) = delete;

	//! Producer: append one frame. Returns false (and counts an overflow)
	//! when the buffer is full. Realtime-safe.
	bool write(sample_t frame) noexcept
	{
		const auto writePos = m_writePos.load(std::memory_order_relaxed);
		const auto readPos = m_readPos.load(std::memory_order_acquire);
		if (writePos - readPos >= m_capacity)
		{
			m_overflow.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		m_data[writePos & m_mask] = frame;
		m_writePos.store(writePos + 1, std::memory_order_release);
		return true;
	}

	//! Producer: append \a frames frames from \a src. Returns the number of
	//! frames actually written; the remainder is dropped and counted.
	//! Realtime-safe.
	std::size_t writeBlock(const sample_t* src, std::size_t frames) noexcept
	{
		return writeStrided(src, 1, frames);
	}

	//! Producer: append \a frames frames from \a src with the given frame
	//! stride (used to demux one channel of an interleaved buffer).
	//! Realtime-safe: no allocation, no locks, no syscalls.
	std::size_t writeStrided(const sample_t* src, std::size_t stride, std::size_t frames) noexcept
	{
		const auto writePos = m_writePos.load(std::memory_order_relaxed);
		const auto readPos = m_readPos.load(std::memory_order_acquire);
		const auto freeFrames = m_capacity - (writePos - readPos);
		const auto toWrite = std::min(frames, freeFrames);
		for (std::size_t i = 0; i < toWrite; ++i)
		{
			m_data[(writePos + i) & m_mask] = src[i * stride];
		}
		m_writePos.store(writePos + toWrite, std::memory_order_release);
		if (toWrite < frames)
		{
			m_overflow.fetch_add(frames - toWrite, std::memory_order_relaxed);
		}
		return toWrite;
	}

	//! Consumer: read up to \a frames frames into \a dst. Returns the number
	//! of frames read (0 when empty).
	std::size_t read(sample_t* dst, std::size_t frames) noexcept
	{
		const auto readPos = m_readPos.load(std::memory_order_relaxed);
		const auto writePos = m_writePos.load(std::memory_order_acquire);
		const auto available = writePos - readPos;
		const auto toRead = std::min(frames, static_cast<std::size_t>(available));
		for (std::size_t i = 0; i < toRead; ++i)
		{
			dst[i] = m_data[(readPos + i) & m_mask];
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

	std::vector<sample_t> m_data;

	// One cache line each: the producer and the consumer only ever write
	// their own index.
	alignas(64) std::atomic<std::uint64_t> m_writePos{0};
	alignas(64) std::atomic<std::uint64_t> m_readPos{0};
	alignas(64) std::atomic<std::uint64_t> m_overflow{0};
} ;


} // namespace lmms

#endif // LMMS_RECORD_RING_BUFFER_H
