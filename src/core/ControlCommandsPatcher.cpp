/*
 * ControlCommandsPatcher.cpp - the `patcher.*` command group (SPEC A11-A16),
 *                               READ HALF: the node graph a target's signal is
 *                               processed through, addressed as a PATCH.
 *
 * THE ITEM THIS CLOSES. Feature row 69 of docs/FEATURE-LIST-0.3.0.md, "Patcher
 * node-graph driving" (the audit's Group A #7): "no `patcher.` id exists at
 * either base; the routing-graph engine it would drive is in the tree (row 28)
 * and the patcher GUI is out of scope". Row 28 recorded the graph as an
 * INSPECTOR and named the two reasons it had no setter:
 *
 *   1. include/RoutingGraph.h's threading contract: topology edits and
 *      prepare() are control-thread operations that "must not run concurrently
 *      with process()", and making live edits safe needs "the atomic plan swap
 *      described in the full design (see PATCHER-MVP.md); this spike
 *      deliberately does not implement it" (PATCHER-MVP.md section 3: "No live
 *      plan swap"; section 4 Part C 3 names the lock-free pending-change plan
 *      swap of mixer/SPEC-dynamic-routing.md section 5.4).
 *   2. a chain's graph is DERIVED - EffectChain::rebuildRoutingGraph() re-wires
 *      it from the effect list on every change - so a hand-wired edge would be
 *      discarded by the next plugin.load.
 *
 * THE DESIGN DECISION row 69 asked for, and the evidence for it:
 *
 *   * (1) is answered by the seam the engine ALREADY has for exactly this.
 *     AudioEngine::renderNextPeriod() holds m_changeMutex for a whole render
 *     period (src/core/AudioEngine.cpp:368) and requestChangeInModel() /
 *     doneChangeInModel() lock the same mutex from the control thread
 *     (src/core/AudioEngine.cpp:628-637), so an edit published under that guard
 *     is NEVER concurrent with process() - the requirement the contract states
 *     word for word. Every topology edit in this tree already takes it
 *     (EffectChain::appendEffect/removeEffect/moveUp/moveDown/clear). The edit
 *     builds a whole new graph off the audio thread - nodes, buffers and plan
 *     included - and publishes it under the guard: EffectChain::setPatchWiring()
 *     (src/core/EffectChainPatcher.cpp). It is NOT the lock-free double-buffered
 *     plan: it blocks the audio thread for the rebuild's duration, and
 *     docs/KNOWN-LIMITATIONS.md says so.
 *   * (2) is answered by making the wiring DATA the chain owns
 *     (include/PatchWiring.h). The node SET stays derived - the effect list's,
 *     so `graphMirrorsEffectList()` stays true and the graph really renders -
 *     and the AUTHORED wiring is re-applied by every rebuild, so a hand-wired
 *     edge survives plugin.load instead of being discarded by it. A wiring the
 *     effect list can no longer take is dropped, reported
 *     (patcher.get_state's `patch_dropped`) and documented rather than
 *     half-applied.
 *
 * A16, honestly:
 *   patcher.get_state   not_mutating  - an inspector; it writes nothing
 *   patcher.set_wiring  snapshot      - its own row, in the EDIT half's file;
 *                                       the inverse is the same command with the
 *                                       wiring captured before the write
 *
 * WHAT THIS GROUP IS NOT: no patcher canvas, no node an agent can ADD to a
 * chain's graph (the node set is the effect list's), no parameter pin and no
 * <routinggraph> XML. docs/PATCHER-GRAPH.md states the bounds;
 * docs/KNOWN-LIMITATIONS.md carries the one-line absence.
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

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <vector>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"

#include "ControlCommandsPatcherShared.h"
#include "EffectChain.h"
#include "PatchWiring.h"
#include "RoutingChainNodes.h"
#include "RoutingGraph.h"
#include "RoutingNode.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

constexpr auto NODE_SET_TEXT = "derived";

//! The role a graph node is addressed by, or an empty string for a node this
//! chain did not build (nothing else is ever in a chain's graph).
QString nodeRefText(const EffectChain& chain, const RoutingNode& node)
{
	if (dynamic_cast<const ChainInputNode*>(&node) != nullptr)
	{
		return PatchRef::input().toString();
	}
	const auto* effectNode = dynamic_cast<const EffectNode*>(&node);
	if (effectNode == nullptr) { return QString(); }

	const std::vector<Effect*>& effects = chain.effects();
	for (std::size_t i = 0; i < effects.size(); ++i)
	{
		if (effects[i] == effectNode->effect())
		{
			return PatchRef::effect(static_cast<int>(i)).toString();
		}
	}
	return QString();
}

//! A node's own parameters, read through its serialization: the <param name
//! value> children RoutingNode::saveSettings() writes. No per-node accessor is
//! needed, so a node type added later is reported without touching this file.
QJsonObject nodeParams(const RoutingNode& node)
{
	QDomDocument document;
	QDomElement element = document.createElement(QStringLiteral("node"));
	node.saveSettings(element);

	QJsonObject params;
	for (QDomElement param = element.firstChildElement(QStringLiteral("param")); !param.isNull();
		param = param.nextSiblingElement(QStringLiteral("param")))
	{
		params.insert(param.attribute(QStringLiteral("name")),
			param.attribute(QStringLiteral("value")).toDouble());
	}
	return params;
}

//! One node: its id, the role a patch addresses it by, its type, its arity, its
//! parameters and whether it is prepared.
QJsonObject nodeJson(const EffectChain& chain, const RoutingGraph& graph, int id)
{
	const RoutingNode* node = graph.node(id);
	QJsonObject out;
	out.insert(QStringLiteral("id"), id);
	if (node == nullptr)
	{
		// removeNode() leaves the id reserved; a hole is reported as a hole.
		out.insert(QStringLiteral("ref"), QJsonValue::Null);
		out.insert(QStringLiteral("type"), QJsonValue::Null);
		out.insert(QStringLiteral("removed"), true);
		return out;
	}
	out.insert(QStringLiteral("ref"), nodeRefText(chain, *node));
	out.insert(QStringLiteral("type"), node->typeName());
	out.insert(QStringLiteral("inputs"), node->inputCount());
	out.insert(QStringLiteral("outputs"), node->outputCount());
	out.insert(QStringLiteral("prepared"), node->isPrepared());
	out.insert(QStringLiteral("params"), nodeParams(*node));
	return out;
}

//! The graph as it IS: the derived node set with its roles, the edges in the
//! graph's own node ids, and the cached order the audio thread walks.
QJsonObject graphJson(const EffectChain& chain, const RoutingGraph& graph)
{
	QJsonArray nodes;
	for (int id = 0; id < graph.nodeCount(); ++id) { nodes.append(nodeJson(chain, graph, id)); }

	QJsonArray connections;
	for (const RoutingConnection& connection : graph.connections())
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("from"), connection.sourceNode);
		entry.insert(QStringLiteral("from_port"), connection.sourcePort);
		entry.insert(QStringLiteral("to"), connection.destNode);
		entry.insert(QStringLiteral("to_port"), connection.destPort);
		connections.append(entry);
	}

	QJsonArray order;
	for (const int id : graph.processingOrder()) { order.append(id); }

	QJsonObject out;
	out.insert(QStringLiteral("nodes"), nodes);
	out.insert(QStringLiteral("node_count"), graph.nodeCount());
	out.insert(QStringLiteral("connections"), connections);
	out.insert(QStringLiteral("connection_count"), static_cast<int>(graph.connections().size()));
	out.insert(QStringLiteral("processing_order"), order);
	out.insert(QStringLiteral("output_node"), graph.outputNodeId());
	out.insert(QStringLiteral("prepared"), graph.isPrepared());
	out.insert(QStringLiteral("frames"), static_cast<int>(graph.frames()));
	out.insert(QStringLiteral("channels"), static_cast<int>(graph.channels()));
	return out;
}

//! Whether an edit can land at all, and why not when it cannot - machine
//! readable, because "the graph is empty" is the measured normal case for a
//! chain of audio-plugin devices (EffectChain::rebuildRoutingGraph() returns
//! early for those) and a client has to be able to tell that from a bug.
QJsonObject editabilityJson(const EffectChain& chain)
{
	const bool routable = chain.routesThroughGraph();

	QJsonObject out;
	out.insert(QStringLiteral("editable"), routable);
	out.insert(QStringLiteral("node_set"), QString::fromLatin1(NODE_SET_TEXT));
	out.insert(QStringLiteral("persisted"), false);
	out.insert(QStringLiteral("reason"), routable ? QString()
		: QStringLiteral("this chain does not render through its routing graph, so there is no wiring to "
			"replace: the graph is built for a chain whose effects have no audio-ports model, and it is "
			"empty when the chain has no effects, when the block size is not known yet, or when a device "
			"routes its own audio ports (an audio-plugin device) - such a chain keeps its plain effect "
			"loop (EffectChain::rebuildRoutingGraph)"));
	return out;
}

ControlResult getState(const QJsonObject& args)
{
	ControlResult error;
	ControlTarget target;
	if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
	{
		return error;
	}
	return ControlResult::success(control::patcherStateJson(*target.chain, target));
}

} // namespace

namespace control
{

QJsonObject patcherStateJson(const EffectChain& chain, const ControlTarget& target)
{
	const RoutingGraph& graph = chain.routingGraph();
	QJsonObject result;
	result.insert(QStringLiteral("target"), target.id);
	result.insert(QStringLiteral("kind"), target.kind);
	result.insert(QStringLiteral("target_type"), target.typeName);
	result.insert(QStringLiteral("effect_count"), static_cast<int>(chain.effects().size()));
	result.insert(QStringLiteral("routes_through_graph"), chain.routesThroughGraph());
	// "derived" is the default and "authored" means a patcher.set_wiring is in
	// force; patch_dropped reports that a rebuild could not re-apply one.
	result.insert(QStringLiteral("wiring"), chain.patchActive()
		? QStringLiteral("authored") : QStringLiteral("derived"));
	result.insert(QStringLiteral("patch_dropped"), chain.patchDropped());
	result.insert(QStringLiteral("effective_wiring"), wiringJson(effectiveWiring(chain)));
	result.insert(QStringLiteral("graph"), graphJson(chain, graph));
	result.insert(QStringLiteral("editable"), editabilityJson(chain));
	result.insert(QStringLiteral("note"),
		QStringLiteral("the node SET is derived from the effect list (one input node, one node per "
			"effect) and a patch only re-wires it, so the roles a patch addresses - \"input\" and "
			"\"effect:<index>\" - survive every rebuild; the wiring is re-applied on every "
			"EffectChain::rebuildRoutingGraph(), which is why a hand-wired edge is no longer discarded "
			"by the next plugin.load. The wiring is session state: it is not written into <fxchain> yet. "
			"The graph is only on the signal path when routes_through_graph is true - a chain of "
			"audio-plugin devices keeps its plain effect loop."));
	return result;
}

PatchWiring effectiveWiring(const EffectChain& chain)
{
	if (chain.patchActive()) { return chain.patchWiring(); }
	return PatchWiring::linear(static_cast<int>(chain.effects().size()));
}

QJsonObject wiringJson(const PatchWiring& wiring)
{
	QJsonObject out;
	out.insert(QStringLiteral("edges"), wiring.toJson());
	out.insert(QStringLiteral("edge_count"), wiring.size());
	out.insert(QStringLiteral("output"), wiring.output().toString());
	return out;
}

} // namespace control

void registerPatcherCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("patcher.get_state");
		cmd.group = QStringLiteral("patcher");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("The patcher node graph of a target's effect chain: every "
			"node with the role a patch addresses it by (\"input\", \"effect:<index>\"), its type, its "
			"ports, its own parameters and whether it is prepared; the edges in node ids and in roles; "
			"the cached topological order the audio thread walks; the output node; and whether the "
			"wiring is the DERIVED one (linear, from the effect list) or an AUTHORED patch. Read-only. "
			"`editable` reports whether an edit can land at all, with the reason when it cannot.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("target"), stringProperty()}},
			{QStringLiteral("target")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("kind"), stringProperty()},
			{QStringLiteral("target_type"), stringProperty()},
			{QStringLiteral("effect_count"), integerProperty(0, 4096)},
			{QStringLiteral("routes_through_graph"), booleanProperty()},
			{QStringLiteral("wiring"), stringProperty()},
			{QStringLiteral("patch_dropped"), booleanProperty()},
			{QStringLiteral("effective_wiring"), objectProperty()},
			{QStringLiteral("graph"), objectProperty()},
			{QStringLiteral("editable"), objectProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return getState(args); };
		registry.registerCommand(cmd);
	}

	// The group's EDIT half (patcher.set_wiring), in its own translation unit.
	registerPatcherEditCommands(registry);
}

} // namespace lmms
