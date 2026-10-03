/*
 * TunerTap.cpp - the newest audio frames, for the tuner
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

#include "TunerTap.h"

#include <algorithm>

#include "SampleFrame.h"

namespace lmms
{

namespace
{

//! In static storage and constant-initialised: no allocation, ever, and nothing to construct on first use.
TunerTap g_tunerTap;

constexpr std::uint64_t Mask = TunerTap::Capacity - 1;
static_assert((TunerTap::Capacity & Mask) == 0, "the ring's capacity is a power of two");

} // namespace

TunerTap& TunerTap::instance()
{
	return g_tunerTap;
}

void TunerTap::push(Source from, const SampleFrame* frames, f_cnt_t count) noexcept
{
	if (m_source.load(std::memory_order_relaxed) != static_cast<int>(from) || frames == nullptr || count == 0) { return; }
	const std::uint64_t head = m_resetPending.exchange(false, std::memory_order_acq_rel) ? 0 : m_head.load(std::memory_order_relaxed);
	const f_cnt_t first = count > Capacity ? count - Capacity : 0;
	for (f_cnt_t i = first; i < count; ++i)
	{
		m_ring[(head + i - first) & Mask].store(0.5f * (frames[i][0] + frames[i][1]), std::memory_order_relaxed);
	}
	m_head.store(head + (count - first), std::memory_order_release);
}

bool TunerTap::latest(float* out, std::size_t count) const noexcept
{
	if (out == nullptr || count == 0 || count > Capacity || m_resetPending.load(std::memory_order_acquire)) { return false; }
	const std::uint64_t head = m_head.load(std::memory_order_acquire);
	if (head < count) { return false; }
	const std::uint64_t start = head - count;
	for (std::size_t i = 0; i < count; ++i) { out[i] = m_ring[(start + i) & Mask].load(std::memory_order_relaxed); }
	// The producer may have lapped the window while it was copied: then part of it is newer than the rest.
	return m_head.load(std::memory_order_acquire) - start <= Capacity;
}

} // namespace lmms
