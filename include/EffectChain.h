/*
 * EffectChain.h - class for processing and effects chain
 *
 * Copyright (c) 2006-2008 Danny McRae <khjklujn/at/users.sourceforge.net>
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_EFFECT_CHAIN_H
#define LMMS_EFFECT_CHAIN_H

#include "Model.h"
#include "SerializingObject.h"
#include "AutomatableModel.h"

#include <atomic>
#include <memory>
#include <vector>

namespace lmms
{

class AudioBuffer;
class AudioBus;
class Effect;
class EffectNode;
class RoutingGraph;

namespace gui
{

class EffectRackView;

} // namespace gui


class LMMS_EXPORT EffectChain : public Model, public SerializingObject
{
	Q_OBJECT
public:
	EffectChain( Model * _parent );
	~EffectChain() override;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;

	inline QString nodeName() const override
	{
		return "fxchain";
	}

	void appendEffect( Effect * _effect );
	void removeEffect( Effect * _effect );
	void moveDown( Effect * _effect );
	void moveUp( Effect * _effect );
	bool processAudioBuffer(AudioBuffer& buffer,
		const AudioBuffer* sidechainBuffer = nullptr);
	//! Processes the effects chain on a multi-channel audio bus
	bool processAudioBuffer(AudioBus& bus,
		const AudioBuffer* sidechainBuffer = nullptr);

	//! Sidechain input for the current processing block, or nullptr when the
	//! owning mixer channel has no incoming sidechain sends (Phase D). Effects
	//! query this through Effect::sidechainBuffer().
	auto sidechainBuffer() const -> const AudioBuffer* { return m_sidechainBuffer; }

	//! Frames of latency this chain adds to the signal path: the sum over the
	//! effects that will actually process audio (enabled, okay, not bypassed),
	//! or zero when the chain itself is disabled. Cached; refreshed by
	//! refreshLatency(). Read by the mixer's PDC graph on the audio thread
	//! (#605), which is why it is an atomic load and never a virtual call.
	auto latencyFrames() const -> int
	{
		return m_latencyFrames.load(std::memory_order_relaxed);
	}

	//! Control thread: recompute the cached latency after an effect was added,
	//! removed, bypassed or reported a new latency (#605).
	void refreshLatency();

	/**
	 * @brief The routing graph this chain's signal is processed through.
	 *
	 * The graph mirrors the effect list: a ChainInputNode sourced from the
	 * host's block, then one EffectNode per effect, wired in chain order
	 * (input -> effect 0 -> ... -> effect n-1). Re-wiring the graph is how a
	 * chain is routed differently than its list order; the wiring stays in
	 * place until the effect list itself changes, at which point
	 * rebuildRoutingGraph() wires it linearly again.
	 *
	 * Control thread only, and not while processAudioBuffer() is running: the
	 * graph's topology edits are not safe against the audio thread yet (see
	 * RoutingGraph.h's threading contract and PATCHER-MVP.md).
	 */
	auto routingGraph() -> RoutingGraph&;
	auto routingGraph() const -> const RoutingGraph&;

	//! True when processAudioBuffer() renders this chain through the graph
	//! rather than the plain effect loop. @see rebuildRoutingGraph
	auto routesThroughGraph() const -> bool { return m_graphActive; }

	/**
	 * @brief Rebuilds the graph from the current effect list and prepares it.
	 *
	 * Call this after any change to the effect list or its order. The graph is
	 * left inactive (and processAudioBuffer() keeps using the plain loop) when
	 * there is nothing to route, when the audio engine's block size is not
	 * known yet, or when an effect routes its own audio ports - the graph's
	 * planar blocks cannot represent an AudioPlugin's port map, so such a chain
	 * keeps the pre-existing path.
	 *
	 * Control thread only.
	 */
	void rebuildRoutingGraph();

	void clear();


private:
	using EffectList = std::vector<Effect*>;
	EffectList m_effects;

	//! The graph the chain is rendered through, and the buffers that carry the
	//! block in and out of it. Built by rebuildRoutingGraph() on the control
	//! thread; processAudioBuffer() only reads them.
	std::unique_ptr<RoutingGraph> m_graph;
	//! Planar mirror of the incoming bus block (the graph's ChainInputNode source)
	std::unique_ptr<AudioBuffer> m_graphInput;
	//! The block the graph renders into (RoutingGraph::process copies it out)
	std::unique_ptr<AudioBuffer> m_graphOutput;
	//! The effect nodes in chain order; owned by m_graph
	std::vector<EffectNode*> m_effectNodes;
	//! Set by rebuildRoutingGraph() when the graph mirrors this chain
	bool m_graphActive = false;

	//! Audio thread: renders one bus block through the graph. Allocates nothing.
	auto processThroughGraph(AudioBus& bus) -> bool;
	//! Audio thread: whether this block can be rendered through the graph
	auto canProcessThroughGraph(const AudioBus& bus) const -> bool;
	//! Audio thread: whether the graph still mirrors m_effects, in order
	auto graphMirrorsEffectList() const -> bool;

	BoolModel m_enabledModel;

	//! Non-owning; set by processAudioBuffer() for the duration of the call
	const AudioBuffer* m_sidechainBuffer = nullptr;

	//! Cached chain latency for the PDC graph (#605); see latencyFrames().
	std::atomic<int> m_latencyFrames{0};

	//! One-shot guard for the "chain latency exceeds the delay-line capacity"
	//! diagnostic; only touched on the control thread.
	bool m_latencyClampWarned = false;


	friend class gui::EffectRackView;


signals:
	void aboutToClear();

} ;

} // namespace lmms

#endif // LMMS_EFFECT_CHAIN_H
