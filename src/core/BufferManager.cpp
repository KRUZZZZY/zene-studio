/*
 * BufferManager.cpp - A buffer caching/memory management system
 *
 * Copyright (c) 2017 Lukas W <lukaswhl/at/gmail.com>
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

#include "BufferManager.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "AudioEngine.h"  // DEFAULT_BUFFER_SIZE

namespace lmms
{

struct BufferManager::Block : RealtimeFreeNode<BufferManager::Block>
{
	std::vector<SampleFrame> samples;
	bool pooled = false;
};

namespace
{

constexpr std::size_t InitialBlocks = 64;

//! Free blocks. Pops happen on the audio thread only; pushes from any thread (see RealtimeFreeList).
RealtimeFreeList<BufferManager::Block>& blockPool()
{
	static RealtimeFreeList<BufferManager::Block> pool;
	return pool;
}

//! Every pooled block, owned here so the pool's nodes outlive the audio thread's use of them. Control thread only.
std::vector<std::unique_ptr<BufferManager::Block>>& blockOwners()
{
	static std::vector<std::unique_ptr<BufferManager::Block>> owners;
	return owners;
}

} // namespace

f_cnt_t BufferManager::s_framesPerPeriod;

void BufferManager::init( f_cnt_t fpp )
{
	s_framesPerPeriod = fpp;
	reserve(InitialBlocks);
}

void BufferManager::reserve( std::size_t blocks )
{
	const auto blockFrames = static_cast<std::size_t>(std::max(DEFAULT_BUFFER_SIZE, s_framesPerPeriod));
	while (blockPool().count() < blocks)
	{
		std::unique_ptr<Block> block(new Block());
		block->samples.resize(blockFrames);
		block->pooled = true;
		blockOwners().push_back(std::move(block));
		blockPool().push(blockOwners().back().get());
	}
}

BufferManager::Lease BufferManager::acquire() noexcept
{
	Block* const block = blockPool().pop();
	if (block == nullptr) { return Lease{}; }
	const auto period = static_cast<std::size_t>(s_framesPerPeriod);
	// A block too small for this period is not usable; it stays pooled for a later, smaller period.
	if (block->samples.capacity() < period)
	{
		blockPool().push(block);
		return Lease{};
	}
	// Shrinking or growing within the capacity allocates nothing.
	block->samples.resize(period);
	return Lease{block};
}

BufferManager::Lease BufferManager::leaseBlocking()
{
	auto* const block = new Block();
	block->samples.resize(static_cast<std::size_t>(s_framesPerPeriod));
	return Lease{block};
}

std::vector<SampleFrame> BufferManager::acquireBlocking()
{
	return std::vector<SampleFrame>(s_framesPerPeriod);
}

std::span<SampleFrame> BufferManager::Lease::frames() const noexcept
{
	return m_block != nullptr ? std::span<SampleFrame>{m_block->samples} : std::span<SampleFrame>{};
}

void BufferManager::Lease::reset() noexcept
{
	Block* const block = std::exchange(m_block, nullptr);
	if (block == nullptr) { return; }
	if (block->pooled) { blockPool().push(block); }
	else { delete block; }
}

} // namespace lmms
