/*
 * RoutingNode.h
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

#ifndef LMMS_ROUTING_NODE_H
#define LMMS_ROUTING_NODE_H

#include <vector>

#include <QDomElement>
#include <QString>

#include "AudioBuffer.h"
#include "LmmsTypes.h"
#include "lmms_constants.h"
#include "lmms_export.h"

namespace lmms
{

/**
 * @brief Base class for a single node in a RoutingGraph.
 *
 * A node owns one output AudioBuffer per output port (the MVP uses exactly
 * one) and holds non-owning pointers to the output buffers of the upstream
 * nodes connected to each of its input ports. Output buffers are allocated by
 * prepare() on the control thread. process() is the audio-thread entry point
 * and must not allocate or lock; the input wiring is (re)built by
 * RoutingGraph whenever the cached execution plan is rebuilt.
 */
class LMMS_EXPORT RoutingNode
{
public:
	RoutingNode() = default;
	virtual ~RoutingNode() = default;

	RoutingNode(const RoutingNode&) = delete;
	auto operator=(const RoutingNode&) -> RoutingNode& = delete;

	//! Stable type id, used by RoutingGraph::createNode for serialization
	virtual auto typeName() const -> QString = 0;

	virtual auto inputCount() const -> int { return 1; }
	virtual auto outputCount() const -> int { return 1; }

	/**
	 * Allocates the output buffers for @a frames frames and @a channels
	 * channels and resets per-channel state. Control thread only. May be
	 * called again when the host block size changes; previous buffers are
	 * released first, so any state that must survive a block size change
	 * needs to be saved and restored by the caller.
	 */
	virtual void prepare(f_cnt_t frames, ch_cnt_t channels);

	auto isPrepared() const -> bool { return m_frames > 0; }
	auto frames() const -> f_cnt_t { return m_frames; }
	auto channels() const -> ch_cnt_t { return m_channels; }

	auto output(int port = 0) -> AudioBuffer&;
	auto output(int port = 0) const -> const AudioBuffer&;

	//! Output buffers of the upstream nodes connected to input @a port
	auto inputs(int port) const -> const std::vector<const AudioBuffer*>&;

	//! Audio-thread processing. Must not allocate or lock.
	virtual void process(f_cnt_t frames) = 0;

	//! Serializes node parameters as child elements of @a element
	virtual void saveSettings(QDomElement& element) const { Q_UNUSED(element) }
	//! Restores node parameters written by saveSettings()
	virtual void loadSettings(const QDomElement& element) { Q_UNUSED(element) }

protected:
	//! Zeroes every output buffer (audio thread; no allocation)
	void zeroOutputs();
	//! Marks all output channels non-silent after writing to them
	void markOutputsNonSilent();
	//! Adds every buffer connected to @a port into @a dest
	void sumInputs(int port, f_cnt_t frames, AudioBuffer& dest) const;

private:
	std::vector<AudioBuffer> m_outputs;
	std::vector<std::vector<const AudioBuffer*>> m_inputs;
	f_cnt_t m_frames = 0;
	ch_cnt_t m_channels = 0;
	int m_id = -1;

	friend class RoutingGraph;
};

} // namespace lmms

#endif // LMMS_ROUTING_NODE_H
