/*
 * RackNodes.cpp
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

#include "RackNodes.h"

#include <algorithm>

#include "AudioBuffer.h"
#include "AudioBus.h"
#include "EffectChain.h"
#include "lmms_constants.h"

namespace lmms
{

RackChainNode::RackChainNode(EffectChain* chain) :
	m_chain(chain)
{
}

RackChainNode::~RackChainNode() = default;

void RackChainNode::prepare(f_cnt_t frames, ch_cnt_t channels)
{
	RoutingNode::prepare(frames, channels);

	// The audio thread may not allocate, so the interleaved pair and the bus
	// over it are built here, on the control thread.
	m_buffer.assign(static_cast<std::size_t>(frames) * 2, SampleFrame{});
	m_busData[0] = m_buffer.data();
	m_bus = std::make_unique<AudioBus>(m_busData, 1, frames);
}

void RackChainNode::process(f_cnt_t frames)
{
	AudioBuffer& out = output(0);
	zeroOutputs();

	// The chain's input block: what the graph's wiring feeds this node. The
	// sum stays in the node's own buffer, which is also where the chain's
	// result lands, so a chain that decides not to process (its effects are
	// disabled) passes its input through unchanged - the meaning its disabled
	// flag already has on the channel's own path.
	sumInputs(0, frames, out);

	bool inputLive = false;
	for (const AudioBuffer* source : inputs(0))
	{
		if (source != nullptr && source->hasAnySignal())
		{
			inputLive = true;
			break;
		}
	}

	if (m_bus == nullptr || m_chain == nullptr)
	{
		m_lastResult = false;
		return;
	}

	// Planar -> interleaved: the block entry point the chain speaks.
	const f_cnt_t count = std::min(frames, out.frames());
	{
		const float* const left = out.buffer(0).data();
		const float* const right = out.buffer(1).data();
		float* const interleaved = reinterpret_cast<float*>(m_busData[0]);
		for (f_cnt_t f = 0; f < count; ++f)
		{
			interleaved[2 * f] = left[f];
			interleaved[2 * f + 1] = right[f];
		}
	}
	m_bus->quietChannels().set(0, !inputLive);
	m_bus->quietChannels().set(1, !inputLive);

	// The channel's own chain machinery, on the chain's own fallbacks.
	m_lastResult = m_chain->processAudioBuffer(*m_bus, m_sidechain);

	// Interleaved -> planar: the chain's result leaves through output(0).
	{
		const float* const interleaved = reinterpret_cast<const float*>(m_busData[0]);
		float* const left = out.buffer(0).data();
		float* const right = out.buffer(1).data();
		for (f_cnt_t f = 0; f < count; ++f)
		{
			left[f] = interleaved[2 * f];
			right[f] = interleaved[2 * f + 1];
		}
	}
	if (!m_bus->quietChannels()[0] || !m_bus->quietChannels()[1])
	{
		markOutputsNonSilent();
	}
}

void RackSumNode::process(f_cnt_t frames)
{
	AudioBuffer& out = output(0);
	zeroOutputs();
	sumInputs(0, frames, out);

	// A block summed from live inputs is real audio; a sum of nodes that are
	// all flagged silent stays flagged silent, so a sleeping effect downstream
	// still auto-quits instead of being woken by a block full of zeros.
	for (const AudioBuffer* source : inputs(0))
	{
		if (source != nullptr && source->hasAnySignal())
		{
			markOutputsNonSilent();
			break;
		}
	}
}

} // namespace lmms
