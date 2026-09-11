/*
 * LatencyCompensation.cpp - fixed-capacity delay line for plugin delay
 *                           compensation at a mixer summing point
 *
 * Copyright (c) 2026 Zene Studio developers
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

#include "LatencyCompensation.h"

#include <algorithm>
#include <cstddef>

namespace lmms
{

void LatencyCompensation::init(f_cnt_t framesPerPeriod)
{
	const f_cnt_t fpp = std::max<f_cnt_t>(framesPerPeriod, 1);
	m_ring.assign(static_cast<std::size_t>(MaxFrames) + fpp, SampleFrame{});
	m_scratch.assign(static_cast<std::size_t>(fpp), SampleFrame{});
	m_write = 0;
	m_delay.store(0, std::memory_order_relaxed);
}


void LatencyCompensation::setDelayFrames(int frames)
{
	if (frames < 0)
	{
		frames = 0;
	}
	else if (frames > MaxFrames)
	{
		frames = MaxFrames;
	}
	m_delay.store(frames, std::memory_order_relaxed);
}


int LatencyCompensation::delayFrames() const
{
	return m_delay.load(std::memory_order_relaxed);
}


int LatencyCompensation::effectiveDelay(f_cnt_t frames) const
{
	if (m_ring.empty() || frames > m_scratch.size())
	{
		// Not initialised for this block size: compensate nothing rather than
		// read out of bounds. The mixer clamps its published delays to
		// MaxFrames, so this only happens after a block-size change.
		return 0;
	}
	const int room = static_cast<int>(m_ring.size() - frames);
	int delay = delayFrames();
	if (delay > room)
	{
		delay = room;
	}
	return std::max(delay, 0);
}


const SampleFrame* LatencyCompensation::readWrapped(f_cnt_t read, f_cnt_t frames)
{
	const f_cnt_t capacity = m_ring.size();
	const f_cnt_t readFirst = std::min(frames, capacity - read);
	std::copy(m_ring.data() + read, m_ring.data() + read + readFirst, m_scratch.data());
	if (frames > readFirst)
	{
		std::copy(m_ring.data(), m_ring.data() + (frames - readFirst),
			m_scratch.data() + readFirst);
	}
	return m_scratch.data();
}


const SampleFrame* LatencyCompensation::process(const SampleFrame* in, f_cnt_t frames)
{
	if (in == nullptr || frames == 0)
	{
		return in;
	}
	const f_cnt_t capacity = m_ring.size();
	if (capacity == 0 || frames > capacity || frames > m_scratch.size())
	{
		return in;
	}

	const int delay = effectiveDelay(frames);

	// The delayed block starts `delay` frames before the *start* of the
	// incoming block, so the read origin is fixed by the pre-write cursor.
	// Write the incoming block first, then read: for a delay shorter than one
	// block the read range overlaps the written range and only the freshly
	// written samples hold the sub-block part of the shift.
	const f_cnt_t blockStart = m_write;
	const f_cnt_t read =
		(blockStart + capacity - static_cast<f_cnt_t>(std::max(delay, 0))) % capacity;

	const f_cnt_t first = std::min(frames, capacity - m_write);
	std::copy(in, in + first, m_ring.data() + m_write);
	if (frames > first)
	{
		std::copy(in + first, in + frames, m_ring.data());
	}
	m_write = (m_write + frames) % capacity;

	if (delay <= 0)
	{
		return in;
	}

	return readWrapped(read, frames);
}


void LatencyCompensation::processInPlace(SampleFrame* buf, f_cnt_t frames)
{
	const SampleFrame* out = process(buf, frames);
	if (out != buf)
	{
		std::copy(out, out + frames, buf);
	}
}


void LatencyCompensation::processPlanar(float* left, float* right, f_cnt_t frames)
{
	if (left == nullptr || right == nullptr || frames == 0)
	{
		return;
	}
	const f_cnt_t capacity = m_ring.size();
	if (capacity == 0 || frames > capacity || frames > m_scratch.size())
	{
		return;
	}

	const int delay = effectiveDelay(frames);

	// Write first, then read from the block-start-relative origin; see
	// process() for why both details matter.
	const f_cnt_t blockStart = m_write;
	const f_cnt_t read =
		(blockStart + capacity - static_cast<f_cnt_t>(std::max(delay, 0))) % capacity;

	for (f_cnt_t i = 0; i < frames; ++i)
	{
		m_ring[(m_write + i) % capacity] = SampleFrame{left[i], right[i]};
	}
	m_write = (m_write + frames) % capacity;

	if (delay <= 0)
	{
		return;
	}

	const SampleFrame* out = readWrapped(read, frames);
	for (f_cnt_t i = 0; i < frames; ++i)
	{
		left[i] = out[i].left();
		right[i] = out[i].right();
	}
}


void LatencyCompensation::advanceSilence(f_cnt_t frames)
{
	const f_cnt_t capacity = m_ring.size();
	if (capacity == 0 || frames > capacity)
	{
		return;
	}
	const SampleFrame silence{};
	const f_cnt_t first = std::min(frames, capacity - m_write);
	std::fill(m_ring.data() + m_write, m_ring.data() + m_write + first, silence);
	if (frames > first)
	{
		std::fill(m_ring.data(), m_ring.data() + (frames - first), silence);
	}
	m_write = (m_write + frames) % capacity;
}

} // namespace lmms
