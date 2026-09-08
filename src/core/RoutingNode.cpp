/*
 * RoutingNode.cpp
 *
 * Copyright (c) 2026 Zachariah Markusson <zachariahmarkusson@gmail.com>
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

#include "RoutingNode.h"

#include <algorithm>
#include <cassert>

namespace lmms
{

namespace
{

//! Returned for out-of-range input ports. Initialized before main() so the
//! audio thread never triggers a lazy static guard.
const std::vector<const AudioBuffer*> s_noInputs;

} // namespace

void RoutingNode::prepare(f_cnt_t frames, ch_cnt_t channels)
{
	assert(frames > 0);
	assert(channels > 0);

	m_frames = frames;
	m_channels = channels;

	m_outputs.clear();
	m_outputs.reserve(outputCount());
	for (int port = 0; port < outputCount(); ++port)
	{
		m_outputs.emplace_back(frames, channels);
	}

	m_inputs.assign(inputCount(), {});
	zeroOutputs();
}

auto RoutingNode::output(int port) -> AudioBuffer&
{
	assert(port >= 0 && port < static_cast<int>(m_outputs.size()));
	return m_outputs[port];
}

auto RoutingNode::output(int port) const -> const AudioBuffer&
{
	assert(port >= 0 && port < static_cast<int>(m_outputs.size()));
	return m_outputs[port];
}

auto RoutingNode::inputs(int port) const -> const std::vector<const AudioBuffer*>&
{
	if (port < 0 || port >= static_cast<int>(m_inputs.size())) { return s_noInputs; }
	return m_inputs[port];
}

void RoutingNode::zeroOutputs()
{
	for (AudioBuffer& out : m_outputs)
	{
		out.silenceAllChannels();
	}
}

void RoutingNode::markOutputsNonSilent()
{
	for (AudioBuffer& out : m_outputs)
	{
		for (ch_cnt_t c = 0; c < out.totalChannels(); ++c)
		{
			out.assumeNonSilent(c);
		}
	}
}

void RoutingNode::sumInputs(int port, f_cnt_t frames, AudioBuffer& dest) const
{
	for (const AudioBuffer* source : inputs(port))
	{
		if (source == nullptr) { continue; }
		const f_cnt_t count = std::min(frames, source->frames());
		const ch_cnt_t channels = std::min(dest.totalChannels(), source->totalChannels());
		for (ch_cnt_t c = 0; c < channels; ++c)
		{
			float* dst = dest.buffer(c).data();
			const float* src = source->buffer(c).data();
			for (f_cnt_t f = 0; f < count; ++f)
			{
				dst[f] += src[f];
			}
		}
	}
}

} // namespace lmms
