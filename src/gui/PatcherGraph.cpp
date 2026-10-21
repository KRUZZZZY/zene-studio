/*
 * PatcherGraph.cpp - a chain's patcher graph as the canvas draws and edits it (Q050-087)
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "PatcherGraph.h"

namespace lmms::gui
{

namespace
{

PatcherEdge edgeFromJson(const QJsonObject& object)
{
	PatcherEdge edge;
	edge.from = object.value(QStringLiteral("from")).toString();
	edge.fromPort = object.value(QStringLiteral("from_port")).toInt();
	edge.to = object.value(QStringLiteral("to")).toString();
	edge.toPort = object.value(QStringLiteral("to_port")).toInt();
	return edge;
}

//! A node's port counts as the engine's graph reports them; a chain that does not route through its graph has no
//! graph nodes, and every device is then drawn with one input and one output.
void readPortCounts(const QJsonObject& state, QList<PatcherNode>* nodes)
{
	const QJsonArray graphNodes = state.value(QStringLiteral("graph")).toObject().value(QStringLiteral("nodes")).toArray();
	for (const QJsonValue& value : graphNodes)
	{
		const QJsonObject entry = value.toObject();
		for (PatcherNode& node : *nodes)
		{
			if (node.ref != entry.value(QStringLiteral("ref")).toString()) { continue; }
			node.inputs = qMax(1, entry.value(QStringLiteral("inputs")).toInt(1));
			node.outputs = qMax(1, entry.value(QStringLiteral("outputs")).toInt(1));
		}
	}
}

} // namespace

PatcherGraph PatcherGraph::fromState(const QJsonObject& state, const QStringList& deviceNames)
{
	PatcherGraph graph;
	PatcherNode input;
	input.ref = QStringLiteral("input");
	input.label = QObject::tr("Input");
	graph.nodes.append(input);
	for (int i = 0; i < state.value(QStringLiteral("effect_count")).toInt(); ++i)
	{
		PatcherNode node;
		node.ref = QStringLiteral("effect:%1").arg(i);
		node.label = i < deviceNames.size() ? deviceNames.at(i) : QObject::tr("Device %1").arg(i + 1);
		graph.nodes.append(node);
	}
	readPortCounts(state, &graph.nodes);

	const QJsonObject wiring = state.value(QStringLiteral("effective_wiring")).toObject();
	for (const QJsonValue& value : wiring.value(QStringLiteral("edges")).toArray()) { graph.edges.append(edgeFromJson(value.toObject())); }
	graph.output = wiring.value(QStringLiteral("output")).toString();

	const QJsonObject editability = state.value(QStringLiteral("editable")).toObject();
	graph.editable = editability.value(QStringLiteral("editable")).toBool();
	graph.reason = editability.value(QStringLiteral("reason")).toString();
	return graph;
}

int PatcherGraph::indexOfNode(const QString& ref) const
{
	for (int i = 0; i < nodes.size(); ++i)
	{
		if (nodes.at(i).ref == ref) { return i; }
	}
	return -1;
}

int PatcherGraph::indexOfEdge(const PatcherEdge& edge) const
{
	return edges.indexOf(edge);
}

QJsonArray PatcherGraph::edgesJson() const
{
	QJsonArray out;
	for (const PatcherEdge& edge : edges)
	{
		out.append(QJsonObject{{QStringLiteral("from"), edge.from}, {QStringLiteral("from_port"), edge.fromPort},
			{QStringLiteral("to"), edge.to}, {QStringLiteral("to_port"), edge.toPort}});
	}
	return out;
}

PatcherGraph PatcherGraph::connected(const PatcherEdge& edge) const
{
	PatcherGraph next = *this;
	const bool joinsItself = edge.from == edge.to;
	const bool intoInput = edge.to == QLatin1String("input");
	if (joinsItself || intoInput || indexOfEdge(edge) >= 0) { return next; }
	next.edges.append(edge);
	return next;
}

PatcherGraph PatcherGraph::disconnected(int index) const
{
	PatcherGraph next = *this;
	if (index >= 0 && index < next.edges.size()) { next.edges.removeAt(index); }
	return next;
}

PatcherGraph PatcherGraph::withOutput(const QString& ref) const
{
	PatcherGraph next = *this;
	next.output = ref;
	return next;
}

} // namespace lmms::gui
