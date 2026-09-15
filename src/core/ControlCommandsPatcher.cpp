/*
 * ControlCommandsPatcher.cpp - the `patcher.*` command group (SPEC A11-A16):
 *                               the node graph a target's signal is processed
 *                               through, READ and EDITABLE.
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
 *      plan swap"; section 4 Part C 3: the lock-free pending-change plan swap
 *      of mixer/SPEC-dynamic-routing.md section 5.4).
 *   2. a chain's graph is DERIVED - EffectChain::rebuildRoutingGraph() re-wires
 *      it from the effect list on every change - so a hand-wired edge would be
 *      discarded by the next plugin.load.
 *
 * THIS FILE resolves both, and the resolution is the design decision row 69
 * asked for:
 *
 *   * (1) is answered by the seam the engine ALREADY has for exactly this.
 *     AudioEngine::renderNextPeriod() holds m_changeMutex for a whole render
 *     period (src/core/AudioEngine.cpp:368) and requestChangeInModel() /
 *     doneChangeInModel() lock the same mutex from the control thread
 *     (src/core/AudioEngine.cpp:628-637), so an edit published under that
 *     guard is NEVER concurrent with process() - which is the requirement the
 *     contract states, word for word. Every topology edit in this tree already
 *     takes it (EffectChain::appendEffect/removeEffect/moveUp/moveDown/clear).
 *     The edit is therefore built off the audio thread - a whole new graph,
 *     nodes and buffers included - and published under the guard:
 *     EffectChain::setPatchWiring(). It is NOT the lock-free double-buffered
 *     plan: it blocks the audio thread for the rebuild's duration, and
 *     docs/KNOWN-LIMITATIONS.md says so.
 *   * (2) is answered by making the wiring DATA the chain owns:
 *     include/PatchWiring.h. The node SET stays derived (the effect list's, so
 *     `graphMirrorsEffectList()` stays true and the graph really renders), and
 *     the AUTHORED wiring is re-applied by every rebuild - so a hand-wired edge
 *     survives plugin.load instead of being discarded by it. A wiring the
 *     effect list can no longer take is dropped, reported and documented rather
 *     than half-applied.
 *
 * A16, honestly:
 *   patcher.get_state   not_mutating  - an inspector; it writes nothing
 *   patcher.set_wiring  snapshot      - the inverse is the SAME command with
 *                                       the wiring captured before the write
 *                                       (`applies: command`), the port.set_pin
 *                                       shape: an EffectChain is a Model and a
 *                                       SerializingObject, not a
 *                                       JournallingObject, so no live
 *                                       checkpoint exists
 *
 * WHAT THIS GROUP IS NOT: there is no patcher canvas, no node you can ADD to a
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
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <utility>
#include <vector>

#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"

#include "ControlVocabulary.h"
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

//! The wiring this chain renders through right now: the authored one when a
//! patch is set, and the derived linear one otherwise.
auto effectiveWiring(const EffectChain& chain) -> PatchWiring
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

//! The wiring's edges as (fromId, toId) pairs, or false when a reference names
//! no node of the current graph (which includes an effect index that is gone).
auto wiringIds(const EffectChain& chain, const PatchWiring& wiring,
	std::vector<std::pair<int, int>>* ids, int* inputId, int* outputId) -> bool
{
	ids->clear();
	for (const PatchEdge& edge : wiring.edges())
	{
		const int from = chain.patchNodeId(edge.from);
		const int to = chain.patchNodeId(edge.to);
		if (from < 0 || to < 0) { return false; }
		ids->emplace_back(from, to);
	}
	*inputId = chain.patchNodeId(PatchRef::input());
	*outputId = chain.patchNodeId(wiring.output());
	return *inputId >= 0 && *outputId >= 0;
}

//! False when the wiring's ids contain a cycle: connect() would refuse the
//! closing edge, and a patch is validated before it is applied rather than
//! half-applied and rolled back.
auto wiringIsAcyclic(const std::vector<std::pair<int, int>>& ids) -> bool
{
	int highest = -1;
	for (const auto& edge : ids) { highest = std::max({highest, edge.first, edge.second}); }
	if (highest < 0) { return true; }

	std::vector<std::vector<int>> adjacency(highest + 1);
	std::vector<int> indegree(highest + 1, 0);
	std::vector<bool> present(highest + 1, false);
	for (const auto& edge : ids)
	{
		adjacency[edge.first].push_back(edge.second);
		++indegree[edge.second];
		present[edge.first] = true;
		present[edge.second] = true;
	}

	std::size_t live = 0;
	std::vector<int> ready;
	for (int id = 0; id <= highest; ++id)
	{
		if (!present[id]) { continue; }
		++live;
		if (indegree[id] == 0) { ready.push_back(id); }
	}

	std::size_t sorted = 0;
	while (!ready.empty())
	{
		const int id = ready.back();
		ready.pop_back();
		++sorted;
		for (const int next : adjacency[id])
		{
			if (--indegree[next] == 0) { ready.push_back(next); }
		}
	}
	return sorted == live;
}

//! False when the host block could never leave the graph: an output node the
//! input cannot reach renders silence, which is a wiring mistake rather than a
//! route (plugin.bypass is how a device is taken out of the path).
auto wiringReachesOutput(const std::vector<std::pair<int, int>>& ids, int inputId, int outputId) -> bool
{
	std::vector<int> frontier{inputId};
	std::vector<int> visited{inputId};
	while (!frontier.empty())
	{
		const int id = frontier.back();
		frontier.pop_back();
		if (id == outputId) { return true; }
		for (const auto& edge : ids)
		{
			if (edge.first != id) { continue; }
			if (std::find(visited.begin(), visited.end(), edge.second) != visited.end()) { continue; }
			visited.push_back(edge.second);
			frontier.push_back(edge.second);
		}
	}
	return false;
}

//! Repeated and self edges. connect() refuses both, but a refusal that names
//! the edge is worth more than one that says "the graph refused it".
auto wiringEdgesAreDistinct(const PatchWiring& wiring, QString* reason) -> bool
{
	const std::vector<PatchEdge>& edges = wiring.edges();
	for (std::size_t i = 0; i < edges.size(); ++i)
	{
		for (std::size_t j = i + 1; j < edges.size(); ++j)
		{
			const PatchEdge& mine = edges[i];
			const PatchEdge& theirs = edges[j];
			if (mine.fromPort != theirs.fromPort || mine.toPort != theirs.toPort
				|| !mine.from.equals(theirs.from) || !mine.to.equals(theirs.to))
			{
				continue;
			}
			*reason = QStringLiteral("two edges are the same connection (%1 port %2 -> %3 port %4)")
				.arg(mine.from.toString()).arg(mine.fromPort)
				.arg(mine.to.toString()).arg(mine.toPort);
			return false;
		}
	}
	for (const PatchEdge& edge : edges)
	{
		if (edge.from.equals(edge.to))
		{
			*reason = QStringLiteral("%1 cannot be connected to itself").arg(edge.from.toString());
			return false;
		}
	}
	return true;
}

//! Every edge's ports, against the arity of the node it names. A chain graph's
//! nodes are 1-in/1-out today except the input node (no inputs), and a wiring
//! written against a node type with other arities is checked the same way.
auto wiringPortsFit(const EffectChain& chain, const RoutingGraph& graph, const PatchWiring& wiring,
	QString* reason) -> bool
{
	for (const PatchEdge& edge : wiring.edges())
	{
		const RoutingNode* source = graph.node(chain.patchNodeId(edge.from));
		const RoutingNode* dest = graph.node(chain.patchNodeId(edge.to));
		if (source == nullptr || dest == nullptr) { continue; }  // named by the caller's ref check

		if (edge.fromPort < 0 || edge.fromPort >= source->outputCount())
		{
			*reason = QStringLiteral("%1 has %2 output port(s), so from_port %3 is out of range")
				.arg(edge.from.toString()).arg(source->outputCount()).arg(edge.fromPort);
			return false;
		}
		if (edge.toPort < 0 || edge.toPort >= dest->inputCount())
		{
			*reason = QStringLiteral("%1 has %2 input port(s), so to_port %3 is out of range")
				.arg(edge.to.toString()).arg(dest->inputCount()).arg(edge.toPort);
			return false;
		}
	}
	return true;
}

} // namespace

/*! Everything a patch can get wrong, checked against the CURRENT node set
 *  BEFORE the chain is touched, so a refusal writes nothing: an unknown
 *  reference, a repeated or self edge, a port outside a node's arity, a cycle,
 *  and an output node the input cannot reach.
 */
auto validateWiring(const EffectChain& chain, const PatchWiring& wiring, QString* reason) -> bool
{
	if (wiring.isEmpty()) { return true; }  // "the derived wiring" is always valid

	const int effectCount = static_cast<int>(chain.effects().size());
	for (const PatchEdge& edge : wiring.edges())
	{
		for (const PatchRef* end : {&edge.from, &edge.to})
		{
			if (end->isInput()) { continue; }
			if (end->index() >= 0 && end->index() < effectCount) { continue; }
			*reason = QStringLiteral("%1 names no node of this chain: it has %2 effect(s)")
				.arg(end->toString()).arg(effectCount);
			return false;
		}
	}
	if (!wiring.output().isInput()
		&& (wiring.output().index() < 0 || wiring.output().index() >= effectCount))
	{
		*reason = QStringLiteral("the output node %1 names no node of this chain: it has %2 effect(s)")
			.arg(wiring.output().toString()).arg(effectCount);
		return false;
	}
	if (!wiringEdgesAreDistinct(wiring, reason)) { return false; }

	const RoutingGraph& graph = chain.routingGraph();
	if (!wiringPortsFit(chain, graph, wiring, reason)) { return false; }

	std::vector<std::pair<int, int>> ids;
	int inputId = -1;
	int outputId = -1;
	if (!wiringIds(chain, wiring, &ids, &inputId, &outputId))
	{
		*reason = QStringLiteral("the wiring names a node this chain's graph does not have");
		return false;
	}
	if (!wiringIsAcyclic(ids))
	{
		*reason = QStringLiteral("the wiring has a cycle: this graph is a DAG and a feedback path is not "
			"expressible in it");
		return false;
	}
	if (!wiringReachesOutput(ids, inputId, outputId))
	{
		*reason = QStringLiteral("the chain's input cannot reach the output node %1, so the graph would "
			"render silence; wire a path to it, or point `output` at a node the input reaches")
			.arg(wiring.output().toString());
		return false;
	}
	return true;
}

//! The wiring a command asks for: its edges, and its output node - the last
//! effect when `output` is absent, which is the node the derivation uses.
auto wiringFromArgs(const QJsonObject& args, const EffectChain& chain, PatchWiring* wanted,
	ControlResult* error) -> bool
{
	const QJsonValue edges = args.value(QStringLiteral("edges"));
	if (edges.isUndefined() || edges.isNull())
	{
		wanted->clear();  // documented default: the derived wiring
	}
	else if (!edges.isArray())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'edges' must be an array of {from, to} objects; an empty array - or no "
				"'edges' at all - restores the derived wiring"));
		return false;
	}
	else
	{
		QString parseError;
		if (!PatchWiring::fromJson(edges.toArray(), wanted, &parseError))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'edges' is malformed: %1").arg(parseError));
			return false;
		}
	}

	const QString output = args.value(QStringLiteral("output")).toString();
	if (output.isEmpty())
	{
		if (wanted->isEmpty()) { return true; }  // nothing to route: nothing to output
		const int effectCount = static_cast<int>(chain.effects().size());
		if (effectCount == 0)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("an edge list needs an 'output' node on a chain this empty: it has no "
					"effect for the default output to name"));
			return false;
		}
		wanted->setOutput(PatchRef::effect(effectCount - 1));
		return true;
	}

	QString refError;
	PatchRef ref;
	if (!PatchRef::parse(output, &ref, &refError))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, refError);
		return false;
	}
	wanted->setOutput(ref);
	return true;
}

ControlResult getState(const QJsonObject& args)
{
	ControlResult error;
	ControlTarget target;
	if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
	{
		return error;
	}
	return ControlResult::success(patcherStateJson(*target.chain, target));
}

ControlResult setWiring(const QJsonObject& args)
{
	ControlResult error;
	ControlTarget target;
	if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
	{
		return error;
	}
	EffectChain& chain = *target.chain;

	PatchWiring wanted;
	if (!wiringFromArgs(args, chain, &wanted, &error)) { return error; }

	QString reason;
	if (!validateWiring(chain, wanted, &reason))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, reason);
	}

	const bool wasAuthored = chain.patchActive();
	const PatchWiring before = effectiveWiring(chain);

	if (!chain.setPatchWiring(wanted, &reason))
	{
		return ControlResult::failure(ControlErrorKind::Refused, reason);
	}

	QJsonObject result = patcherStateJson(chain, target);
	result.insert(QStringLiteral("previous"), wiringJson(before));
	result.insert(QStringLiteral("changed"), !before.equals(effectiveWiring(chain)));

	// SPEC A16: an EffectChain is a Model and a SerializingObject, not a
	// JournallingObject, so there is no live checkpoint to take. The inverse is
	// the SAME command with the wiring captured before the write, and a chain
	// that WAS on its derived wiring comes back to it AS the derivation (an
	// empty edge list) rather than as a linear-looking authored patch.
	QJsonObject beforeState;
	beforeState.insert(QStringLiteral("wiring"),
		wasAuthored ? QStringLiteral("authored") : QStringLiteral("derived"));
	beforeState.insert(QStringLiteral("edges"), before.toJson());
	beforeState.insert(QStringLiteral("output"), before.output().toString());

	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("target"), target.id);
	inverseArgs.insert(QStringLiteral("edges"), wasAuthored ? before.toJson() : QJsonArray());
	if (wasAuthored) { inverseArgs.insert(QStringLiteral("output"), before.output().toString()); }

	QJsonObject transaction = transactionPayload(beforeState, QStringLiteral("patcher.set_wiring"),
		inverseArgs, true,
		QStringLiteral("snapshot: the previous wiring is a bounded list of edges plus one output node, and "
			"the recorded inverse is patcher.set_wiring with it - empty when the chain was on its derived "
			"wiring, so the derivation comes back as the derivation. There is no JournallingObject behind "
			"an EffectChain (a Model and a SerializingObject), so no live checkpoint exists"));
	QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
	inverse.insert(QStringLiteral("applies"), QStringLiteral("command"));
	transaction.insert(QStringLiteral("inverse"), inverse);
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

} // namespace

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

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("patcher.set_wiring");
		cmd.group = QStringLiteral("patcher");
		cmd.verb = QStringLiteral("set_wiring");
		cmd.description = QStringLiteral("Re-wire a target's effect chain: 'edges' is the whole wiring "
			"({from, to, from_port, to_port} objects whose ends are \"input\" or \"effect:<index>\"), "
			"'output' is the node the host block leaves through (the last effect by default), and an "
			"empty or absent edge list restores the DERIVED wiring. The new graph is built off the audio "
			"thread and published under the audio engine's model-change guard, so the edit is never "
			"concurrent with the render (include/RoutingGraph.h's threading contract) and it survives "
			"the next plugin.load - the derived rebuild re-applies it. Refused, typed, with nothing "
			"written, when the chain does not render through its graph (see patcher.get_state's "
			"`editable`), when an edge would make a cycle, or when the output cannot be reached from the "
			"input. Reversible: the previous wiring, derived or authored, is restored by control.undo.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("edges"), arrayProperty()},
			{QStringLiteral("output"), stringProperty()},
		}, {QStringLiteral("target")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("wiring"), stringProperty()},
			{QStringLiteral("effective_wiring"), objectProperty()},
			{QStringLiteral("previous"), objectProperty()},
			{QStringLiteral("changed"), booleanProperty()},
			{QStringLiteral("routes_through_graph"), booleanProperty()},
			{QStringLiteral("graph"), objectProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return setWiring(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
