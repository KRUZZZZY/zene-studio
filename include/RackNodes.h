/*
 * RackNodes.h
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

#ifndef LMMS_RACK_NODES_H
#define LMMS_RACK_NODES_H

#include <memory>
#include <vector>

#include "RoutingNode.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

class AudioBuffer;
class AudioBus;
class EffectChain;

/**
 * @brief The two node types a Rack wires up, and nothing else.
 *
 * A rack is the channel's own effect chain plus zero or more parallel chains,
 * wired on one RoutingGraph: a ChainInputNode (from RoutingChainNodes.h) feeds
 * every chain the graph renders, each chain is one RackChainNode, and
 * RackSumNode sums the chains into the rack's output. No new DSP is written
 * here: a RackChainNode calls the existing EffectChain block entry point, so a
 * chain in a rack is processed by exactly the machinery that processed it when
 * it was the whole signal path.
 *
 * Both nodes live as long as the rack's graph does. The pointers they hold
 * (the chain, the sidechain input) are non-owning and are set on the control
 * thread before prepare(); process() only reads them.
 */

/**
 * @brief Runs one EffectChain on the block arriving at its input port.
 *
 * The chain is called through EffectChain::processAudioBuffer(AudioBus&) -
 * the same entry point the mixer uses for a channel's own chain - so a rack
 * chain keeps its own fallbacks: it renders through its own RoutingGraph when
 * it can ((docs/ROUTING-GRAPH-LIVE.md)) and through its plain effect loop when
 * it cannot. The node's only job is to bridge between the graph's planar
 * blocks and the interleaved bus that entry point takes.
 */
class LMMS_EXPORT RackChainNode : public RoutingNode
{
public:
	explicit RackChainNode(EffectChain* chain);
	~RackChainNode() override;

	RackChainNode(const RackChainNode&) = delete;
	auto operator=(const RackChainNode&) = delete;

	auto typeName() const -> QString override { return QStringLiteral("rack_chain"); }

	void prepare(f_cnt_t frames, ch_cnt_t channels) override;
	void process(f_cnt_t frames) override;

	auto chain() const -> EffectChain* { return m_chain; }

	/**
	 * The channel's sidechain input for the block currently being processed,
	 * or nullptr. Audio thread: the rack sets this before it runs the graph,
	 * so every chain in the rack sees the same sidechain input the channel's
	 * own chain would have seen.
	 */
	void setSidechain(const AudioBuffer* sidechain) { m_sidechain = sidechain; }

	/**
	 * @returns what the chain's processAudioBuffer() reported last time: false
	 *          means "this chain has nothing more to say", which the rack
	 *          aggregates for the mixer as the single chain used to.
	 */
	auto lastResult() const -> bool { return m_lastResult; }

private:
	EffectChain* m_chain = nullptr;
	const AudioBuffer* m_sidechain = nullptr;

	/**
	 * The interleaved block handed to the chain: one stereo pair, the same
	 * shape the mixer gives a channel's own chain. Allocated in prepare() so
	 * process() never allocates.
	 */
	std::vector<SampleFrame> m_buffer;
	//! The pointer table an AudioBus is built over; points at m_buffer
	SampleFrame* m_busData[1] = {nullptr};
	//! AudioBus is not assignable (its members are const), so it is built here
	std::unique_ptr<AudioBus> m_bus;
	bool m_lastResult = false;
};

/**
 * @brief Sums every input connected to its single input port into its output.
 *
 * The rack's output node: in parallel mode every chain feeds it and the sum is
 * the channel's output; with a chain selected, exactly one chain feeds it. A
 * port takes any number of connections, so the node needs no per-chain state.
 */
class LMMS_EXPORT RackSumNode : public RoutingNode
{
public:
	RackSumNode() = default;

	auto typeName() const -> QString override { return QStringLiteral("rack_sum"); }

	void process(f_cnt_t frames) override;
};

} // namespace lmms

#endif // LMMS_RACK_NODES_H
