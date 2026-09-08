/*
 * WasmSpscRingBuffer.h - lock-free single-producer/single-consumer ring buffer
 *
 * Copyright (c) 2026 LMMS WASM DSP sandbox contributors
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef LMMS_WASM_SPSC_RING_BUFFER_H
#define LMMS_WASM_SPSC_RING_BUFFER_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace lmms::wasm
{

//! Fixed-capacity lock-free SPSC queue.
//!
//! The producer (audio thread) only ever touches m_write and m_data[]; the
//! consumer (worker thread) only ever touches m_read and m_data[]. Indices are
//! published with release stores and observed with acquire loads, which is
//! enough for a single-producer/single-consumer hand-off. No allocation, no
//! locks and no syscalls on either side: push/pop are a handful of atomic
//! loads/stores.
//!
//! The capacity is one less than the storage size so that a full queue is
//! distinguishable from an empty one.
template <typename T, std::size_t Capacity>
class SpscRingBuffer
{
public:
	static_assert(Capacity >= 2, "ring buffer needs at least two slots");

	//! Producer side. Returns false when the queue is full.
	bool push(const T& value) noexcept
	{
		const std::size_t write = m_write.load(std::memory_order_relaxed);
		const std::size_t next = (write + 1) % storageSize;
		if (next == m_read.load(std::memory_order_acquire))
		{
			return false;
		}
		m_data[write] = value;
		m_write.store(next, std::memory_order_release);
		return true;
	}

	//! Consumer side. Returns false when the queue is empty.
	bool pop(T& value) noexcept
	{
		const std::size_t read = m_read.load(std::memory_order_relaxed);
		if (read == m_write.load(std::memory_order_acquire))
		{
			return false;
		}
		value = m_data[read];
		m_read.store((read + 1) % storageSize, std::memory_order_release);
		return true;
	}

	bool empty() const noexcept
	{
		return m_read.load(std::memory_order_acquire) == m_write.load(std::memory_order_acquire);
	}

	std::size_t size() const noexcept
	{
		const std::size_t write = m_write.load(std::memory_order_acquire);
		const std::size_t read = m_read.load(std::memory_order_acquire);
		return (write + storageSize - read) % storageSize;
	}

	static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
	static constexpr std::size_t storageSize = Capacity + 1;

	std::array<T, storageSize> m_data{};
	std::atomic<std::size_t> m_read{0};
	std::atomic<std::size_t> m_write{0};
};

} // namespace lmms::wasm

#endif // LMMS_WASM_SPSC_RING_BUFFER_H
