/*
 * EffectChain.cpp - class for processing and effects chain
 *
 * Copyright (c) 2006-2008 Danny McRae <khjklujn/at/users.sourceforge.net>
 * Copyright (c) 2008-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "EffectChain.h"

#include <QDebug>
#include <QDomElement>
#include <algorithm>
#include <cassert>

#include "AudioBus.h"
#include "AudioBuffer.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "DummyEffect.h"
#include "Engine.h"
#include "LatencyCompensation.h"
#include "PatchWiring.h"
#include "RoutingChainNodes.h"
#include "RoutingGraph.h"

namespace lmms
{

namespace
{

//! The graph node id a patch reference names, through the derived node table
//! ids[0] = the chain's input node, ids[k] = the node of effect k-1, or -1.
auto nodeIdFor(const PatchRef& ref, const std::vector<int>& ids) -> int
{
	if (ref.isInput()) { return ids.empty() ? -1 : ids.front(); }

	const std::size_t index = static_cast<std::size_t>(ref.index()) + 1;
	if (ref.index() < 0 || index >= ids.size()) { return -1; }
	return ids[index];
}

/*! Connects @a wiring on @a graph through @a ids and sets its output node.
 *
 *  @returns false on the first edge the graph refuses (a port out of range, a
 *  repeated edge, a cycle) or when a reference names no node in @a ids. A
 *  false leaves whatever connected cleanly on the graph, which is why every
 *  caller treats it as "no wiring" and rebuilds from the derived one rather
 *  than keeping a half-wired graph; connect() itself rolls back the single
 *  edge it refused.
 */
auto wireGraph(RoutingGraph& graph, const PatchWiring& wiring, const std::vector<int>& ids) -> bool
{
	for (const PatchEdge& edge : wiring.edges())
	{
		const int from = nodeIdFor(edge.from, ids);
		const int to = nodeIdFor(edge.to, ids);
		if (from < 0 || to < 0) { return false; }
		if (!graph.connect(from, to, edge.fromPort, edge.toPort)) { return false; }
	}

	const int output = nodeIdFor(wiring.output(), ids);
	if (output < 0) { return false; }
	return graph.setOutputNode(output);
}

} // namespace


EffectChain::EffectChain( Model * _parent ) :
	Model( _parent ),
	SerializingObject(),
	m_graph( std::make_unique<RoutingGraph>() ),
	m_enabledModel( false, nullptr, tr( "Effects enabled" ) )
{
	// Disabling the chain takes every effect out of the signal path, so the
	// cached PDC latency must follow (#605).
	connect(&m_enabledModel, &BoolModel::dataChanged, [this] { refreshLatency(); });
}


auto EffectChain::routingGraph() -> RoutingGraph&
{
	return *m_graph;
}


auto EffectChain::routingGraph() const -> const RoutingGraph&
{
	return *m_graph;
}


void EffectChain::rebuildRoutingGraph()
{
	m_graph->clear();
	m_effectNodes.clear();
	m_graphInput.reset();
	m_graphOutput.reset();
	m_graphActive = false;
	m_patchDropped = false;

	auto* engine = Engine::audioEngine();
	const f_cnt_t frames = engine != nullptr ? engine->framesPerPeriod() : 0;
	if (frames == 0 || m_effects.empty()) { return; }

	// Effects with audio ports route their own ports on the bus (AudioPlugin
	// overrides the bus entry point for exactly that); the graph's planar
	// blocks cannot carry that port map, so the whole chain keeps the
	// pre-existing path rather than routing half of it.
	for (const Effect* effect : m_effects)
	{
		if (effect->audioPortsModel() != nullptr) { return; }
	}

	m_graphInput = std::make_unique<AudioBuffer>(frames, DEFAULT_CHANNELS);
	m_graphOutput = std::make_unique<AudioBuffer>(frames, DEFAULT_CHANNELS);
	m_graphInput->silenceAllChannels();
	m_graphOutput->silenceAllChannels();

	auto input = std::make_unique<ChainInputNode>(m_graphInput.get());
	const int inputId = m_graph->addNode(std::move(input));

	// The node set is DERIVED - one source node plus one node per effect, in
	// list order - and these ids index it: ids[0] is the chain's input node,
	// ids[k] is the node of effect k-1. A patch addresses those roles rather
	// than these ids, because the ids are rebuilt here on every topology change.
	std::vector<int> ids;
	ids.reserve(m_effects.size() + 1);
	ids.push_back(inputId);
	for (Effect* effect : m_effects)
	{
		auto node = std::make_unique<EffectNode>(effect);
		EffectNode* const raw = node.get();
		ids.push_back(m_graph->addNode(std::move(node)));
		m_effectNodes.push_back(raw);
	}

	// The wiring is the DERIVED one (linear, in chain order) until a patch
	// replaces it, and the authored wiring is applied here on every rebuild -
	// which is what makes a hand-wired edge survive the next plugin.load.
	const PatchWiring derived = PatchWiring::linear(static_cast<int>(m_effects.size()));
	if (m_patch.isEmpty() ? !wireGraph(*m_graph, derived, ids) : !wireGraph(*m_graph, m_patch, ids))
	{
		// A patch the current effect list cannot take is DROPPED rather than
		// kept for a list it does not fit, and the chain falls back to its
		// derivation - the same wiring it had before the patch existed.
		m_patchDropped = !m_patch.isEmpty();
		m_patch.clear();
		if (!wireGraph(*m_graph, derived, ids))
		{
			// Unreachable for a linear chain (no cycle is possible); leave the
			// chain on the plain loop rather than on a half-wired graph.
			m_graph->clear();
			m_effectNodes.clear();
			m_graphInput.reset();
			m_graphOutput.reset();
			return;
		}
	}

	m_graph->prepare(frames, DEFAULT_CHANNELS);
	m_graphActive = true;
}


auto EffectChain::patchNodeId(const PatchRef& ref) const -> int
{
	if (m_graph == nullptr) { return -1; }

	// The chain's graph is always rebuilt from empty (no node is ever removed
	// from it), so its ids are dense: 0 .. nodeCount()-1.
	for (int id = 0; id < m_graph->nodeCount(); ++id)
	{
		const RoutingNode* node = m_graph->node(id);
		if (node == nullptr) { continue; }

		if (ref.isInput())
		{
			if (dynamic_cast<const ChainInputNode*>(node) != nullptr) { return id; }
			continue;
		}
		if (ref.index() < 0 || ref.index() >= static_cast<int>(m_effects.size())) { return -1; }

		const auto* effectNode = dynamic_cast<const EffectNode*>(node);
		if (effectNode != nullptr && effectNode->effect() == m_effects[ref.index()]) { return id; }
	}
	return -1;
}


auto EffectChain::setPatchWiring(const PatchWiring& wiring, QString* error) -> bool
{
	auto* engine = Engine::audioEngine();

	if (wiring.isEmpty())
	{
		// "No patch" is the derived wiring, and a chain can always be put back
		// on it - there is nothing to refuse and nothing to validate.
		if (engine != nullptr) { engine->requestChangeInModel(); }
		m_patch.clear();
		rebuildRoutingGraph();
		if (engine != nullptr) { engine->doneChangeInModel(); }
		return true;
	}

	// The new wiring is built off the audio thread (rebuildRoutingGraph()
	// allocates every node's buffers) and published under the model-change
	// guard the audio thread takes for a whole render period, so the edit is
	// never concurrent with process() - RoutingGraph.h's threading contract.
	const PatchWiring previous = m_patch;
	if (engine != nullptr) { engine->requestChangeInModel(); }
	m_patch = wiring;
	rebuildRoutingGraph();
	// applied == the chain renders through its graph AND the graph took the
	// wiring. Either half can fail: a chain with nothing routable has no graph
	// at all, and a wiring the current effect list cannot take is dropped by
	// rebuildRoutingGraph().
	const bool applied = m_graphActive && !m_patchDropped;
	const bool hasGraph = m_graphActive;
	if (!applied)
	{
		// Nothing is written: the previous wiring is rebuilt in place, so a
		// refusal leaves the chain in exactly the state it was in.
		m_patch = previous;
		rebuildRoutingGraph();
	}
	if (engine != nullptr) { engine->doneChangeInModel(); }

	if (!applied)
	{
		if (error != nullptr)
		{
			*error = hasGraph
				? QStringLiteral("the wiring could not be applied to this chain's graph - a reference or "
					"a port the graph refuses - so the chain is back on its derived wiring")
				: QStringLiteral("this chain does not render through its routing graph, so it has no "
					"wiring to replace: a graph is built for a chain of effects whose devices route "
					"their own audio ports, and its graph is empty");
		}
		return false;
	}
	return true;
}


void EffectChain::refreshLatency()
{
	int frames = 0;
	if (m_enabledModel.value())
	{
		for (const Effect* effect : m_effects)
		{
			if (effect->isEnabled() && effect->isOkay() && !effect->dontRun())
			{
				frames += std::max(0, effect->latencyFrames());
			}
		}
	}
	// The delay lines that align this chain at a summing point cannot apply
	// more than LatencyCompensation::MaxFrames (#605). Report a chain that
	// exceeds that bound once, on the control thread: the mixer then clamps
	// the alignment it publishes, and compensation against this chain may be
	// incomplete. This is the only place the diagnostic can live -- the audio
	// thread may not call qWarning (it locks and may allocate).
	if (frames > LatencyCompensation::MaxFrames && !m_latencyClampWarned)
	{
		m_latencyClampWarned = true;
		qWarning("EffectChain: PDC delay-line capacity exceeded: this chain "
			"reports %d frames of latency, the delay lines can apply at most "
			"%d; delays above the capacity are clamped, so compensation "
			"against this chain may be incomplete.",
			frames, LatencyCompensation::MaxFrames);
	}
	m_latencyFrames.store(frames, std::memory_order_relaxed);
}




EffectChain::~EffectChain()
{
	clear();
}




void EffectChain::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	m_enabledModel.saveSettings( _doc, _this, "enabled" );
	_this.setAttribute("numofeffects", static_cast<int>(m_effects.size()));

	for( Effect* effect : m_effects)
	{
		if (auto dummy = dynamic_cast<DummyEffect*>(effect)) { _this.appendChild(dummy->originalPluginData()); }
		else
		{
			QDomElement ef = effect->saveState( _doc, _this );
			ef.setAttribute( "name", QString::fromUtf8( effect->descriptor()->name ) );
			ef.appendChild( effect->key().saveXML( _doc ) );
		}
	}
}




void EffectChain::loadSettings( const QDomElement & _this )
{
	clear();

	// TODO This method should probably also lock the audio engine

	m_enabledModel.loadSettings( _this, "enabled" );

	const int plugin_cnt = _this.attribute( "numofeffects" ).toInt();

	QDomNode node = _this.firstChild();
	int fx_loaded = 0;
	while( !node.isNull() && fx_loaded < plugin_cnt )
	{
		if( node.isElement() && node.nodeName() == "effect" )
		{
			QDomElement effectData = node.toElement();

			const QString name = effectData.attribute( "name" );
			EffectKey key( effectData.elementsByTagName( "key" ).item( 0 ).toElement() );

			Effect* e = Effect::instantiate( name.toUtf8(), this, &key );

			if( e != nullptr && e->isOkay() && e->nodeName() == node.nodeName() )
			{
				e->restoreState( effectData );
			}
			else
			{
				delete e;
				e = new DummyEffect( parentModel(), effectData );
			}

			m_effects.push_back( e );
			++fx_loaded;
		}
		node = node.nextSibling();
	}

	refreshLatency();
	// The restored effects are this chain's signal path too.
	rebuildRoutingGraph();
	emit dataChanged();
}




void EffectChain::appendEffect( Effect * _effect )
{
	Engine::audioEngine()->requestChangeInModel();
	// Phase D: the chain is the effect's parent for sidechain delivery. The
	// factory path sets this in Effect::instantiate(), but effects built
	// directly (tests, native plugins) only pass through here.
	_effect->setEffectChain( this );
	m_effects.push_back(_effect);
	// The chain's signal path is defined by its effect list, so the routing
	// graph is rebuilt inside the model change: the audio thread must never see
	// a graph that does not mirror the list it is rendering.
	rebuildRoutingGraph();
	Engine::audioEngine()->doneChangeInModel();

	m_enabledModel.setValue( true );

	refreshLatency();
	emit dataChanged();
}




void EffectChain::removeEffect( Effect * _effect )
{
	Engine::audioEngine()->requestChangeInModel();

	auto found = std::find(m_effects.begin(), m_effects.end(), _effect);
	if( found == m_effects.end() )
	{
		Engine::audioEngine()->doneChangeInModel();
		return;
	}
	m_effects.erase( found );

	rebuildRoutingGraph();

	Engine::audioEngine()->doneChangeInModel();

	if (m_effects.empty())
	{
		m_enabledModel.setValue( false );
	}

	refreshLatency();
	emit dataChanged();
}




void EffectChain::moveDown( Effect * _effect )
{
	if (_effect != m_effects.back())
	{
		// Reordering the list reorders the signal path, so it needs the same
		// model change every other topology edit takes: without it the audio
		// thread could swap the list (and the graph below) out from under the
		// plain loop's range-for.
		//
		// D5 (mixer concurrency audit): the swap changes the order of the
		// vector a worker range-fors in processAudioBuffer(). Same idiom as
		// appendEffect()/removeEffect()/clear() above: hold the change mutex
		// so the reorder can only land between render periods.
		Engine::audioEngine()->requestChangeInModel();
		auto it = std::find(m_effects.begin(), m_effects.end(), _effect);
		assert(it != m_effects.end());
		std::swap(*std::next(it), *it);
		rebuildRoutingGraph();
		Engine::audioEngine()->doneChangeInModel();
	}
}




void EffectChain::moveUp( Effect * _effect )
{
	if (_effect != m_effects.front())
	{
		// D5: see moveDown() above. The graph is rebuilt inside the same model
		// change, so the audio thread never sees an effect list the graph does
		// not mirror.
		Engine::audioEngine()->requestChangeInModel();
		auto it = std::find(m_effects.begin(), m_effects.end(), _effect);
		assert(it != m_effects.end());
		std::swap(*std::prev(it), *it);
		rebuildRoutingGraph();
		Engine::audioEngine()->doneChangeInModel();
	}
}




bool EffectChain::processAudioBuffer(AudioBuffer& buffer, const AudioBuffer* sidechainBuffer)
{
	if( m_enabledModel.value() == false )
	{
		return false;
	}

	// Publish the sidechain input for the duration of this block so effects
	// can query it via Effect::sidechainBuffer() without changing the
	// processImpl() signature (Phase D, spec 4.2).
	const AudioBuffer* const previousSidechain = m_sidechainBuffer;
	m_sidechainBuffer = sidechainBuffer;

	bool moreEffects = false;
	for (Effect* effect : m_effects)
	{
		moreEffects |= effect->processAudioBuffer(buffer);
	}

	m_sidechainBuffer = previousSidechain;

	return moreEffects;
}




bool EffectChain::processAudioBuffer(AudioBus& bus, const AudioBuffer* sidechainBuffer)
{
	if( m_enabledModel.value() == false )
	{
		return false;
	}

	// See the AudioBuffer overload above.
	const AudioBuffer* const previousSidechain = m_sidechainBuffer;
	m_sidechainBuffer = sidechainBuffer;

	bool moreEffects = false;
	if (canProcessThroughGraph(bus))
	{
		// The same effects, in the same order, on the same block - driven by
		// the graph's cached plan instead of a range-for over m_effects.
		moreEffects = processThroughGraph(bus);
	}
	else
	{
		for (Effect* effect : m_effects)
		{
			moreEffects |= effect->processAudioBuffer(bus);
		}
	}

	m_sidechainBuffer = previousSidechain;

	return moreEffects;
}


auto EffectChain::canProcessThroughGraph(const AudioBus& bus) const -> bool
{
	// The audio thread never builds or re-wires a graph; it only runs one the
	// control thread has already prepared. Anything unexpected - a block size
	// the nodes were not prepared for, a bus the planar buffers cannot mirror,
	// or a chain the graph no longer mirrors - falls back to the plain loop, so
	// the fallback is always the pre-existing behaviour rather than a guess.
	return m_graphActive
		&& m_graph != nullptr
		&& m_graph->isPrepared()
		&& m_graph->frames() == bus.frames()
		&& bus.channelPairs() == 1
		&& graphMirrorsEffectList();
}


auto EffectChain::graphMirrorsEffectList() const -> bool
{
	if (m_effectNodes.size() != m_effects.size()) { return false; }
	for (std::size_t i = 0; i < m_effectNodes.size(); ++i)
	{
		if (m_effectNodes[i] == nullptr || m_effectNodes[i]->effect() != m_effects[i])
		{
			return false;
		}
	}
	return true;
}


auto EffectChain::processThroughGraph(AudioBus& bus) -> bool
{
	AudioBuffer& input = *m_graphInput;
	AudioBuffer& output = *m_graphOutput;
	const f_cnt_t frames = m_graph->frames();

	// 1. Mirror the incoming block into the graph's boundary buffer. The bus's
	//    quiet flags and the planar buffer's silence flags have the same
	//    polarity (1 = quiet), so the flags carry over instead of the block
	//    being re-scanned.
	input.silenceAllChannels();
	{
		const float* const samples = bus.trackChannelPair(0).data();
		float* const left = input.buffer(0).data();
		float* const right = input.buffer(1).data();
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			left[f] = samples[2 * f];
			right[f] = samples[2 * f + 1];
		}
	}
	if (!bus.quietChannels()[0]) { input.assumeNonSilent(0); }
	if (!bus.quietChannels()[1]) { input.assumeNonSilent(1); }

	// 2. Run the graph: every node in the cached plan, then the output node's
	//    block is copied into `output`.
	m_graph->process(output);

	// 3. Publish the rendered block back onto the bus, silence flags included:
	//    the mixer reads those flags to decide whether the channels fed by this
	//    one still have something to say.
	{
		float* const samples = bus.trackChannelPair(0).data();
		const float* const left = output.buffer(0).data();
		const float* const right = output.buffer(1).data();
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			samples[2 * f] = left[f];
			samples[2 * f + 1] = right[f];
		}
	}
	bus.quietChannels()[0] = output.silenceFlags()[0];
	bus.quietChannels()[1] = output.silenceFlags()[1];

	// 4. The effects' "continue processing" answers leave the graph through
	//    their nodes; the chain still reports them to the mixer as one answer.
	bool moreEffects = false;
	for (const EffectNode* node : m_effectNodes)
	{
		moreEffects |= node->lastResult();
	}
	return moreEffects;
}




void EffectChain::clear()
{
	emit aboutToClear();

	Engine::audioEngine()->requestChangeInModel();

	while (m_effects.size())
	{
		auto e = m_effects[m_effects.size() - 1];
		m_effects.pop_back();
		delete e;
	}

	Engine::audioEngine()->doneChangeInModel();

	m_enabledModel.setValue( false );

	// Nothing left to route: the graph goes back to empty (and inactive), so
	// the chain's signal path is exactly the plain loop again.
	rebuildRoutingGraph();

	refreshLatency();
}

} // namespace lmms
