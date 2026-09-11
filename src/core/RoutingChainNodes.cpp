/*
 * RoutingChainNodes.cpp
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

#include "RoutingChainNodes.h"

#include <algorithm>
#include <cstring>

#include "AudioBuffer.h"
#include "Effect.h"

namespace lmms
{

void ChainInputNode::process(f_cnt_t frames)
{
	AudioBuffer& out = output(0);

	// A source with no host buffer is silent, not stale: the graph's boundary
	// buffer only exists between prepare() and teardown.
	zeroOutputs();
	if (m_source == nullptr) { return; }

	const f_cnt_t count = std::min(frames, m_source->frames());
	const ch_cnt_t channels = std::min(out.totalChannels(), m_source->totalChannels());
	for (ch_cnt_t c = 0; c < channels; ++c)
	{
		std::memcpy(out.buffer(c).data(), m_source->buffer(c).data(), count * sizeof(float));

		// The graph and the host use the same silence-flag polarity (1 = quiet),
		// so the host's flags carry over instead of the block being re-scanned.
		if (!m_source->silenceFlags()[c]) { out.assumeNonSilent(c); }
	}
}

void EffectNode::prepare(f_cnt_t frames, ch_cnt_t channels)
{
	RoutingNode::prepare(frames, channels);

	for (int port = 0; port < outputCount(); ++port)
	{
		// Effect::processAudioBuffer(AudioBuffer&) converts through the buffer's
		// interleaved scratch; allocate it here so the audio thread never does.
		output(port).allocateInterleavedBuffer();
	}
}

void EffectNode::process(f_cnt_t frames)
{
	AudioBuffer& out = output(0);
	zeroOutputs();
	sumInputs(0, frames, out);

	// Which block is real audio: a block summed from live inputs, or a node with
	// no live input that stays flagged silent so a sleeping effect downstream
	// still auto-quits instead of being woken by a block full of zeros.
	for (const AudioBuffer* source : inputs(0))
	{
		if (source != nullptr && source->hasAnySignal())
		{
			markOutputsNonSilent();
			break;
		}
	}

	// Effect::processAudioBuffer(AudioBuffer&) is the legacy entry point: it
	// renders processImpl() on the buffer's interleaved scratch and converts
	// back to planar afterwards. The graph carries planar blocks (that is what
	// RoutingGraph and RoutingNode speak), so hand the effect the block that
	// arrived on the connection instead of a stale scratch.
	if (out.hasInterleavedBuffer())
	{
		toInterleaved(out.groupBuffers(0), out.interleavedBuffer());
	}

	m_lastResult = (m_effect != nullptr) && m_effect->processAudioBuffer(out);
}

} // namespace lmms
