/*
 * PhaseScopeTap.cpp - 0.5.0 L08 (MX-137): one mixer channel's newest stereo frames, for the phase view
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

#include "PhaseScopeTap.h"

#include "SampleFrame.h"

namespace lmms
{

namespace
{

//! In static storage and constant-initialised: no allocation, ever, and nothing to construct on first use.
PhaseScopeTap g_phaseScopeTap;

constexpr std::uint64_t Mask = PhaseScopeTap::Capacity - 1;
static_assert((PhaseScopeTap::Capacity & Mask) == 0, "the ring's capacity is a power of two");
static_assert(std::atomic<float>::is_always_lock_free, "the ring's slots must be lock-free atomics");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "the ring's counters must be lock-free atomics");

} // namespace

PhaseScopeTap& PhaseScopeTap::instance()
{
	return g_phaseScopeTap;
}

void PhaseScopeTap::push(int index, const SampleFrame* frames, f_cnt_t count) noexcept
{
	// The channel and its reset generation arrive together, so a new channel cannot append to the old ring.
	const std::uint64_t state = m_state.load(std::memory_order_acquire);
	if ((state & ChannelMask) != ((static_cast<std::uint64_t>(index) + 1) & ChannelMask)
		|| frames == nullptr || count == 0) { return; }
	// One writer: another worker's push still in flight (only in the period the channel changed) keeps the ring.
	if (m_writing.exchange(true, std::memory_order_acquire)) { return; }
	const bool fresh = m_applied.load(std::memory_order_relaxed) != state;
	const std::uint64_t head = fresh ? 0 : m_head.load(std::memory_order_relaxed);
	const f_cnt_t first = count > Capacity ? count - Capacity : 0;
	// Bracket every write with an odd generation and back to even (a seqlock), so a reader refuses a copy
	// if any push changes the slots it sampled.
	m_generation.fetch_add(1, std::memory_order_relaxed);
	// Announced before the slots change (the seqlock shape: store, release fence, then the writes).
	m_reserved.store(head + (count - first), std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_release);
	for (f_cnt_t i = first; i < count; ++i)
	{
		const std::uint64_t slot = (head + i - first) & Mask;
		m_left[slot].store(frames[i][0], std::memory_order_relaxed);
		m_right[slot].store(frames[i][1], std::memory_order_relaxed);
	}
	m_head.store(head + (count - first), std::memory_order_release);
	// latest() and written() only read a generation whose reset and first write are both complete.
	if (fresh) { m_applied.store(state, std::memory_order_release); }
	m_generation.fetch_add(1, std::memory_order_release);
	m_writing.store(false, std::memory_order_release);
}

bool PhaseScopeTap::isSnapshotCurrent(std::uint64_t state, std::uint64_t generation) const noexcept
{
	return m_state.load(std::memory_order_acquire) == state
		&& m_generation.load(std::memory_order_relaxed) == generation;
}

bool PhaseScopeTap::latest(float* left, float* right, std::size_t count) const noexcept
{
	const std::uint64_t state = m_state.load(std::memory_order_acquire);
	if (left == nullptr || right == nullptr || count == 0 || count > Capacity) { return false; }
	if (m_applied.load(std::memory_order_acquire) != state) { return false; }
	// The generation brackets the copy: an odd value means a fresh push is rebasing the ring right now,
	// and an unchanged value across the copy means no push invalidated the slots read below.
	const std::uint64_t generation = m_generation.load(std::memory_order_acquire);
	if ((generation & 1) != 0) { return false; }
	const std::uint64_t head = m_head.load(std::memory_order_acquire);
	if (head < count) { return false; }
	const std::uint64_t start = head - count;
	for (std::size_t i = 0; i < count; ++i)
	{
		left[i] = m_left[(start + i) & Mask].load(std::memory_order_relaxed);
		right[i] = m_right[(start + i) & Mask].load(std::memory_order_relaxed);
	}
	// The producer may have lapped the window while it was copied: then part of it is newer than the rest.
	// Read against the push in PROGRESS, not the last published one (TunerTap::latest's rule).
	std::atomic_thread_fence(std::memory_order_acquire);
	if (!isSnapshotCurrent(state, generation)) { return false; }
	return m_reserved.load(std::memory_order_relaxed) - start <= Capacity;
}

} // namespace lmms
