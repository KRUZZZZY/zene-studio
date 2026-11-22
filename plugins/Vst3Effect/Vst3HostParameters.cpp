/*
 * Vst3HostParameters.cpp - MIDI input and parameter control for the VST3 host
 *
 * Copyright (c) 2026 Zene Studio contributors
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 */

#include "Vst3HostInternals.h"

#include <algorithm>

namespace lmms::vst3
{

auto HostedPlugin::parameters() const -> const std::vector<Vst3ParamDescriptor>&
{
	return m_impl->params;
}

auto HostedPlugin::busLayout() const -> const BusLayout&
{
	return m_impl->layout;
}

auto HostedPlugin::className() const -> QString
{
	return m_impl->className;
}

auto HostedPlugin::vendor() const -> QString
{
	return m_impl->vendor;
}

auto HostedPlugin::isInstrument() const -> bool
{
	return m_impl->instrument;
}

auto HostedPlugin::isLoaded() const -> bool
{
	return m_impl->component != nullptr && m_impl->processor != nullptr;
}

auto HostedPlugin::receivesMidi() const -> bool
{
	return m_impl->midiEnabled;
}

auto HostedPlugin::eventInputBusIndex() const -> int
{
	return static_cast<int>(m_impl->eventInputBusIndex);
}

void HostedPlugin::pushMidiEvent(const MidiEventIn& event)
{
	auto& d = *m_impl;
	// Nothing would ever drain the queue if the plug-in has no instrument
	// event input, so refuse rather than let events pile up unread. The push is
	// announced before the switch is read: release() turns the switch off, then
	// waits for the announced pushes before it resets the queue, so a reset never
	// lands in the middle of one (round 6). Both are seq_cst, which is what makes
	// "this push sees the switch off" or "release() sees this push" certain.
	d.midiProducers.fetch_add(1);
	if (d.midiEnabled && !d.midiQueue.push(event)) { ++d.droppedMidi; }
	d.midiProducers.fetch_sub(1);
}

auto HostedPlugin::droppedMidiEvents() const -> std::uint64_t
{
	return m_impl->droppedMidi;
}

void HostedPlugin::setParamNormalized(std::uint32_t id, float normalized)
{
	auto& d = *m_impl;
	const auto index = paramIndex(id);
	if (index < 0) { return; }
	// A value with no meaning (NaN, an infinity) is refused and the last good one
	// kept: std::clamp() passes NaN through, and the audio thread would hand this
	// value to the processor's parameter queue as it is.
	const auto keep = d.paramValues[index].load(std::memory_order_relaxed);
	d.paramValues[index].store(sanitizedNormalized(normalized, keep), std::memory_order_relaxed);
}

void HostedPlugin::notifyController(std::uint32_t id, float normalized)
{
	auto& d = *m_impl;
	setParamNormalized(id, normalized);
	if (d.controller)
	{
		const auto index = paramIndex(id);
		const auto keep = index < 0 ? 0.f : d.paramValues[index].load(std::memory_order_relaxed);
		d.controller->setParamNormalized(static_cast<ParamID>(id),
			sanitizedNormalized(normalized, keep));
	}
}

void HostedPlugin::readParamEdits(std::uint32_t id, PluginParamGesture::Edits* edits)
{
	const auto index = paramIndex(id);
	if (index < 0) { *edits = {}; return; }
	m_impl->paramGestures[index].read(m_impl->paramValues[index], edits);
}

auto HostedPlugin::paramNormalized(std::uint32_t id) const -> float
{
	const auto index = paramIndex(id);
	return index < 0 ? 0.f : m_impl->paramValues[index].load(std::memory_order_relaxed);
}

auto HostedPlugin::paramIndex(std::uint32_t id) const -> int
{
	const auto& indexById = m_impl->indexById;
	const auto it = std::lower_bound(indexById.begin(), indexById.end(),
		std::make_pair(id, std::size_t{0}));
	if (it == indexById.end() || it->first != id) { return -1; }
	return static_cast<int>(it->second);
}


} // namespace lmms::vst3
