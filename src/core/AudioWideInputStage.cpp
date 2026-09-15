/*
 * AudioWideInputStage.cpp - the N-channel capture staging stage (0.3.0,
 *                           feature row 64).
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

#include "AudioWideInputStage.h"

namespace lmms
{

AudioWideInputStage::AudioWideInputStage(int maxChannels, f_cnt_t capacityFrames) :
	m_maxChannels(maxChannels < 1 ? 1 : maxChannels),
	m_capacityFrames(capacityFrames < 1 ? 1 : capacityFrames),
	m_stage(std::make_unique<InputChannelRing>(
		static_cast<std::size_t>(capacityFrames < 1 ? 1 : capacityFrames),
		maxChannels < 1 ? 1 : maxChannels)),
	m_buffers(static_cast<std::size_t>(kBufferCount)),
	m_frames{0, 0},
	m_channels{0, 0}
{
	// Allocated exactly once, here, off every realtime thread. Each buffer
	// holds one period's worth of the widest block push() accepts, so a drain
	// can never overrun it; both are zeroed so an empty stage reads as silence
	// rather than as whatever the allocator returned.
	const auto samples = static_cast<std::size_t>(m_maxChannels)
		* static_cast<std::size_t>(m_capacityFrames);
	for (auto& buffer : m_buffers)
	{
		buffer.assign(samples, 0.f);
	}
}

AudioWideInputStage::~AudioWideInputStage() = default;


std::size_t AudioWideInputStage::push(const float* interleaved, int channels, f_cnt_t frames) noexcept
{
	if (interleaved == nullptr || frames == 0 || m_stage == nullptr)
	{
		return 0;
	}
	if (channels < 1 || channels > m_maxChannels || channels != m_stage->channels())
	{
		// Refused whole, and counted: a reader that saw part of a block at one
		// width and part at another would attribute a channel to the wrong
		// route, which is worse than dropping the block.
		m_refused.fetch_add(1, std::memory_order_relaxed);
		m_dropped.fetch_add(static_cast<std::uint64_t>(frames), std::memory_order_relaxed);
		return 0;
	}

	const auto staged = m_stage->writeBlock(interleaved, channels, static_cast<std::size_t>(frames));
	if (staged < static_cast<std::size_t>(frames))
	{
		m_dropped.fetch_add(static_cast<std::uint64_t>(frames - static_cast<f_cnt_t>(staged)),
			std::memory_order_relaxed);
	}
	return staged;
}


void AudioWideInputStage::drain() noexcept
{
	// The other half of the double buffer: whatever the consumer was reading is
	// now the write side, so a drain never writes under a live reader.
	m_write = m_read;
	m_read = 1 - m_read;
	m_frames[m_read] = 0;
	m_channels[m_read] = 0;

	if (m_stage == nullptr)
	{
		return;
	}

	const auto staged = m_stage->available();
	const auto frames = std::min(staged, static_cast<std::size_t>(m_capacityFrames));
	if (frames == 0)
	{
		return;
	}

	const auto read = m_stage->read(m_buffers[static_cast<std::size_t>(m_read)].data(), frames);
	m_frames[m_read] = static_cast<f_cnt_t>(read);
	m_channels[m_read] = read > 0 ? m_stage->channels() : 0;
}


} // namespace lmms
