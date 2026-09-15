/*
 * ControlCommandsPatcherEdit.cpp - the `patcher.*` group's EDIT HALF:
 *                                   patcher.set_wiring, the verb feature row 69
 *                                   asked for ("make an EDIT possible").
 *
 * The group is two translation units because Gate 7 measures A FILE (500 lines,
 * zero tolerance); the read half, the design decision and the evidence for it
 * (the model-change guard as the seam that makes a topology edit non-concurrent
 * with process(), and the authored wiring the DERIVED rebuild re-applies) are in
 * src/core/ControlCommandsPatcher.cpp. This half is the writing verb and
 * everything a wiring can get wrong.
 *
 * THE ORDER OF OPERATIONS, which is the part worth reading: a wiring is
 * validated against the CURRENT node set before the chain is touched
 * (validateWiring), so every refusal writes nothing; the write then goes through
 * EffectChain::setPatchWiring(), which builds the new graph off the audio thread
 * and publishes it under AudioEngine::requestChangeInModel(), and rebuilds the
 * previous wiring in place when the derivation will not take the new one. The
 * audio thread therefore never sees a half-wired graph and never runs the
 * rebuild itself.
 *
 * A16: `snapshot`, whose inverse IS a command - see the transaction the handler
 * records, and the row in src/core/ControlReversibilityTableRouting.cpp. A chain
 * that was on its DERIVED wiring comes back to it AS the derivation (an empty
 * edge list) rather than as a linear-looking authored patch.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <utility>
#include <vector>

#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"

#include "ControlCommandsPatcherShared.h"
#include "EffectChain.h"
#include "PatchWiring.h"
#include "RoutingGraph.h"
#include "RoutingNode.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

constexpr auto DERIVED_TEXT = "derived";
constexpr auto AUTHORED_TEXT = "authored";

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

//! True when this chain HAS every node a wiring names - the effect list is the
//! node set, so an index past its end is a reference to a node that is gone.
auto wiringRefsExist(const EffectChain& chain, const PatchWiring& wiring, QString* reason) -> bool
{
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
	return true;
}

//! True when a wiring names the node the host block leaves the graph through.
//! An unset output is refused by name: it is the state a wiring parsed from an
//! `edges` array without an `output` is in, and the alternative - defaulting to
//! effect 0 - would route through a node the caller never named.
auto wiringOutputExists(const EffectChain& chain, const PatchWiring& wiring, QString* reason) -> bool
{
	if (!wiring.output().isSet())
	{
		*reason = QStringLiteral("the wiring names no output node: `output` is the node the host block "
			"leaves the graph through (%1)").arg(PatchRef::effect(0).toString());
		return false;
	}
	if (wiring.output().isInput()) { return true; }

	const int effectCount = static_cast<int>(chain.effects().size());
	if (wiring.output().index() >= 0 && wiring.output().index() < effectCount) { return true; }

	*reason = QStringLiteral("the output node %1 names no node of this chain: it has %2 effect(s)")
		.arg(wiring.output().toString()).arg(effectCount);
	return false;
}

} // namespace

namespace control
{

bool validateWiring(const EffectChain& chain, const PatchWiring& wiring, QString* reason)
{
	if (wiring.isEmpty()) { return true; }  // "the derived wiring" is always valid
	if (!wiringRefsExist(chain, wiring, reason)) { return false; }
	if (!wiringOutputExists(chain, wiring, reason)) { return false; }

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

bool wiringFromArgs(const QJsonObject& args, const EffectChain& chain, PatchWiring* wanted,
	ControlResult* error)
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

} // namespace control

namespace
{

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
	if (!control::wiringFromArgs(args, chain, &wanted, &error)) { return error; }

	QString reason;
	if (!control::validateWiring(chain, wanted, &reason))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, reason);
	}

	const bool wasAuthored = chain.patchActive();
	const PatchWiring before = control::effectiveWiring(chain);

	if (!chain.setPatchWiring(wanted, &reason))
	{
		return ControlResult::failure(ControlErrorKind::Refused, reason);
	}

	QJsonObject result = control::patcherStateJson(chain, target);
	result.insert(QStringLiteral("previous"), control::wiringJson(before));
	result.insert(QStringLiteral("changed"),
		!before.equals(control::effectiveWiring(chain)));

	// SPEC A16: an EffectChain is a Model and a SerializingObject, not a
	// JournallingObject, so there is no live checkpoint to take. The inverse is
	// the SAME command with the wiring captured before the write, and a chain
	// that WAS on its derived wiring comes back to it AS the derivation (an
	// empty edge list) rather than as a linear-looking authored patch.
	QJsonObject beforeState;
	beforeState.insert(QStringLiteral("wiring"),
		wasAuthored ? QString::fromLatin1(AUTHORED_TEXT) : QString::fromLatin1(DERIVED_TEXT));
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

void registerPatcherEditCommands(ControlRegistry& registry)
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

} // namespace lmms
