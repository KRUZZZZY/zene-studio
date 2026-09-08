/*
 * RoutingGraph.cpp
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

#include "RoutingGraph.h"
#include "RoutingNodes.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <queue>
#include <utility>

#include <QDomDocument>
#include <QHash>

namespace lmms
{

namespace
{

constexpr auto GRAPH_ELEMENT = "routinggraph";
constexpr auto GRAPH_VERSION = 1;

} // namespace

auto RoutingGraph::addNode(std::unique_ptr<RoutingNode> node) -> int
{
	if (node == nullptr) { return -1; }

	int id = -1;
	for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
	{
		if (m_nodes[i] == nullptr)
		{
			id = i;
			break;
		}
	}
	if (id < 0)
	{
		id = static_cast<int>(m_nodes.size());
		m_nodes.emplace_back();
	}

	node->m_id = id;
	m_nodes[id] = std::move(node);

	if (isPrepared()) { m_nodes[id]->prepare(m_frames, m_channels); }
	rebuildPlan();
	return id;
}

auto RoutingGraph::removeNode(int id) -> bool
{
	if (node(id) == nullptr) { return false; }

	m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
		[id](const RoutingConnection& c) {
			return c.sourceNode == id || c.destNode == id;
		}), m_connections.end());

	m_nodes[id].reset();
	if (m_outputNodeId == id) { m_outputNodeId = -1; }
	rebuildPlan();
	return true;
}

auto RoutingGraph::connect(int sourceId, int destId, int sourcePort, int destPort, QString* error) -> bool
{
	const auto fail = [error](const QString& message) {
		if (error != nullptr) { *error = message; }
		return false;
	};

	RoutingNode* source = node(sourceId);
	RoutingNode* dest = node(destId);
	if (source == nullptr || dest == nullptr) { return fail(QStringLiteral("node does not exist")); }
	if (sourceId == destId) { return fail(QStringLiteral("a node cannot be connected to itself")); }
	if (sourcePort < 0 || sourcePort >= source->outputCount())
	{
		return fail(QStringLiteral("source port out of range"));
	}
	if (destPort < 0 || destPort >= dest->inputCount())
	{
		return fail(QStringLiteral("destination port out of range"));
	}

	for (const RoutingConnection& c : m_connections)
	{
		if (c.sourceNode == sourceId && c.sourcePort == sourcePort &&
			c.destNode == destId && c.destPort == destPort)
		{
			return fail(QStringLiteral("connection already exists"));
		}
	}

	m_connections.push_back(RoutingConnection{sourceId, sourcePort, destId, destPort});
	if (!rebuildPlan())
	{
		m_connections.pop_back();
		rebuildPlan();
		return fail(QStringLiteral("connection would create a cycle"));
	}
	return true;
}

auto RoutingGraph::disconnect(int sourceId, int destId, int sourcePort, int destPort) -> bool
{
	const auto it = std::find_if(m_connections.begin(), m_connections.end(),
		[=](const RoutingConnection& c) {
			return c.sourceNode == sourceId && c.sourcePort == sourcePort &&
				c.destNode == destId && c.destPort == destPort;
		});
	if (it == m_connections.end()) { return false; }

	m_connections.erase(it);
	rebuildPlan();
	return true;
}

void RoutingGraph::clear()
{
	m_nodes.clear();
	m_connections.clear();
	m_plan.clear();
	m_outputNodeId = -1;
}

auto RoutingGraph::nodeCount() const -> int
{
	return static_cast<int>(std::count_if(m_nodes.begin(), m_nodes.end(),
		[](const std::unique_ptr<RoutingNode>& node) { return node != nullptr; }));
}

auto RoutingGraph::node(int id) -> RoutingNode*
{
	if (id < 0 || id >= static_cast<int>(m_nodes.size())) { return nullptr; }
	return m_nodes[id].get();
}

auto RoutingGraph::node(int id) const -> const RoutingNode*
{
	if (id < 0 || id >= static_cast<int>(m_nodes.size())) { return nullptr; }
	return m_nodes[id].get();
}

auto RoutingGraph::setOutputNode(int id) -> bool
{
	if (node(id) == nullptr) { return false; }
	m_outputNodeId = id;
	return true;
}

void RoutingGraph::prepare(f_cnt_t frames, ch_cnt_t channels)
{
	assert(frames > 0);
	assert(channels > 0);

	m_frames = frames;
	m_channels = channels;
	for (const std::unique_ptr<RoutingNode>& node : m_nodes)
	{
		if (node != nullptr) { node->prepare(frames, channels); }
	}
	rebuildPlan();
}

void RoutingGraph::process(AudioBuffer& channelBuffer)
{
	if (!isPrepared()) { return; }

	for (const int id : m_plan)
	{
		RoutingNode* node = m_nodes[id].get();
		if (node != nullptr) { node->process(m_frames); }
	}

	const RoutingNode* output = node(m_outputNodeId);
	if (output == nullptr) { return; }

	const AudioBuffer& source = output->output(0);
	const f_cnt_t frames = std::min(m_frames, channelBuffer.frames());
	const ch_cnt_t channels = std::min(m_channels, channelBuffer.totalChannels());
	for (ch_cnt_t c = 0; c < channels; ++c)
	{
		float* dst = channelBuffer.buffer(c).data();
		const float* src = source.buffer(c).data();
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			dst[f] = src[f];
		}
	}
	for (ch_cnt_t c = 0; c < channelBuffer.totalChannels(); ++c)
	{
		channelBuffer.assumeNonSilent(c);
	}
}

void RoutingGraph::save(QDomElement& parent) const
{
	QDomDocument document = parent.ownerDocument();
	QDomElement graphElement = document.createElement(QString::fromLatin1(GRAPH_ELEMENT));
	graphElement.setAttribute(QStringLiteral("version"), GRAPH_VERSION);
	graphElement.setAttribute(QStringLiteral("frames"), static_cast<qulonglong>(m_frames));
	graphElement.setAttribute(QStringLiteral("channels"), static_cast<int>(m_channels));
	graphElement.setAttribute(QStringLiteral("output"), m_outputNodeId);

	for (int id = 0; id < static_cast<int>(m_nodes.size()); ++id)
	{
		const RoutingNode* node = m_nodes[id].get();
		if (node == nullptr) { continue; }

		QDomElement nodeElement = document.createElement(QStringLiteral("node"));
		nodeElement.setAttribute(QStringLiteral("id"), id);
		nodeElement.setAttribute(QStringLiteral("type"), node->typeName());
		node->saveSettings(nodeElement);
		graphElement.appendChild(nodeElement);
	}

	for (const RoutingConnection& c : m_connections)
	{
		QDomElement connectionElement = document.createElement(QStringLiteral("connection"));
		connectionElement.setAttribute(QStringLiteral("from"), c.sourceNode);
		connectionElement.setAttribute(QStringLiteral("fromport"), c.sourcePort);
		connectionElement.setAttribute(QStringLiteral("to"), c.destNode);
		connectionElement.setAttribute(QStringLiteral("toport"), c.destPort);
		graphElement.appendChild(connectionElement);
	}

	parent.appendChild(graphElement);
}

auto RoutingGraph::load(const QDomElement& parent) -> bool
{
	const QDomElement graphElement = parent.firstChildElement(QString::fromLatin1(GRAPH_ELEMENT));
	if (graphElement.isNull()) { return false; }

	RoutingGraph loaded;
	QHash<int, int> idMap;

	for (QDomElement nodeElement = graphElement.firstChildElement(QStringLiteral("node"));
		!nodeElement.isNull(); nodeElement = nodeElement.nextSiblingElement(QStringLiteral("node")))
	{
		std::unique_ptr<RoutingNode> node = createNode(nodeElement.attribute(QStringLiteral("type")));
		if (node == nullptr) { return false; }

		node->loadSettings(nodeElement);
		const int fileId = nodeElement.attribute(QStringLiteral("id")).toInt();
		idMap.insert(fileId, loaded.addNode(std::move(node)));
	}

	for (QDomElement connectionElement = graphElement.firstChildElement(QStringLiteral("connection"));
		!connectionElement.isNull();
		connectionElement = connectionElement.nextSiblingElement(QStringLiteral("connection")))
	{
		const int from = connectionElement.attribute(QStringLiteral("from")).toInt();
		const int to = connectionElement.attribute(QStringLiteral("to")).toInt();
		if (!idMap.contains(from) || !idMap.contains(to)) { return false; }

		if (!loaded.connect(idMap.value(from), idMap.value(to),
				connectionElement.attribute(QStringLiteral("fromport")).toInt(),
				connectionElement.attribute(QStringLiteral("toport")).toInt()))
		{
			return false;
		}
	}

	const int output = graphElement.attribute(QStringLiteral("output")).toInt();
	if (output >= 0)
	{
		if (!idMap.contains(output)) { return false; }
		loaded.setOutputNode(idMap.value(output));
	}

	*this = std::move(loaded);
	return true;
}

auto RoutingGraph::createNode(const QString& typeName) -> std::unique_ptr<RoutingNode>
{
	if (typeName == QLatin1String("constant")) { return std::make_unique<ConstantSourceNode>(); }
	if (typeName == QLatin1String("onepole_lowpass")) { return std::make_unique<OnePoleLowPassNode>(); }
	if (typeName == QLatin1String("gain")) { return std::make_unique<GainNode>(); }
	if (typeName == QLatin1String("sink")) { return std::make_unique<SinkNode>(); }
	return nullptr;
}

auto RoutingGraph::rebuildPlan() -> bool
{
	const int count = static_cast<int>(m_nodes.size());

	// Reset input wiring for every live node (control thread only)
	for (const std::unique_ptr<RoutingNode>& node : m_nodes)
	{
		if (node != nullptr) { node->m_inputs.assign(node->inputCount(), {}); }
	}

	std::vector<int> indegree(count, 0);
	std::vector<std::vector<int>> adjacency(count);

	for (const RoutingConnection& c : m_connections)
	{
		RoutingNode* source = node(c.sourceNode);
		RoutingNode* dest = node(c.destNode);
		if (source == nullptr || dest == nullptr) { continue; }

		adjacency[c.sourceNode].push_back(c.destNode);
		++indegree[c.destNode];

		if (source->isPrepared() && dest->isPrepared())
		{
			dest->m_inputs[c.destPort].push_back(&source->output(c.sourcePort));
		}
	}

	// Kahn's algorithm with ascending node ids for a deterministic order
	std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
	for (int i = 0; i < count; ++i)
	{
		if (m_nodes[i] != nullptr && indegree[i] == 0) { ready.push(i); }
	}

	std::vector<int> plan;
	plan.reserve(count);
	while (!ready.empty())
	{
		const int id = ready.top();
		ready.pop();
		plan.push_back(id);
		for (const int next : adjacency[id])
		{
			if (--indegree[next] == 0) { ready.push(next); }
		}
	}

	if (static_cast<int>(plan.size()) != nodeCount()) { return false; }

	m_plan = std::move(plan);
	return true;
}

} // namespace lmms
