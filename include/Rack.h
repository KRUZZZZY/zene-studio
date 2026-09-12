/*
 * Rack.h
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

#ifndef LMMS_RACK_H
#define LMMS_RACK_H

#include <memory>
#include <vector>

#include <QDomElement>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class AudioBuffer;
class AudioBus;
class EffectChain;
class RackChainNode;
class RoutingGraph;

/**
 * @brief A mixer channel's rack: its own effect chain plus parallel chains.
 *
 * The rack is what a RoutingGraph makes expressible on the mixer channel's
 * effect path: two or more chains process the same input block and their
 * outputs are summed into the channel's output. With no rack configured the
 * rack is not on the signal path at all and the channel renders exactly as it
 * did before this class existed (see Rack::canProcessThroughRack).
 *
 * Chain 0 is always the channel's own EffectChain - the object the mixer, the
 * effect rack GUI and the <fxchain> project element already own. Chains 1..n
 * are this rack's, saved as <chain> children of a <rack> element inside the
 * channel's <mixerchannel>; a project that has no rack simply has no <rack>
 * element and loads unchanged.
 *
 * ## The Chain Selector
 *
 * selectedChain() is -1 (the default) or an index into the chain list.
 *
 *  * **-1, parallel**: every chain is fed the same input, and the channel's
 *    output is the sum of every chain's output. This is the layering trick.
 *  * **k, selected**: the input feeds chain k alone and chain k alone is the
 *    channel's output. The chains that are not selected are **not in the
 *    graph's node set at all** - they are not fed, they do not run, and they
 *    contribute nothing (not even their input).
 *
 * Switching the selection is a **control-thread operation**: it rebuilds the
 * rack's node set, so it takes the audio engine's model-change guard like
 * every other topology edit in this tree, and the switch takes effect at a
 * block boundary. There is **no crossfade and no fade**: the block after the
 * switch is exactly the newly selected chain's output, so a step discontinuity
 * (an audible click) is possible where the two chains' outputs differ. The
 * deselected chain's DSP state is *frozen*, not flushed: an effect with a tail
 * (a delay, a reverb) resumes where it stopped when the chain is selected
 * again, rather than having had its tail rendered while it was idle.
 *
 * ## Threading
 *
 * Configuration (chain list, selection, persistence) is control thread only
 * and is not safe against a running process() - the same contract
 * RoutingGraph.h and EffectChain document. The audio thread only reads what
 * the control thread built: canProcessThroughRack(), processAudioBuffer() and
 * routingGraph() are allocation-free and lock-free.
 */
class LMMS_EXPORT Rack
{
public:
	//! The value of selectedChain() that means "every chain runs and sums"
	static constexpr int Parallel = -1;

	/**
	 * @param baseChain the channel's own chain, which is always chain 0. It is
	 *        never owned by the rack; it must outlive the rack.
	 */
	explicit Rack(EffectChain* baseChain);
	~Rack();

	Rack(const Rack&) = delete;
	auto operator=(const Rack&) -> Rack& = delete;

	// --- configuration (control thread only) ---

	auto chainCount() const -> int;
	auto chain(int index) const -> EffectChain*;
	auto baseChain() const -> EffectChain* { return m_baseChain; }

	/**
	 * Adds an empty parallel chain, re-wires the rack and returns its index.
	 * The caller fills it with effects through the existing EffectChain API.
	 */
	auto addChain() -> int;
	//! Drops parallel chain @a index (which must be >= 1) and re-wires
	auto removeChain(int index) -> bool;

	/**
	 * Selects the chain the channel's signal is routed to, or Parallel to
	 * route it to every chain. An index that is not a chain leaves the rack
	 * un-wired rather than guessing; @see selectedChain.
	 */
	void setSelectedChain(int index);
	auto selectedChain() const -> int { return m_selectedChain; }

	//! Drops every parallel chain and the wiring: the channel is a plain chain
	void clear();

	// --- audio thread ---

	/**
	 * Whether this block is rendered through the rack. False - and the channel
	 * then runs its own chain, unchanged - when no rack is configured, when
	 * the block size is not the one the graph was prepared for, when the bus is
	 * not a single channel pair, or when the graph no longer mirrors the chain
	 * list (so the worst case of a missed hook is today's behaviour, never a
	 * wrong render).
	 */
	auto canProcessThroughRack(const AudioBus& bus) const -> bool;

	/**
	 * Renders one channel block through the rack's graph, allocation-free.
	 * @returns the effects' "still processing" answer aggregated over the
	 *          chains, as a single chain reports it.
	 */
	auto processAudioBuffer(AudioBus& bus, const AudioBuffer* sidechainBuffer = nullptr) -> bool;

	// --- persistence ---

	//! Writes a <rack> element, or nothing when the channel has no rack
	void saveSettings(QDomDocument& doc, QDomElement& parent) const;
	//! Reads a <rack> element; a missing element leaves an empty rack
	void loadSettings(const QDomElement& element);

	// --- introspection (control thread) ---

	auto routingGraph() -> RoutingGraph&;
	auto routingGraph() const -> const RoutingGraph&;

	//! The chain indexes the graph currently renders, in wiring order
	auto routedChains() const -> const std::vector<int>& { return m_routedChains; }

private:
	//! Rebuilds the node set from the chain list and the selection
	void rebuildRoutingGraph();
	//! True when the graph's chain nodes still stand for this rack's chains
	auto graphMirrorsChains() const -> bool;

	//! Chains the wiring uses: every chain in parallel mode, one when selected
	auto chainsToRoute() const -> std::vector<int>;

	EffectChain* m_baseChain = nullptr;
	//! Chains 1..n; chain 0 is m_baseChain
	std::vector<std::unique_ptr<EffectChain>> m_parallelChains;

	//! The graph the rack is rendered through and the buffers that carry the
	//! block in and out of it. Built by rebuildRoutingGraph() on the control
	//! thread; processAudioBuffer() only reads them.
	std::unique_ptr<RoutingGraph> m_graph;
	std::unique_ptr<AudioBuffer> m_input;
	std::unique_ptr<AudioBuffer> m_output;
	//! The chain nodes in the graph, index-aligned with m_routedChains
	std::vector<RackChainNode*> m_chainNodes;
	std::vector<int> m_routedChains;

	int m_selectedChain = Parallel;
	//! Set by rebuildRoutingGraph() when the graph is on the signal path
	bool m_graphActive = false;

	//! Audio thread: the channel's sidechain input for the current block
	const AudioBuffer* m_sidechainBuffer = nullptr;
};

} // namespace lmms

#endif // LMMS_RACK_H
