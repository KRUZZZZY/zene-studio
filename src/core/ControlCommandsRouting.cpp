/*
 * ControlCommandsRouting.cpp - the routing.* command group (SPEC A11-A16).
 *
 * Feature row 28 of docs/FEATURE-LIST-0.3.0.md ("Routing graph"). The engine
 * side exists, is on the audio path and is proven by registered tests
 * (tests/src/core/RoutingGraphTest.cpp, RoutingGraphLiveTest.cpp): the class is
 * include/RoutingGraph.h:64 with its nodes in RoutingNodes.h /
 * RoutingChainNodes.h, and TWO live graphs own one - EffectChain::routingGraph()
 * (the chain a track or a mixer channel renders through, built by
 * EffectChain::rebuildRoutingGraph) and Rack::routingGraph() (the rack's
 * parallel chains). The audit's row 28 says the group is missing and "the
 * patcher GUI is missing and is out of scope". This file registers the read.
 *
 * DESIGN DECISION - no routing setter verb, and this is the honest half of row
 * 28 rather than a trimmed deliverable:
 *   * RoutingGraph's own threading contract (include/RoutingGraph.h:58-62) says
 *     topology edits and prepare() are CONTROL-THREAD operations that "must not
 *     run concurrently with process()", and that making live edits safe needs
 *     the atomic plan swap of the full design, which the spike "deliberately
 *     does not implement". A command that re-wired a graph on the signal path
 *     would therefore be a data race with the audio thread, not a feature.
 *   * Even where that were safe, the topology of a chain's graph is DERIVED:
 *     EffectChain::rebuildRoutingGraph() clears the graph and re-wires it
 *     linearly from the effect list on every list change, so a hand-wired edge
 *     would be discarded by the next plugin.load / plugin.unload. The settable
 *     topology in this release is the MIXER's (mixer.route_to / mixer.send_to /
 *     mixer.sidechain_to / mixer.route_remove / bus.create), which is real,
 *     journalled state rather than a rebuilt cache.
 * So this group is an inspector: what the graph IS, so a client can read the
 * route a signal takes. docs/KNOWN-LIMITATIONS.md carries the absence line.
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"
#include "ControlRackSupport.h"
#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "Rack.h"
#include "RoutingGraph.h"
#include "RoutingNode.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! One node of a graph: its stable id, its serialized type name and its arity.
QJsonObject nodeJson(const RoutingGraph& graph, int id)
{
	const RoutingNode* node = graph.node(id);
	QJsonObject out;
	out.insert(QStringLiteral("id"), id);
	if (node == nullptr)
	{
		// removeNode() empties the slot and leaves the id reserved; a hole is
		// reported as a hole rather than hidden, so the ids a client reads are
		// the graph's own indices.
		out.insert(QStringLiteral("type"), QJsonValue::Null);
		out.insert(QStringLiteral("removed"), true);
		return out;
	}
	out.insert(QStringLiteral("type"), node->typeName());
	out.insert(QStringLiteral("inputs"), node->inputCount());
	out.insert(QStringLiteral("outputs"), node->outputCount());
	out.insert(QStringLiteral("prepared"), node->isPrepared());
	return out;
}

QJsonArray nodeReport(const RoutingGraph& graph)
{
	QJsonArray nodes;
	for (int id = 0; id < graph.nodeCount(); ++id) { nodes.append(nodeJson(graph, id)); }
	return nodes;
}

QJsonArray connectionReport(const RoutingGraph& graph)
{
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
	return connections;
}

QJsonArray orderReport(const RoutingGraph& graph)
{
	QJsonArray order;
	for (int id : graph.processingOrder()) { order.append(id); }
	return order;
}

/*! One RoutingGraph, as every graph in this group is reported: the type-name'd
 *  node set, the edges, the cached topological order the audio thread walks and
 *  the output node whose first output is copied into the host buffer.
 */
QJsonObject graphJson(const RoutingGraph& graph)
{
	QJsonObject out;
	out.insert(QStringLiteral("node_count"), graph.nodeCount());
	out.insert(QStringLiteral("nodes"), nodeReport(graph));
	out.insert(QStringLiteral("connection_count"),
		static_cast<int>(graph.connections().size()));
	out.insert(QStringLiteral("connections"), connectionReport(graph));
	out.insert(QStringLiteral("processing_order"), orderReport(graph));
	// -1 when the graph has no output node (an unwired or cleared graph).
	out.insert(QStringLiteral("output_node"), graph.outputNodeId());
	out.insert(QStringLiteral("prepared"), graph.isPrepared());
	out.insert(QStringLiteral("frames"), static_cast<int>(graph.frames()));
	out.insert(QStringLiteral("channels"), static_cast<int>(graph.channels()));
	return out;
}

//! The effect chain a target owns, and whether that chain renders through its
//! graph rather than through its plain effect loop.
QJsonObject chainSection(const ControlTarget& target)
{
	const EffectChain& chain = *target.chain;
	QJsonObject out;
	out.insert(QStringLiteral("effect_count"), static_cast<int>(chain.effects().size()));
	out.insert(QStringLiteral("routes_through_graph"), chain.routesThroughGraph());
	out.insert(QStringLiteral("latency_frames"), chain.latencyFrames());
	out.insert(QStringLiteral("graph"), graphJson(chain.routingGraph()));
	return out;
}

//! The rack's graph, for a mixer channel that has one.
QJsonObject rackSection(const QString& targetId)
{
	ControlResult ignored;
	Rack* rack = resolveRack(targetId, &ignored);
	if (rack == nullptr) { return QJsonObject(); }
	QJsonArray routed;
	for (int chain : rack->routedChains()) { routed.append(chain); }
	QJsonObject out;
	out.insert(QStringLiteral("selected_chain"), rack->selectedChain());
	out.insert(QStringLiteral("routed_chains"), routed);
	out.insert(QStringLiteral("graph"), graphJson(rack->routingGraph()));
	return out;
}

ControlResult handleRoutingGetState(const QJsonObject& args)
{
	ControlResult error;
	ControlTarget target;
	const QString targetId = args.value(QStringLiteral("target")).toString();
	if (!resolveControlTarget(targetId, &target, &error)) { return error; }

	QJsonObject result;
	result.insert(QStringLiteral("target"), target.id);
	result.insert(QStringLiteral("kind"), target.kind);
	result.insert(QStringLiteral("target_type"), target.typeName);
	result.insert(QStringLiteral("chain"), chainSection(target));
	if (target.kind == QLatin1String("channel"))
	{
		result.insert(QStringLiteral("rack"), rackSection(target.id));
	}
	else
	{
		// A track's chain is owned by its AudioBusHandle; the rack is a mixer
		// channel's own object, so a track target has none. null rather than an
		// empty object, so a client can tell "no rack" from "an empty rack".
		result.insert(QStringLiteral("rack"), QJsonValue::Null);
	}
	result.insert(QStringLiteral("note"),
		QStringLiteral("the topology is DERIVED: EffectChain::rebuildRoutingGraph re-wires the graph "
			"linearly from the effect list on every change, and Rack::rebuildRoutingGraph does the "
			"same from the rack's chain list and its selection. No command edits either graph - the "
			"engine's threading contract forbids live topology edits (include/RoutingGraph.h) - so "
			"change the EFFECT LIST (plugin.load / plugin.unload / chain.set_selector) and read the "
			"resulting route here"));
	return ControlResult::success(result);
}

} // namespace

void registerRoutingCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("routing.get_state");
		cmd.group = QStringLiteral("routing");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("The routing graph a target's signal is processed through: "
			"the nodes and their type names, the connections, the cached topological order the audio "
			"thread walks, the output node, and - for a mixer channel - its rack's graph. Read-only.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("target"), stringProperty()}},
			{QStringLiteral("target")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("kind"), stringProperty()},
			{QStringLiteral("target_type"), stringProperty()},
			{QStringLiteral("chain"), objectProperty()},
			{QStringLiteral("rack"), objectProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleRoutingGetState(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
