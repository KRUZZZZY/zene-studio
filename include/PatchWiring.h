/*
 * PatchWiring.h - the AUTHORED wiring of a DERIVED routing graph.
 *
 * The problem this header exists for (feature row 69 of
 * docs/FEATURE-LIST-0.3.0.md, the patcher node-graph): a chain's graph is
 * DERIVED. EffectChain::rebuildRoutingGraph() builds its node set from the
 * effect list - one ChainInputNode plus one EffectNode per effect - and wires
 * it linearly, on EVERY topology change (plugin.load, plugin.unload, moveUp,
 * moveDown, clear, loadSettings). A graph node id is therefore not stable
 * across a rebuild, and a hand-wired edge used to be discarded by the next
 * plugin.load.
 *
 * PatchWiring is the missing half: the wiring as DATA the chain owns and the
 * derived rebuild re-applies. It addresses a node by ROLE - the chain's own
 * input node, or effect <index> in chain order - rather than by a node id, so
 * it survives the rebuild that recreates the nodes.
 *
 * What this does NOT do, stated here because it is the interesting bound:
 *   * the node SET stays derived. A patch re-wires the nodes the effect list
 *     produced; it cannot add a node of its own (a filter, a second source).
 *     A graph the agent builds from scratch needs the full Patcher's canvas
 *     and is not in this release.
 *   * the wiring is session state, like the pre-existing re-wired session
 *     (docs/ROUTING-GRAPH-LIVE.md section 7): no <routinggraph> element is
 *     written into <fxchain>, so a patch does not survive a save/load.
 *   * it is not the lock-free pending-change plan swap of
 *     mixer/SPEC-dynamic-routing.md section 5.4. A patch is built off the
 *     audio thread and published under the engine's model-change guard
 *     (AudioEngine::requestChangeInModel), which is the same seam every other
 *     topology edit in this tree takes - see EffectChain::setPatchWiring.
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

#ifndef LMMS_PATCH_WIRING_H
#define LMMS_PATCH_WIRING_H

#include <vector>

#include <QJsonArray>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

/**
 * @brief One end of a patch edge, by ROLE rather than by graph node id.
 *
 * "input" is the chain's own source node (ChainInputNode); "effect:<index>"
 * is the node of the effect at that index in the chain's effect list. Both
 * roles are stable across a rebuild, which a node id is not.
 */
class LMMS_EXPORT PatchRef
{
public:
	PatchRef() = default;

	//! The chain graph's source node
	static auto input() -> PatchRef;
	//! The node of effect @a index, in chain order
	static auto effect(int index) -> PatchRef;

	auto isInput() const -> bool { return m_input; }
	auto index() const -> int { return m_index; }

	//! "input", or "effect:<index>"
	auto toString() const -> QString;
	/**
	 * Parses those two forms. @returns false and sets @a error (if given) for
	 * anything else, leaving @a ref untouched.
	 */
	static auto parse(const QString& text, PatchRef* ref, QString* error = nullptr) -> bool;

	auto equals(const PatchRef& other) const -> bool;

private:
	bool m_input = false;
	int m_index = 0;
};

//! One connection of a patch: @a from's output port into @a to's input port
struct PatchEdge
{
	PatchRef from;
	int fromPort = 0;
	PatchRef to;
	int toPort = 0;
};

/**
 * @brief The wiring a patch asks for: its edges, and the node the host block
 *        leaves the graph through.
 *
 * An EMPTY wiring is not an edge case, it is the default: it means "the
 * derived wiring", which is what a chain has until a patch is set and what it
 * returns to when the patch is cleared (@see PatchWiring::linear).
 */
class LMMS_EXPORT PatchWiring
{
public:
	auto edges() const -> const std::vector<PatchEdge>& { return m_edges; }
	auto output() const -> const PatchRef& { return m_output; }
	auto isEmpty() const -> bool { return m_edges.empty(); }
	auto size() const -> int { return static_cast<int>(m_edges.size()); }

	void clear();
	void addEdge(const PatchEdge& edge) { m_edges.push_back(edge); }
	void setOutput(const PatchRef& output) { m_output = output; }

	//! True when both wirings have the same edges, in order, and the same output
	auto equals(const PatchWiring& other) const -> bool;

	/**
	 * The wiring a chain of @a effectCount effects is DERIVED with:
	 * input -> effect 0 -> ... -> effect n-1, the last effect as the output.
	 * Empty (no edges, no output) for a chain with no effects, because such a
	 * chain has no graph at all.
	 */
	static auto linear(int effectCount) -> PatchWiring;

	//! The edges as [{"from":..,"from_port":..,"to":..,"to_port":..}, ...]
	auto toJson() const -> QJsonArray;
	/**
	 * Parses toJson()'s shape. @returns false and sets @a error (if given) when
	 * an entry is not an object or a reference does not parse; @a wiring is
	 * left as it was. The output is NOT part of this array - it is a separate
	 * field of the command's arguments (@see setOutput).
	 */
	static auto fromJson(const QJsonArray& array, PatchWiring* wiring, QString* error) -> bool;

private:
	std::vector<PatchEdge> m_edges;
	PatchRef m_output;
};

} // namespace lmms

#endif // LMMS_PATCH_WIRING_H
