/*
 * RoutingChainNodes.h
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

#ifndef LMMS_ROUTING_CHAIN_NODES_H
#define LMMS_ROUTING_CHAIN_NODES_H

#include "RoutingNode.h"
#include "lmms_export.h"

namespace lmms
{

class Effect;

/**
 * @brief Put an existing signal path on a RoutingGraph.
 *
 * These two node types are the bridge between the graph and the host's own
 * audio path: an EffectChain builds one ChainInputNode and one EffectNode per
 * effect, wires them input -> effect 0 -> ... -> effect n-1, and renders the
 * block through RoutingGraph::process(). Nothing else in the graph catalogue
 * is needed to route a chain, so the node set stays minimal.
 *
 * Both nodes live for as long as the EffectChain's graph does. The pointers
 * they hold (the host's buffer, the effects) are non-owning and are set up on
 * the control thread before prepare(); process() only reads them.
 */

/**
 * @brief Sources a graph from the host's own audio block.
 *
 * A node with no inputs has nothing to render, so a graph that processes an
 * existing path needs exactly one node that reads the host's buffer instead.
 * The source buffer must outlive the node; setSource() is a control-thread
 * operation.
 */
class LMMS_EXPORT ChainInputNode : public RoutingNode
{
public:
	ChainInputNode() = default;
	explicit ChainInputNode(const AudioBuffer* source) : m_source(source) {}

	auto typeName() const -> QString override { return QStringLiteral("chain_input"); }
	auto inputCount() const -> int override { return 0; }

	//! Control thread: the buffer whose content this node emits
	void setSource(const AudioBuffer* source) { m_source = source; }
	auto source() const -> const AudioBuffer* { return m_source; }

	void process(f_cnt_t frames) override;

private:
	const AudioBuffer* m_source = nullptr;
};

/**
 * @brief Runs one Effect on the block arriving at its input port.
 *
 * The node's own output buffer is handed to Effect::processAudioBuffer(), so
 * the effect processes the graph-owned block in place and the result leaves
 * through output(0). Effects keep their own state across blocks exactly as
 * they do in the plain chain loop.
 */
class LMMS_EXPORT EffectNode : public RoutingNode
{
public:
	EffectNode() = default;
	explicit EffectNode(Effect* effect) : m_effect(effect) {}

	auto typeName() const -> QString override { return QStringLiteral("effect"); }

	void prepare(f_cnt_t frames, ch_cnt_t channels) override;
	void process(f_cnt_t frames) override;

	auto effect() -> Effect* { return m_effect; }

	/**
	 * @returns what the effect's processAudioBuffer() reported last time: false
	 *          means "this effect has nothing more to say", which the owning
	 *          chain aggregates for the mixer as it does in the plain loop.
	 */
	auto lastResult() const -> bool { return m_lastResult; }

private:
	Effect* m_effect = nullptr;
	bool m_lastResult = false;
};

} // namespace lmms

#endif // LMMS_ROUTING_CHAIN_NODES_H
