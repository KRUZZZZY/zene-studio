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
#include "PatchWiring.h"

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
	 * @brief The AUTHORED wiring of this chain's graph.
	 *
	 * Empty means "the derived wiring": input -> effect 0 -> ... -> effect n-1,
	 * which is what rebuildRoutingGraph() builds from the effect list and what
	 * a chain has until a patch is set (@see setPatchWiring). A non-empty
	 * wiring is re-applied by EVERY rebuild, which is what makes a hand-wired
	 * edge survive the next plugin.load rather than being discarded by it.
	 */
	auto patchWiring() const -> const PatchWiring& { return m_patch; }
	//! True when an authored wiring - not the derivation - wires this graph
	auto patchActive() const -> bool { return !m_patch.isEmpty(); }
	/**
	 * True when a rebuild could NOT apply an authored wiring (the effect list
	 * changed under it) and the chain fell back to the derived wiring. The
	 * patch is dropped rather than kept for a list it does not fit; the flag
	 * reports that this happened until the next rebuild.
	 */
	auto patchDropped() const -> bool { return m_patchDropped; }

	/**
	 * @brief The graph node id a patch reference names in the CURRENT graph,
	 *        or -1 when this chain has no such node.
	 *
	 * Control thread only. Refs are roles ("input", "effect:<index>"), which
	 * survive a rebuild; node ids do not, which is why a patch is stored as
	 * refs and resolved here.
	 */
	auto patchNodeId(const PatchRef& ref) const -> int;

	/**
	 * @brief Replaces this chain's wiring with @a wiring and re-renders through it.
	 *
	 * An empty @a wiring puts the chain back on its derived wiring. The new
	 * graph is built off the audio thread (every node's buffers are allocated
	 * there) and published under the audio engine's model-change guard, which
	 * is the seam every other topology edit in this tree takes
	 * (appendEffect/removeEffect/moveUp/moveDown/clear): the guard excludes a
	 * render period, so the edit is never concurrent with process() - the
	 * requirement RoutingGraph.h's threading contract states. It is NOT the
	 * lock-free pending-change plan swap of mixer/SPEC-dynamic-routing.md
	 * section 5.4, and it blocks the audio thread for the rebuild's duration.
	 *
	 * Refused, with @a error set and nothing written, when the chain does not
	 * render through a graph at all (no effects, an effect that routes its own
	 * audio ports, an unknown block size) or when the derivation cannot take
	 * the wiring; in both cases the previous wiring is rebuilt in place.
	 * Control thread only.
	 */
	auto setPatchWiring(const PatchWiring& wiring, QString* error) -> bool;

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

	//! Every effect in the chain, in processing order. Control thread only.
	//! Added for the agent control surface (plugin.* / dsp.get_state, SPEC
	//! A11-A14): m_effects is the true order, while QObject child order can
	//! diverge from it after moveUp()/moveDown(), so an id resolved by index
	//! must come from here.
	auto effects() const -> const std::vector<Effect*>& { return m_effects; }


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
	/*! The AUTHORED wiring, empty while the graph is wired by its derivation,
	 *  and re-applied by every rebuildRoutingGraph() (@see PatchWiring,
	 *  setPatchWiring). Control thread only. */
	PatchWiring m_patch;
	//! Set by rebuildRoutingGraph() when it could not apply m_patch
	bool m_patchDropped = false;

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
