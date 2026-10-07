/*
 * InstrumentOutputBuses.cpp - 0.5.0 MX-114: a hosted instrument's extra audio
 *                             output buses, each routable to a mixer channel.
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

#include "InstrumentOutputBuses.h"

#include <algorithm>

#include <QDomDocument>
#include <QDomElement>

#include "AudioBus.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "LatencyCompensation.h"
#include "Mixer.h"
#include "SampleFrame.h"

namespace lmms
{

//! One bus's route and the storage its audio passes through.
struct InstrumentOutputBuses::Slot
{
	std::atomic<int> channelId{-1};          //!< the persistent id of the target channel; -1 = not routed
	mutable std::atomic<int> cachedIndex{-1}; //!< the mixer index that id had last period
	std::vector<SampleFrame> block;          //!< one period, sized by prepareSlot
	LatencyCompensation delay;               //!< the target channel's PDC alignment
};

namespace
{

//! RAII for the audio engine's model-change guard (recursive; a no-op without an engine).
class ChangeGuard
{
public:
	ChangeGuard() : m_engine(Engine::audioEngine()) { if (m_engine != nullptr) { m_engine->requestChangeInModel(); } }
	~ChangeGuard() { if (m_engine != nullptr) { m_engine->doneChangeInModel(); } }
	ChangeGuard(const ChangeGuard&) = delete;
	auto operator=(const ChangeGuard&) -> ChangeGuard& = delete;

private:
	AudioEngine* m_engine;
};

//! Interleaves one bus out of the planar output: mono buses play on both sides.
void interleave(SampleFrame* dst, const float* left, const float* right, f_cnt_t frames)
{
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		dst[f][0] = left[f];
		dst[f][1] = right[f];
	}
}

} // namespace

InstrumentOutputBuses::InstrumentOutputBuses() = default;
InstrumentOutputBuses::~InstrumentOutputBuses() = default;

void InstrumentOutputBuses::setLayout(const std::vector<InstrumentOutputBus>& buses)
{
	const auto count = std::min<std::size_t>(buses.size(), MaxBuses);
	ChangeGuard guard;
	m_layout.assign(buses.begin(), buses.begin() + static_cast<std::ptrdiff_t>(count));
}

auto InstrumentOutputBuses::bus(int index) const -> const InstrumentOutputBus*
{
	return index >= 0 && index < busCount() ? &m_layout[static_cast<std::size_t>(index)] : nullptr;
}

void InstrumentOutputBuses::prepareSlot(Slot& slot)
{
	const f_cnt_t frames = Engine::audioEngine() != nullptr ? Engine::audioEngine()->framesPerPeriod() : 0;
	if (frames == 0 || slot.block.size() == static_cast<std::size_t>(frames)) { return; }
	slot.block.assign(static_cast<std::size_t>(frames), SampleFrame{});
	slot.delay.init(frames);
}

void InstrumentOutputBuses::storeRoute(int bus, int channelId)
{
	auto& slot = m_slots[static_cast<std::size_t>(bus)];
	const f_cnt_t frames = Engine::audioEngine() != nullptr ? Engine::audioEngine()->framesPerPeriod() : 0;
	std::unique_ptr<Slot> prepared;
	if (slot == nullptr || (channelId >= 0 && frames != 0 && slot->block.size() != static_cast<std::size_t>(frames)))
	{
		prepared = std::make_unique<Slot>();
		// Prepare detached storage before the guard; render readers only see it after publication.
		if (channelId >= 0) { prepareSlot(*prepared); }
	}
	ChangeGuard guard;
	if (prepared != nullptr) { slot = std::move(prepared); }
	slot->channelId.store(channelId, std::memory_order_release);
	slot->cachedIndex.store(-1, std::memory_order_relaxed);
}

auto InstrumentOutputBuses::route(int bus, int channelId) -> bool
{
	if (bus < 1 || bus >= busCount()) { return false; }
	if (channelId >= 0 && m_layout[static_cast<std::size_t>(bus)].firstChannel < 0)
	{
		// An inactive bus (a VST3 aux output) is switched on by the host, which reports the new layout.
		if (!m_enable || !m_enable(bus)) { return false; }
		if (bus >= busCount() || m_layout[static_cast<std::size_t>(bus)].firstChannel < 0) { return false; }
	}
	storeRoute(bus, channelId);
	return true;
}

auto InstrumentOutputBuses::routeOf(int bus) const -> int
{
	if (bus < 0 || bus >= MaxBuses || m_slots[static_cast<std::size_t>(bus)] == nullptr) { return -1; }
	return m_slots[static_cast<std::size_t>(bus)]->channelId.load(std::memory_order_acquire);
}

auto InstrumentOutputBuses::routedCount() const -> int
{
	int count = 0;
	for (int bus = 1; bus < MaxBuses; ++bus) { count += routeOf(bus) >= 0 ? 1 : 0; }
	return count;
}

void InstrumentOutputBuses::clearRoutes()
{
	for (int bus = 1; bus < MaxBuses; ++bus)
	{
		if (routeOf(bus) >= 0) { storeRoute(bus, -1); }
	}
}

void InstrumentOutputBuses::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	if (routedCount() == 0) { return; }
	QDomElement element = doc.createElement(QStringLiteral("outputbuses"));
	for (int bus = 1; bus < MaxBuses; ++bus)
	{
		if (routeOf(bus) < 0) { continue; }
		QDomElement entry = doc.createElement(QStringLiteral("route"));
		entry.setAttribute(QStringLiteral("bus"), bus);
		entry.setAttribute(QStringLiteral("channel"), routeOf(bus));
		element.appendChild(entry);
	}
	parent.appendChild(element);
}

void InstrumentOutputBuses::loadSettings(const QDomElement& parent)
{
	clearRoutes();
	const QDomElement element = parent.firstChildElement(QStringLiteral("outputbuses"));
	for (QDomElement entry = element.firstChildElement(QStringLiteral("route")); !entry.isNull();
		entry = entry.nextSiblingElement(QStringLiteral("route")))
	{
		const int bus = entry.attribute(QStringLiteral("bus")).toInt();
		const int channel = entry.attribute(QStringLiteral("channel"), QStringLiteral("-1")).toInt();
		if (bus < 1 || bus >= MaxBuses || channel < 0) { continue; }
		// Through route() when the layout already has the bus (it may need enabling); kept otherwise.
		if (!route(bus, channel)) { storeRoute(bus, channel); }
	}
}

auto InstrumentOutputBuses::mixerIndexOf(Slot& slot, int channelId) const -> int
{
	Mixer* mixer = Engine::mixer();
	if (mixer == nullptr) { return -1; }
	const int channels = static_cast<int>(mixer->numChannels());
	const int cached = slot.cachedIndex.load(std::memory_order_relaxed);
	if (cached > 0 && cached < channels && mixer->mixerChannel(cached)->id() == channelId) { return cached; }
	// The master (index 0) is a valid target too; a linear scan of pointers, no allocation.
	for (int i = 0; i < channels; ++i)
	{
		if (mixer->mixerChannel(i)->id() != channelId) { continue; }
		slot.cachedIndex.store(i, std::memory_order_relaxed);
		return i;
	}
	return -1;
}

auto InstrumentOutputBuses::routedBuffer(int bus, f_cnt_t frames) -> SampleFrame*
{
	if (bus < 1 || bus >= MaxBuses || m_slots[static_cast<std::size_t>(bus)] == nullptr) { return nullptr; }
	Slot& slot = *m_slots[static_cast<std::size_t>(bus)];
	if (slot.channelId.load(std::memory_order_acquire) < 0) { return nullptr; }
	return slot.block.size() == static_cast<std::size_t>(frames) ? slot.block.data() : nullptr;
}

void InstrumentOutputBuses::send(int bus, f_cnt_t frames, bool muted)
{
	SampleFrame* block = routedBuffer(bus, frames);
	if (block == nullptr) { return; }
	Slot& slot = *m_slots[static_cast<std::size_t>(bus)];
	if (muted)
	{
		slot.delay.advanceSilence(frames);
		return;
	}
	const int index = mixerIndexOf(slot, slot.channelId.load(std::memory_order_acquire));
	if (index < 0) { return; }
	Mixer* mixer = Engine::mixer();
	slot.delay.setDelayFrames(mixer->channelInputLatency(static_cast<mix_ch_t>(index)));
	slot.delay.processInPlace(block, frames);
	SampleFrame* pair = block;
	AudioBus audio{&pair, 1, frames};
	audio.updateAll();
	mixer->mixToChannel(audio, static_cast<mix_ch_t>(index));
}

void InstrumentOutputBuses::deliver(const float* const* channels, int channelCount, f_cnt_t frames, bool muted)
{
	for (int bus = 1; bus < busCount(); ++bus)
	{
		const InstrumentOutputBus& layout = m_layout[static_cast<std::size_t>(bus)];
		SampleFrame* block = routedBuffer(bus, frames);
		if (block == nullptr || layout.firstChannel < 0 || layout.firstChannel >= channelCount) { continue; }
		const int right = layout.channels > 1 && layout.firstChannel + 1 < channelCount
			? layout.firstChannel + 1 : layout.firstChannel;
		interleave(block, channels[layout.firstChannel], channels[right], frames);
		send(bus, frames, muted);
	}
}

} // namespace lmms
