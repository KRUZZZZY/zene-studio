/*
 * BufferManager.h - A buffer caching/memory management system
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_BUFFER_MANAGER_H
#define LMMS_BUFFER_MANAGER_H

#include <cstddef>
#include <span>
#include <vector>

#include "RealtimeFreeList.h"
#include "SampleFrame.h"
#include "lmms_export.h"
#include "LmmsTypes.h"

namespace lmms
{

class SampleFrame;

/*! Period-sized frame blocks.
 *
 *  acquire() is the audio thread's: it takes a block from a preallocated pool and never allocates. The pool grows
 *  only on the control thread, through reserve() (called at init and from the engine's capacity top-up). When the
 *  pool has no block, acquire() returns an EMPTY lease - exhaustion is reported, never papered over with a heap
 *  block. acquireBlocking() and leaseBlocking() allocate; they are for control-thread and offline callers.
 *
 *  Blocks are sized to max(DEFAULT_BUFFER_SIZE, period) at reserve time; acquire() resizes a block to the current
 *  period, so a period change up to the engine's cap (DEFAULT_BUFFER_SIZE) needs no re-reserve.
 */
class LMMS_EXPORT BufferManager
{
public:
	struct Block;

	//! RAII lease on one block. A pooled block returns to the pool when the lease ends. Empty when exhausted.
	class Lease
	{
	public:
		Lease() noexcept = default;
		explicit Lease(Block* block) noexcept : m_block(block) {}
		Lease(Lease&& other) noexcept : m_block(other.m_block) { other.m_block = nullptr; }
		Lease& operator=(Lease&& other) noexcept
		{
			if (this != &other)
			{
				reset();
				m_block = other.m_block;
				other.m_block = nullptr;
			}
			return *this;
		}
		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;
		~Lease() { reset(); }

		explicit operator bool() const noexcept { return m_block != nullptr; }
		//! The frames of the current period, or no frames when the lease is empty.
		std::span<SampleFrame> frames() const noexcept;
		//! Ends the lease now: a pooled block goes back to the pool, an owned block is freed.
		void reset() noexcept;

	private:
		Block* m_block = nullptr;
	};

	static void init( f_cnt_t fpp );
	//! Control thread only. Grows the pool until it holds at least @a blocks free blocks.
	static void reserve( std::size_t blocks );
	//! Audio thread. An empty lease when the pool is exhausted; never allocates.
	static Lease acquire() noexcept;
	//! Control thread (allocates). A lease that owns its block and frees it on reset.
	static Lease leaseBlocking();
	//! Control thread (allocates). The plain vector of one period, as before the pool.
	static std::vector<SampleFrame> acquireBlocking();

private:
	static f_cnt_t s_framesPerPeriod;
};


} // namespace lmms

#endif // LMMS_BUFFER_MANAGER_H
