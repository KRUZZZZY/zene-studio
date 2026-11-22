/*
 * RenderDiagnostics.cpp - each instrument track's synthesis load and the engine's late periods
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

#include "RenderDiagnostics.h"

#include <cmath>

#include "DspLoadProbe.h"

namespace lmms
{

namespace
{

//! What @a periods periods without a sample leave of a smoothed figure.
float decayOver(std::uint64_t periods) noexcept
{
	return static_cast<float>(std::pow(1.0 - DspLoadProbe::Smoothing, static_cast<double>(periods)));
}

} // namespace

void SynthesisLoad::fold(std::uint64_t period, double periodSeconds) noexcept
{
	const std::uint64_t last = m_foldedPeriod.load(std::memory_order_relaxed);
	if (period == last || periodSeconds <= 0.0) { return; }
	const std::uint64_t nanos = m_nanos.exchange(0, std::memory_order_relaxed);
	float previous = m_percent.load(std::memory_order_relaxed);
	if (last != 0 && period > last + 1) { previous *= decayOver(period - last - 1); }  // the periods it was silent
	const auto sample = static_cast<float>(100.0 * static_cast<double>(nanos) * 1e-9 / periodSeconds);
	m_percent.store(previous + (sample - previous) * DspLoadProbe::Smoothing, std::memory_order_relaxed);
	m_foldedPeriod.store(period, std::memory_order_release);
}

float SynthesisLoad::percent(std::uint64_t currentPeriod) const noexcept
{
	const std::uint64_t last = m_foldedPeriod.load(std::memory_order_acquire);
	const float value = m_percent.load(std::memory_order_relaxed);
	if (last == 0) { return 0.0f; }
	// The period after the last fold may still be rendering; only the ones past it went by without a note.
	return currentPeriod > last + 1 ? value * decayOver(currentPeriod - last - 1) : value;
}

void LatePeriodLog::record(tick_t tick, bool playing, std::uint32_t tookMicros, std::uint32_t periodMicros) noexcept
{
	const std::uint64_t index = m_count.load(std::memory_order_relaxed);
	Slot& slot = m_slots[index % Capacity];
	slot.sequence.store(2 * index + 1, std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_release);
	slot.tick.store(tick, std::memory_order_relaxed);
	slot.playing.store(playing, std::memory_order_relaxed);
	slot.tookMicros.store(tookMicros, std::memory_order_relaxed);
	slot.periodMicros.store(periodMicros, std::memory_order_relaxed);
	slot.sequence.store(2 * index + 2, std::memory_order_release);
	m_count.store(index + 1, std::memory_order_release);
}

std::vector<LatePeriodLog::Mark> LatePeriodLog::recent() const
{
	const std::uint64_t count = m_count.load(std::memory_order_acquire);
	const std::uint64_t first = count > Capacity ? count - Capacity : 0;
	std::vector<Mark> marks;
	marks.reserve(static_cast<std::size_t>(count - first));
	for (std::uint64_t index = first; index < count; ++index)
	{
		const Slot& slot = m_slots[index % Capacity];
		const std::uint64_t before = slot.sequence.load(std::memory_order_acquire);
		if (before != 2 * index + 2) { continue; }  // overwritten by a newer mark meanwhile
		Mark mark;
		mark.serial = index + 1;
		mark.tick = slot.tick.load(std::memory_order_relaxed);
		mark.playing = slot.playing.load(std::memory_order_relaxed);
		mark.tookMicros = slot.tookMicros.load(std::memory_order_relaxed);
		mark.periodMicros = slot.periodMicros.load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		if (slot.sequence.load(std::memory_order_relaxed) != before) { continue; }  // torn: a newer mark took the slot
		marks.push_back(mark);
	}
	return marks;
}

} // namespace lmms
