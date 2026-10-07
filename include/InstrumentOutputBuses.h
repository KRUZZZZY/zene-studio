/*
 * InstrumentOutputBuses.h - 0.5.0 MX-114: a hosted instrument's extra audio
 *                           output buses, each routable to a mixer channel.
 *
 * Copyright (c) 2026 Zene Studio contributors
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
 */

#ifndef LMMS_INSTRUMENT_OUTPUT_BUSES_H
#define LMMS_INSTRUMENT_OUTPUT_BUSES_H

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "InstrumentOutputBus.h"
#include "LmmsTypes.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

class SampleFrame;

/**
 * @brief 0.5.0 MX-114: the extra output buses of a CLAP, VST3 or LV2 instrument, and where each one goes.
 *
 * Bus 0 is the instrument's main output: it reaches the track exactly as before, through the pin
 * connector, and is never routed here. Every other bus (a drum sampler's kick and snare outputs, a
 * multitimbral synth's part outputs) is silent by default - the pin connector routes only the first two
 * plug-in channels to the track - and plays into a mixer channel once it is ROUTED to one. A routed bus
 * bypasses the track's own volume, pan and effects (it is a separate signal, as in every DAW), follows
 * the track's mute, and is delayed by the target channel's PDC alignment like any other source feeding
 * that channel. MX-157: it carries the instrument's own latency (setSourceLatencyFrames), which the mixer
 * counts in that channel's alignment point, so the bus is delayed only by what the alignment adds on top.
 *
 * The host reports the layout (setLayout) whenever it loads or re-prepares the plug-in. A bus can be
 * inactive (a VST3 aux bus is off until a host asks for it): routing one calls the host's enable hook,
 * which activates it and reports the new layout.
 *
 * Threading: configuration is control thread only; a route is published under the audio engine's
 * model-change guard, after its buffers were allocated, so the audio thread only reads prepared state.
 * The audio side (deliver / routedBuffer / send) runs on the worker that renders the instrument, and
 * neither allocates nor locks beyond Mixer::mixToChannel's own channel spin lock. Routes name a channel
 * by its persistent id (ch-<n>), so moving or deleting other channels never redirects a bus; a route
 * whose channel is gone plays nowhere until it is routed again.
 */
class LMMS_EXPORT InstrumentOutputBuses
{
public:
	static constexpr int MaxBuses = 32;
	//! Asked to activate bus @a bus; true when the host did (it reports the new layout itself).
	using EnableHook = std::function<bool(int bus)>;

	InstrumentOutputBuses();
	~InstrumentOutputBuses();
	InstrumentOutputBuses(const InstrumentOutputBuses&) = delete;
	auto operator=(const InstrumentOutputBuses&) -> InstrumentOutputBuses& = delete;

	// --- control thread ---

	//! The host's buses, main first; anything past MaxBuses is dropped. Under the model-change guard.
	void setLayout(const std::vector<InstrumentOutputBus>& buses);
	auto busCount() const -> int { return static_cast<int>(m_layout.size()); }
	auto bus(int index) const -> const InstrumentOutputBus*;
	void setEnableHook(EnableHook hook) { m_enable = std::move(hook); }

	/*! Routes bus @a bus (1..busCount()-1) to the mixer channel whose persistent id is @a channelId, or
	 *  un-routes it (@a channelId < 0). False for the main bus, an index past the layout, or a bus that
	 *  stays inactive after the host was asked to enable it. */
	auto route(int bus, int channelId) -> bool;
	//! The persistent id of the channel @a bus plays into, or -1.
	auto routeOf(int bus) const -> int;
	auto routedCount() const -> int;
	//! Drops every route (an instrument replaced, a project cleared).
	void clearRoutes();

	//! Writes <outputbuses><route bus=".." channel=".."/>..</outputbuses> when anything is routed.
	void saveSettings(QDomDocument& doc, QDomElement& parent) const;
	//! Reads the <outputbuses> child of @a parent; routes for a bus the layout lacks are kept for later.
	void loadSettings(const QDomElement& parent);

	// --- audio thread (the worker rendering the instrument) ---

	/*! Copies every routed extra bus out of the plug-in's planar output (@a channels, @a channelCount
	 *  pointers of @a frames samples) and mixes it into its channel. Nothing routed: one loop of loads. */
	void deliver(const float* const* channels, int channelCount, f_cnt_t frames, bool muted);
	//! The prepared block for routed bus @a bus, or nullptr (not routed, or @a frames is not the block size).
	auto routedBuffer(int bus, f_cnt_t frames) -> SampleFrame*;
	//! Mixes routed bus @a bus's block (filled through routedBuffer) into its channel.
	void send(int bus, f_cnt_t frames, bool muted);

	// --- MX-157 (BUGS 11.34): the instrument's own latency, for delay compensation ---
	/*! Control thread: the latency the plug-in reports, in frames (Instrument::reportLatency). Every bus
	 *  carries it; while it is non-zero these buses are listed for the mixer's alignment pass
	 *  (forEachLatentRoute). 0, the default, leaves every bus exactly as before. */
	void setSourceLatencyFrames(int frames);
	auto sourceLatencyFrames() const -> int { return m_sourceLatency.load(std::memory_order_relaxed); }
	//! Audio thread: the mixer index routed, active bus @a bus plays into now, or -1.
	auto routedIndex(int bus) -> int;
	/*! Audio thread, inside the render period (Mixer::updateLatencyCompensation, which runs under the
	 *  engine's model-change lock - the lock the list is changed under): calls @a visit(mixerIndex,
	 *  latencyFrames) for every routed bus of every instrument that reports a latency. Allocation-free. */
	template<typename Visit>
	static void forEachLatentRoute(Visit&& visit)
	{
		for (InstrumentOutputBuses* buses : latentList())
		{
			const int latency = buses->sourceLatencyFrames();
			for (int bus = 1; latency > 0 && bus < buses->busCount(); ++bus)
			{
				if (const int index = buses->routedIndex(bus); index >= 0) { visit(index, latency); }
			}
		}
	}

private:
	//! MX-157: the instruments whose latency is non-zero; changed only under the model-change guard.
	static auto latentList() -> std::vector<InstrumentOutputBuses*>&;
	struct Slot;

	//! Allocates @a slot's block and delay line for the engine's period (control thread, before the guard).
	void prepareSlot(Slot& slot);
	//! The current mixer index of persistent channel id @a channelId, cached in @a slot; -1 when gone.
	auto mixerIndexOf(Slot& slot, int channelId) const -> int;
	void storeRoute(int bus, int channelId);

	std::vector<InstrumentOutputBus> m_layout;
	std::array<std::unique_ptr<Slot>, MaxBuses> m_slots;
	EnableHook m_enable;
	std::atomic<int> m_sourceLatency{0};  //!< MX-157, see setSourceLatencyFrames()
	bool m_listed = false;                //!< MX-157: in latentList()
};

} // namespace lmms

#endif // LMMS_INSTRUMENT_OUTPUT_BUSES_H
