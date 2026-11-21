/*
 * PatcherGraph.h - a chain's patcher graph as the canvas draws and edits it (Q050-087)
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_PATCHER_GRAPH_H
#define LMMS_PATCHER_GRAPH_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms::gui
{

//! One node of a chain's patcher graph: the chain's input, or the device at one slot of the chain.
struct PatcherNode
{
	QString ref;        //!< "input" or "effect:<index>" - the role patcher.set_wiring addresses it by
	QString label;
	int inputs = 1;
	int outputs = 1;
	bool isInput() const { return ref == QLatin1String("input"); }
};

//! One cable: \a from's output port into \a to's input port, both ends by role.
struct PatcherEdge
{
	QString from;
	int fromPort = 0;
	QString to;
	int toPort = 0;
	bool operator==(const PatcherEdge& other) const
	{
		return from == other.from && fromPort == other.fromPort && to == other.to && toPort == other.toPort;
	}
};

/*! What patcher.get_state reports, in the form the canvas needs: the nodes in chain order, the cables that are in
 *  force (the derived linear wiring or the authored one - `effective_wiring`), the node the chain leaves through, and
 *  whether the engine can route an edit at all. The edit helpers return the whole new wiring, which is what
 *  patcher.set_wiring takes; nothing here changes the chain. */
class LMMS_EXPORT PatcherGraph
{
public:
	//! \a state is patcher.get_state's result; \a deviceNames names effect 0, 1, ... in chain order.
	static PatcherGraph fromState(const QJsonObject& state, const QStringList& deviceNames);

	QList<PatcherNode> nodes;
	QList<PatcherEdge> edges;
	QString output;
	bool editable = false;
	QString reason;  //!< why an edit cannot land, when editable is false

	int indexOfNode(const QString& ref) const;
	int indexOfEdge(const PatcherEdge& edge) const;
	//! The cables as patcher.set_wiring's `edges`.
	QJsonArray edgesJson() const;

	//! The wiring with \a edge added; unchanged when it is already there or would join a node to itself or the input.
	PatcherGraph connected(const PatcherEdge& edge) const;
	//! The wiring without cable \a index.
	PatcherGraph disconnected(int index) const;
	//! The wiring with \a ref as the node the chain leaves through.
	PatcherGraph withOutput(const QString& ref) const;
};

} // namespace lmms::gui

#endif // LMMS_PATCHER_GRAPH_H
