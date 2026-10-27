/*
 * HardwareOutputBus.cpp - the device's output channels beyond the master: what each hardware output carries
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

#include "HardwareOutputBus.h"

#include <algorithm>

namespace lmms
{

void HardwareOutputBus::init(f_cnt_t frames)
{
	m_frames = std::max<f_cnt_t>(frames, 1);
	for (Side& side : m_sides)
	{
		side.data.assign(static_cast<std::size_t>(MaxChannels) * m_frames, 0.f);
		side.used = 0;
		side.master = Placement{};
	}
	m_write = 0;
}

void HardwareOutputBus::swap() noexcept
{
	m_write = 1 - m_write;
	Side& side = m_sides[m_write];
	std::fill_n(side.data.begin(), static_cast<std::size_t>(side.used) * m_frames, 0.f);
	side.used = 0;
	side.master = Placement{};
}

void HardwareOutputBus::add(int start, int width, const SampleFrame* src, f_cnt_t frames, float gain) noexcept
{
	Side& side = m_sides[m_write];
	if (src == nullptr || start < 0 || side.data.empty()) { return; }
	const f_cnt_t count = std::min(frames, m_frames);
	const int sides = width == 1 ? 1 : 2;
	for (int s = 0; s < sides; ++s)
	{
		const int channel = start + s;
		if (channel >= MaxChannels) { return; }
		float* plane = side.data.data() + static_cast<std::size_t>(channel) * m_frames;
		for (f_cnt_t f = 0; f < count; ++f)
		{
			plane[f] += gain * (sides == 1 ? 0.5f * (src[f][0] + src[f][1]) : src[f][s]);
		}
		side.used = std::max(side.used, channel + 1);
	}
}

void HardwareOutputBus::addPlane(int channel, const float* src, f_cnt_t frames, float gain, const float* gains) noexcept
{
	Side& side = m_sides[m_write];
	if (src == nullptr || channel < 0 || channel >= MaxChannels || side.data.empty()) { return; }
	const f_cnt_t count = std::min(frames, m_frames);
	float* plane = side.data.data() + static_cast<std::size_t>(channel) * m_frames;
	for (f_cnt_t f = 0; f < count; ++f) { plane[f] += src[f] * (gains != nullptr ? gains[f] : gain); }
	side.used = std::max(side.used, channel + 1);
}

bool HardwareOutputBus::active() const noexcept
{
	const Side& side = readSide();
	return side.used > 0 || !(side.master == Placement{});
}

float HardwareOutputBus::sample(int channel, f_cnt_t frame, const SampleFrame& master) const noexcept
{
	// A frame outside the period is no audio: silence, not the next channel's plane or past the storage.
	if (frame < 0 || frame >= m_frames) { return 0.f; }
	const Side& side = readSide();
	float value = channel >= 0 && channel < side.used ? side.data[static_cast<std::size_t>(channel) * m_frames + frame] : 0.f;
	const Placement& at = side.master;
	if (at.start < 0 || channel < at.start || channel >= at.start + at.width) { return value; }
	return value + (at.width == 1 ? 0.5f * (master[0] + master[1]) : master[channel - at.start]);
}

float HardwareOutputBus::monoSample(f_cnt_t frame, const SampleFrame& master) const noexcept
{
	if (frame < 0 || frame >= m_frames) { return 0.f; }
	const Side& side = readSide();
	const float value = side.used > 0 ? side.data[static_cast<std::size_t>(frame)] : 0.f;
	return value + (side.master.start == 0 ? master.average() : 0.f);
}

int HardwareOutputBus::channelsInUse() const noexcept
{
	const Side& side = readSide();
	const int master = side.master.start < 0 ? 0 : side.master.start + side.master.width;
	return std::max(side.used, master);
}

} // namespace lmms
