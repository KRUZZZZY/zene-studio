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
#include "MissingDevices.h"
#include "PluginVendors.h"
#include "Engine.h"
#include "LatencyCompensation.h"
#include "RoutingChainNodes.h"
#include "RoutingGraph.h"

namespace lmms
{


#ifdef LMMS_TESTING
void (*EffectChain::s_loadSettingsProbe)() = nullptr;

void EffectChain::setLoadSettingsProbe(void (*probe)())
{
	s_loadSettingsProbe = probe;
}
#endif


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


// The graph rebuild (rebuildRoutingGraph) and the patcher half (patchNodeId,
// setPatchWiring) are defined in src/core/EffectChainPatcher.cpp. This file is
// upstream-inherited and carries a whole-tree file-length baseline row, so the
// fork's additions live in their own translation unit; behaviour is unchanged.






void EffectChain::refreshLatency()
{
	int frames = 0;
	if (m_enabledModel.value())
	{
		for (const Effect* effect : m_effects)
		{
			if (effect->isEnabled() && effect->isOkay() && !effect->dontRun())
			{
				// 0.5.0 L10 (MX-099): a user's latency override replaces what the device reports.
				frames += std::max(0, effect->compensatedLatencyFrames());
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
		// Q050-018 (new-p14-96): the loaded node belongs to its source document. Import a copy; appending it
		// directly moves it out of that document, so a second save would strip the effect from the first.
		// cluster new-p14-97: the copy carries the generic settings the user changed on the placeholder.
		if (auto dummy = dynamic_cast<DummyEffect*>(effect)) { _this.appendChild(effect->preservedXmlWithChangedGenericSettings(_doc, dummy->originalPluginData())); }
		else if (!effect->preservedPluginData().isNull() && !effect->unavailableReason().isEmpty())
		{
			// 0.5.0 Q050-018: a wrapper whose plug-in binary is missing writes back what the file had -
			// with the generic settings the user changed on the placeholder merged in (new-p14-97).
			_this.appendChild(effect->preservedXmlWithChangedGenericSettings(_doc, effect->preservedPluginData()));
		}
		else
		{
			QDomElement ef = effect->saveState( _doc, _this );
			ef.setAttribute( "name", QString::fromUtf8( effect->descriptor()->name ) );
			ef.appendChild( effect->key().saveXML( _doc ) );
			// 0.5.0 Q050-018: the maker is not in the key; keep it beside the name for a later open without the plug-in.
			const auto& attrs = effect->key().attributes;
			QString vendor = pluginvendors::vendorOf(attrs.value("file"), attrs.value("class"));
			if (vendor.isEmpty()) { vendor = pluginvendors::vendorOf(attrs.value("file"), attrs.value("id")); }
			if (!vendor.isEmpty()) { ef.setAttribute("vendor", vendor); }
		}
	}
}




void EffectChain::loadSettings( const QDomElement & _this )
{
	// new-p14-98 (cluster 5051 root cause): loading a chain is a topology edit,
	// not a settings read. Hold the same model-change guard every other edit
	// takes across clear(), the effect-list rebuild and the graph rebuild, so a
	// render period cannot start between the old chain being destroyed and the
	// restored list/graph being published. The guard is recursive, so the
	// request/done pair inside clear() nests inside this one.
	const auto guard = Engine::audioEngine()->requestChangesGuard();

	clear();

#ifdef LMMS_TESTING
	if (s_loadSettingsProbe != nullptr) { s_loadSettingsProbe(); }
#endif

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
				// 0.5.0 Q050-018: a hosted plug-in binary that is missing leaves a placeholder; keep the file's XML.
				if (!e->unavailableReason().isEmpty())
				{
					e->setPreservedPluginData(missingdevices::detachedCopy(effectData));
				}
			}
			else
			{
				delete e;
				e = new DummyEffect( parentModel(), effectData );
			}

			e->setEffectChain( this );
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




void EffectChain::setEnabledUnjournalled(bool on)
{
	const bool journalling = m_enabledModel.testAndSetJournalling(false);
	m_enabledModel.setValue(on);
	m_enabledModel.setJournalling(journalling);
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
	// A removed effect is no longer on this chain, so it must stop naming it: callers read a nullptr
	// effectChain() as "taken off its chain". Set inside the same model change as the erase, so the
	// audio thread never sees the effect's back-pointer disagree with the list it renders.
	_effect->setEffectChain( nullptr );

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
		// 0.5.0 L10 (MX-080): through the device's delta tap - its plain output while the tap is off.
		moreEffects |= effect->deltaTap().process(*effect, buffer);
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
			// 0.5.0 L10 (MX-080): through the device's delta tap - its plain output while the tap is off.
			moreEffects |= effect->deltaTap().process(*effect, bus);
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

	// The routing graph's nodes point at the effects. Drop those nodes while
	// the effects are still alive and while the audio thread is excluded.
	m_graph->clear();
	m_effectNodes.clear();
	m_graphInput.reset();
	m_graphOutput.reset();
	m_graphActive = false;
	m_patchDropped = false;

	while (m_effects.size())
	{
		auto e = m_effects[m_effects.size() - 1];
		m_effects.pop_back();
		delete e;
	}

	Engine::audioEngine()->doneChangeInModel();

	m_enabledModel.setValue( false );

	refreshLatency();
}

} // namespace lmms
