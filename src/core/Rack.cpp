/*
 * Rack.cpp
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

#include "Rack.h"

#include <algorithm>

#include <QDomDocument>

#include "AudioBuffer.h"
#include "AudioBus.h"
#include "AudioEngine.h"
#include "EffectChain.h"
#include "Engine.h"
#include "RackNodes.h"
#include "RoutingChainNodes.h"
#include "RoutingGraph.h"
#include "lmms_constants.h"

namespace lmms
{

namespace
{

//! The element a rack is saved as inside a <mixerchannel>
constexpr auto RACK_ELEMENT = "rack";
constexpr auto RACK_VERSION = 1;

} // namespace

Rack::Rack(EffectChain* baseChain) :
	m_baseChain(baseChain),
	m_graph(std::make_unique<RoutingGraph>())
{
}

Rack::~Rack() = default;

auto Rack::chainCount() const -> int
{
	return 1 + static_cast<int>(m_parallelChains.size());
}

auto Rack::chain(int index) const -> EffectChain*
{
	if (index == 0) { return m_baseChain; }
	if (index < 1 || index > static_cast<int>(m_parallelChains.size())) { return nullptr; }
	return m_parallelChains[index - 1].get();
}

auto Rack::chainsToRoute() const -> std::vector<int>
{
	std::vector<int> routed;
	if (m_selectedChain == Parallel)
	{
		for (int index = 0; index < chainCount(); ++index) { routed.push_back(index); }
		return routed;
	}
	if (chain(m_selectedChain) != nullptr) { routed.push_back(m_selectedChain); }
	return routed;
}

void Rack::rebuildRoutingGraph()
{
	m_graph->clear();
	m_chainNodes.clear();
	m_routedChains.clear();
	m_input.reset();
	m_output.reset();
	m_graphActive = false;

	auto* engine = Engine::audioEngine();
	const f_cnt_t frames = engine != nullptr ? engine->framesPerPeriod() : 0;
	if (frames == 0) { return; }

	// One chain is not a rack: the channel's own chain is the whole signal
	// path, exactly as it was before this class existed.
	if (chainCount() < 2) { return; }

	// An out-of-range selection wires nothing, so a bad selection cannot route
	// a chain nobody asked for (see setSelectedChain).
	const std::vector<int> routed = chainsToRoute();
	if (routed.empty() || static_cast<int>(routed.size()) > chainCount()) { return; }

	m_input = std::make_unique<AudioBuffer>(frames, DEFAULT_CHANNELS);
	m_output = std::make_unique<AudioBuffer>(frames, DEFAULT_CHANNELS);
	m_input->silenceAllChannels();
	m_output->silenceAllChannels();

	auto input = std::make_unique<ChainInputNode>(m_input.get());
	const int inputId = m_graph->addNode(std::move(input));

	auto sum = std::make_unique<RackSumNode>();
	const int sumId = m_graph->addNode(std::move(sum));

	// One node per chain the wiring uses, all fed the same input block; every
	// chain's output lands on the sum node's single input port, which takes any
	// number of connections (RoutingGraph::connect only rejects duplicates and
	// cycles). Chains the selection leaves out are not added to the graph at
	// all, so the plan never runs them.
	for (const int index : routed)
	{
		auto node = std::make_unique<RackChainNode>(chain(index));
		RackChainNode* const raw = node.get();
		const int id = m_graph->addNode(std::move(node));
		m_chainNodes.push_back(raw);

		QString error;
		if (!m_graph->connect(inputId, id, 0, 0, &error) ||
			!m_graph->connect(id, sumId, 0, 0, &error))
		{
			// Unreachable for this shape (a fan-out from the input node into
			// the sum node's single port; no cycle is possible). Leave the rack
			// off the path rather than on a half-wired graph.
			m_graph->clear();
			m_chainNodes.clear();
			m_input.reset();
			m_output.reset();
			return;
		}
	}

	// The wiring the audio thread will read: routedChains() first, then the
	// prepared graph, then the flag that puts it on the path.
	m_routedChains = routed;
	m_graph->setOutputNode(sumId);
	m_graph->prepare(frames, DEFAULT_CHANNELS);
	m_graphActive = true;
}

auto Rack::addChain() -> int
{
	auto* engine = Engine::audioEngine();
	if (engine != nullptr) { engine->requestChangeInModel(); }

	m_parallelChains.push_back(std::make_unique<EffectChain>(nullptr));
	const int index = chainCount() - 1;
	rebuildRoutingGraph();

	if (engine != nullptr) { engine->doneChangeInModel(); }
	return index;
}

auto Rack::removeChain(int index) -> bool
{
	if (index < 1 || index > static_cast<int>(m_parallelChains.size())) { return false; }

	auto* engine = Engine::audioEngine();
	if (engine != nullptr) { engine->requestChangeInModel(); }

	m_parallelChains.erase(m_parallelChains.begin() + (index - 1));
	// The selection has to name a chain that is still there: a chain that was
	// removed drops the rack back to parallel, a later one slides down.
	if (m_selectedChain == index) { m_selectedChain = Parallel; }
	else if (m_selectedChain > index) { --m_selectedChain; }
	rebuildRoutingGraph();

	if (engine != nullptr) { engine->doneChangeInModel(); }
	return true;
}

void Rack::setSelectedChain(int index)
{
	auto* engine = Engine::audioEngine();
	if (engine != nullptr) { engine->requestChangeInModel(); }

	m_selectedChain = index;
	rebuildRoutingGraph();

	if (engine != nullptr) { engine->doneChangeInModel(); }
}

void Rack::clear()
{
	auto* engine = Engine::audioEngine();
	if (engine != nullptr) { engine->requestChangeInModel(); }

	m_parallelChains.clear();
	m_selectedChain = Parallel;
	rebuildRoutingGraph();

	if (engine != nullptr) { engine->doneChangeInModel(); }
}

auto Rack::canProcessThroughRack(const AudioBus& bus) const -> bool
{
	// Anything unexpected falls back to the channel's own chain, so the
	// fallback is always the pre-existing behaviour rather than a guess.
	return m_graphActive
		&& m_graph != nullptr
		&& m_graph->isPrepared()
		&& m_graph->frames() == bus.frames()
		&& bus.channelPairs() == 1
		&& graphMirrorsChains();
}

auto Rack::graphMirrorsChains() const -> bool
{
	if (m_chainNodes.size() != m_routedChains.size()) { return false; }
	for (std::size_t i = 0; i < m_chainNodes.size(); ++i)
	{
		if (m_chainNodes[i] == nullptr || m_chainNodes[i]->chain() != chain(m_routedChains[i]))
		{
			return false;
		}
	}
	return true;
}

auto Rack::processAudioBuffer(AudioBus& bus, const AudioBuffer* sidechainBuffer) -> bool
{
	if (!canProcessThroughRack(bus)) { return false; }

	AudioBuffer& input = *m_input;
	AudioBuffer& output = *m_output;
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

	// 2. Every chain in this block sees the channel's sidechain input, exactly
	//    as the channel's own chain did when it was the whole path (Phase D).
	m_sidechainBuffer = sidechainBuffer;
	for (RackChainNode* node : m_chainNodes) { node->setSidechain(m_sidechainBuffer); }

	// 3. Run the graph: every node in the cached plan, then the sum node's
	//    block is copied into `output`.
	m_graph->process(output);

	m_sidechainBuffer = nullptr;
	for (RackChainNode* node : m_chainNodes) { node->setSidechain(nullptr); }

	// 4. Publish the rendered block back onto the bus, silence flags included:
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

	// 5. The chains' "continue processing" answers leave the graph through
	//    their nodes; the rack still reports them to the mixer as one answer.
	bool moreEffects = false;
	for (const RackChainNode* node : m_chainNodes) { moreEffects |= node->lastResult(); }
	return moreEffects;
}

auto Rack::routingGraph() -> RoutingGraph&
{
	return *m_graph;
}

auto Rack::routingGraph() const -> const RoutingGraph&
{
	return *m_graph;
}

void Rack::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	// A channel with no rack writes no element, so an unracked project is
	// byte-for-byte the project this build would have written without racks.
	if (m_parallelChains.empty()) { return; }

	QDomElement rack = doc.createElement(QString::fromLatin1(RACK_ELEMENT));
	rack.setAttribute(QStringLiteral("version"), RACK_VERSION);
	rack.setAttribute(QStringLiteral("selected"), m_selectedChain);

	for (std::size_t i = 0; i < m_parallelChains.size(); ++i)
	{
		QDomElement chainElement = doc.createElement(QStringLiteral("chain"));
		chainElement.setAttribute(QStringLiteral("index"), static_cast<int>(i) + 1);
		// The chain's own <fxchain> element, saved by the code that saves the
		// channel's chain: effects, their order and their settings are the
		// existing machinery's business, not the rack's.
		m_parallelChains[i]->saveState(doc, chainElement);
		rack.appendChild(chainElement);
	}

	parent.appendChild(rack);
}

void Rack::loadSettings(const QDomElement& element)
{
	clear();

	const QDomElement rack = element.firstChildElement(QString::fromLatin1(RACK_ELEMENT));
	// An old project has no <rack> element, and neither has a project whose
	// channel has none: both leave an empty rack, which is not on the path.
	if (rack.isNull()) { return; }

	for (QDomElement chainElement = rack.firstChildElement(QStringLiteral("chain"));
		!chainElement.isNull(); chainElement = chainElement.nextSiblingElement(QStringLiteral("chain")))
	{
		auto chain = std::make_unique<EffectChain>(nullptr);
		const QDomElement chainState = chainElement.firstChildElement(chain->nodeName());
		if (!chainState.isNull()) { chain->restoreState(chainState); }
		m_parallelChains.push_back(std::move(chain));
	}

	m_selectedChain = rack.attribute(QStringLiteral("selected"), QString::number(Parallel)).toInt();
	rebuildRoutingGraph();
}

} // namespace lmms
