/*
 * RoutingGraph.h
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

#ifndef LMMS_ROUTING_GRAPH_H
#define LMMS_ROUTING_GRAPH_H

#include <memory>
#include <vector>

#include <QDomElement>
#include <QString>

#include "RoutingNode.h"
#include "lmms_export.h"

namespace lmms
{

//! A connection from a node's output port to another node's input port
struct RoutingConnection
{
	int sourceNode = -1;
	int sourcePort = 0;
	int destNode = -1;
	int destPort = 0;
};

/**
 * @brief A directed acyclic graph of RoutingNodes with a cached execution plan.
 *
 * Node ids are indices into the graph's node table and stay stable until the
 * node is removed. The processing order is topologically sorted by Kahn's
 * algorithm and cached; it is only recomputed when the topology changes, never
 * from the audio thread. connect() rejects any edge that would create a cycle,
 * so the cached plan is always valid.
 *
 * Threading contract (MVP): topology edits and prepare() are control-thread
 * operations and must not run concurrently with process(). process() itself
 * walks the cached plan and allocates/locks nothing. Making live edits safe
 * during playback requires the atomic plan swap described in the full design
 * (see PATCHER-MVP.md); this spike deliberately does not implement it.
 */
class LMMS_EXPORT RoutingGraph
{
public:
	RoutingGraph() = default;
	~RoutingGraph() = default;

	RoutingGraph(const RoutingGraph&) = delete;
	auto operator=(const RoutingGraph&) -> RoutingGraph& = delete;
	RoutingGraph(RoutingGraph&&) noexcept = default;
	auto operator=(RoutingGraph&&) noexcept -> RoutingGraph& = default;

	// --- topology (control thread only) ---
	//! Adds a node and returns its id, or -1 if @a node is null
	auto addNode(std::unique_ptr<RoutingNode> node) -> int;
	//! Removes a node and every connection touching it
	auto removeNode(int id) -> bool;
	/**
	 * Connects @a sourceId's output port to @a destId's input port.
	 * @returns false and sets @a error (if given) when a node or port does not
	 * exist or when the connection would introduce a cycle.
	 */
	auto connect(int sourceId, int destId, int sourcePort = 0, int destPort = 0,
		QString* error = nullptr) -> bool;
	auto disconnect(int sourceId, int destId, int sourcePort = 0, int destPort = 0) -> bool;
	void clear();

	auto nodeCount() const -> int;
	auto node(int id) -> RoutingNode*;
	auto node(int id) const -> const RoutingNode*;
	auto connections() const -> const std::vector<RoutingConnection>& { return m_connections; }

	//! Cached, topologically sorted node ids (rebuilt on every topology edit)
	auto processingOrder() const -> const std::vector<int>& { return m_plan; }

	//! The node whose first output is copied into the host channel buffer
	auto setOutputNode(int id) -> bool;
	auto outputNodeId() const -> int { return m_outputNodeId; }

	// --- audio: control thread prepares, audio thread processes ---
	//! Allocates every node's buffers for the given block size
	void prepare(f_cnt_t frames, ch_cnt_t channels = DEFAULT_CHANNELS);
	auto isPrepared() const -> bool { return m_frames > 0; }
	auto frames() const -> f_cnt_t { return m_frames; }
	auto channels() const -> ch_cnt_t { return m_channels; }

	/**
	 * Audio-thread entry point: walks the cached plan, then copies the output
	 * node's first output into @a channelBuffer. Never allocates or locks.
	 */
	void process(AudioBuffer& channelBuffer);

	// --- serialization ---
	//! Appends a <routinggraph> element to @a parent
	void save(QDomElement& parent) const;
	/**
	 * Replaces this graph with the first <routinggraph> child of @a parent.
	 * @returns false (leaving the graph unchanged) on malformed or unknown
	 * content. The loaded graph is unprepared; call prepare() before process().
	 */
	auto load(const QDomElement& parent) -> bool;

	//! Instantiates a node by serialized type name, or nullptr if unknown
	static auto createNode(const QString& typeName) -> std::unique_ptr<RoutingNode>;

private:
	//! Rebuilds the cached plan and input wiring; false if the graph has a cycle
	auto rebuildPlan() -> bool;

	std::vector<std::unique_ptr<RoutingNode>> m_nodes; //!< indexed by node id
	std::vector<RoutingConnection> m_connections;
	std::vector<int> m_plan;
	int m_outputNodeId = -1;
	f_cnt_t m_frames = 0;
	ch_cnt_t m_channels = 0;
};

} // namespace lmms

#endif // LMMS_ROUTING_GRAPH_H
