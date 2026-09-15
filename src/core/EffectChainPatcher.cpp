/*
 * EffectChainPatcher.cpp - the patcher half of EffectChain.
 *
 * WHY THIS IS ITS OWN TRANSLATION UNIT. EffectChain.cpp is upstream-inherited
 * and carries a whole-tree file-length baseline row (tests/file-length-baseline-all.tsv,
 * 505 lines), and Gate 7's tolerance is zero: an inherited file that grows at
 * all fails the whole-tree scope. The patcher work - the authored wiring a
 * DERIVED graph re-applies, the role-to-node resolution and the edit entry
 * point - is therefore a fork-NEW file that DEFINES three members declared in
 * include/EffectChain.h (a definition may live in any translation unit), and
 * EffectChain.cpp net-SHRINKS: its own rebuildRoutingGraph() definition moved
 * here verbatim-in-behaviour and the file dropped below the limit.
 *
 * What lives here:
 *   * EffectChain::rebuildRoutingGraph() - unchanged in behaviour when no patch
 *     is set (same node set, same ids, same linear edges, same early returns),
 *     extended so that an AUTHORED wiring is re-applied by every rebuild. That
 *     is the DERIVED-graph constraint of feature row 69 resolved rather than
 *     documented away: a hand-wired edge is no longer discarded by the next
 *     plugin.load.
 *   * EffectChain::patchNodeId() and EffectChain::setPatchWiring() - the read
 *     and the edit the patcher.* group drives (src/core/ControlCommandsPatcher.cpp).
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QString>

#include <memory>
#include <utility>
#include <vector>

#include "EffectChain.h"

#include "AudioBuffer.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "Engine.h"
#include "PatchWiring.h"
#include "RoutingChainNodes.h"
#include "RoutingGraph.h"

namespace lmms
{

namespace
{

//! The graph node id a patch reference names, through the derived node table
//! ids[0] = the chain's input node, ids[k] = the node of effect k-1, or -1.
auto nodeIdFor(const PatchRef& ref, const std::vector<int>& ids) -> int
{
	// An unset reference names no node, and a wiring that reaches this point
	// with one fails the wiring (and is reported as dropped) rather than
	// silently routing through effect 0.
	if (!ref.isSet()) { return -1; }
	if (ref.isInput()) { return ids.empty() ? -1 : ids.front(); }

	const std::size_t index = static_cast<std::size_t>(ref.index()) + 1;
	if (ref.index() < 0 || index >= ids.size()) { return -1; }
	return ids[index];
}

/*! Connects @a wiring on @a graph through @a ids and sets its output node.
 *
 *  @returns false on the first edge the graph refuses (a port out of range, a
 *  repeated edge, a cycle) or when a reference names no node in @a ids. A
 *  false leaves whatever connected cleanly on the graph, which is why the
 *  caller treats it as "no wiring" and rebuilds from the derived one rather
 *  than keeping a half-wired graph; connect() itself rolls back the single
 *  edge it refused.
 */
auto wireGraph(RoutingGraph& graph, const PatchWiring& wiring, const std::vector<int>& ids) -> bool
{
	for (const PatchEdge& edge : wiring.edges())
	{
		const int from = nodeIdFor(edge.from, ids);
		const int to = nodeIdFor(edge.to, ids);
		if (from < 0 || to < 0) { return false; }
		if (!graph.connect(from, to, edge.fromPort, edge.toPort)) { return false; }
	}

	const int output = nodeIdFor(wiring.output(), ids);
	if (output < 0) { return false; }
	return graph.setOutputNode(output);
}

//! Why a wiring was not applied, in the two cases a caller can tell apart.
void reportPatchWiringRefusal(bool hasGraph, QString* error)
{
	if (error == nullptr) { return; }
	*error = hasGraph
		? QStringLiteral("the wiring could not be applied to this chain's graph - a reference or "
			"a port the graph refuses - so the chain is back on its derived wiring")
		: QStringLiteral("this chain does not render through its routing graph, so it has no "
			"wiring to replace: the graph is built for a chain whose effects have no "
			"audio-ports model, and it is empty when the chain has no effects, when the block "
			"size is not known yet, or when a device routes its own audio ports");
}

} // namespace

void EffectChain::rebuildRoutingGraph()
{
	m_graph->clear();
	m_effectNodes.clear();
	m_graphInput.reset();
	m_graphOutput.reset();
	m_graphActive = false;
	m_patchDropped = false;

	auto* engine = Engine::audioEngine();
	const f_cnt_t frames = engine != nullptr ? engine->framesPerPeriod() : 0;
	if (frames == 0 || m_effects.empty()) { return; }

	// Effects with audio ports route their own ports on the bus (AudioPlugin
	// overrides the bus entry point for exactly that); the graph's planar
	// blocks cannot carry that port map, so the whole chain keeps the
	// pre-existing path rather than routing half of it.
	for (const Effect* effect : m_effects)
	{
		if (effect->audioPortsModel() != nullptr) { return; }
	}

	m_graphInput = std::make_unique<AudioBuffer>(frames, DEFAULT_CHANNELS);
	m_graphOutput = std::make_unique<AudioBuffer>(frames, DEFAULT_CHANNELS);
	m_graphInput->silenceAllChannels();
	m_graphOutput->silenceAllChannels();

	auto input = std::make_unique<ChainInputNode>(m_graphInput.get());
	const int inputId = m_graph->addNode(std::move(input));

	// The node set is DERIVED - one source node plus one node per effect, in
	// list order - and these ids index it: ids[0] is the chain's input node,
	// ids[k] is the node of effect k-1. A patch addresses those roles rather
	// than these ids, because the ids are rebuilt here on every topology change.
	std::vector<int> ids;
	ids.reserve(m_effects.size() + 1);
	ids.push_back(inputId);
	for (Effect* effect : m_effects)
	{
		auto node = std::make_unique<EffectNode>(effect);
		EffectNode* const raw = node.get();
		ids.push_back(m_graph->addNode(std::move(node)));
		m_effectNodes.push_back(raw);
	}

	// The wiring is the DERIVED one (linear, in chain order) until a patch
	// replaces it, and the authored wiring is applied here on every rebuild -
	// which is what makes a hand-wired edge survive the next plugin.load.
	const PatchWiring derived = PatchWiring::linear(static_cast<int>(m_effects.size()));
	if (m_patch.isEmpty() ? !wireGraph(*m_graph, derived, ids) : !wireGraph(*m_graph, m_patch, ids))
	{
		// A patch the current effect list cannot take is DROPPED rather than
		// kept for a list it does not fit, and the chain falls back to its
		// derivation - the same wiring it had before the patch existed.
		m_patchDropped = !m_patch.isEmpty();
		m_patch.clear();
		if (!wireGraph(*m_graph, derived, ids))
		{
			// Unreachable for a linear chain (no cycle is possible); leave the
			// chain on the plain loop rather than on a half-wired graph.
			m_graph->clear();
			m_effectNodes.clear();
			m_graphInput.reset();
			m_graphOutput.reset();
			return;
		}
	}

	m_graph->prepare(frames, DEFAULT_CHANNELS);
	m_graphActive = true;
}

auto EffectChain::patchNodeId(const PatchRef& ref) const -> int
{
	if (m_graph == nullptr) { return -1; }

	// The chain's graph is always rebuilt from empty (no node is ever removed
	// from it), so its ids are dense: 0 .. nodeCount()-1.
	for (int id = 0; id < m_graph->nodeCount(); ++id)
	{
		const RoutingNode* node = m_graph->node(id);
		if (node == nullptr) { continue; }

		if (ref.isInput())
		{
			if (dynamic_cast<const ChainInputNode*>(node) != nullptr) { return id; }
			continue;
		}
		if (ref.index() < 0 || ref.index() >= static_cast<int>(m_effects.size())) { return -1; }

		const auto* effectNode = dynamic_cast<const EffectNode*>(node);
		if (effectNode != nullptr && effectNode->effect() == m_effects[ref.index()]) { return id; }
	}
	return -1;
}

auto EffectChain::setPatchWiring(const PatchWiring& wiring, QString* error) -> bool
{
	auto* engine = Engine::audioEngine();
	const PatchWiring previous = m_patch;

	// The new wiring is built off the audio thread (rebuildRoutingGraph()
	// allocates every node's buffers) and published under the model-change
	// guard the audio thread takes for a whole render period, so the edit is
	// never concurrent with process() - RoutingGraph.h's threading contract.
	if (engine != nullptr) { engine->requestChangeInModel(); }
	m_patch = wiring;
	rebuildRoutingGraph();
	// Applied == the chain renders through its graph AND the graph took the
	// wiring - except for "no patch at all", which is the derivation and is
	// applicable to any chain. A graph that did not take the wiring says so
	// through m_patchDropped, and rebuildRoutingGraph() has already put the
	// chain back on its derivation when that happens.
	const bool hasGraph = m_graphActive;
	const bool applied = wiring.isEmpty() || (m_graphActive && !m_patchDropped);
	if (!applied)
	{
		// Nothing is written: the previous wiring is rebuilt in place, so a
		// refusal leaves the chain in exactly the state it was in.
		m_patch = previous;
		rebuildRoutingGraph();
	}
	if (engine != nullptr) { engine->doneChangeInModel(); }

	if (applied) { return true; }
	reportPatchWiringRefusal(hasGraph, error);
	return false;
}

} // namespace lmms
