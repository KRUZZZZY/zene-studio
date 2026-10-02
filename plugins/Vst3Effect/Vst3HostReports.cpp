/*
 * Vst3HostReports.cpp - what the VST3 host reports about its plug-in: latency, tail, output events
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
 *
 */

#include <algorithm>
#include <climits>

#include "Vst3HostInternals.h"

namespace lmms::vst3
{

auto HostedPlugin::latency() const -> std::uint32_t
{
	const auto& d = *m_impl;
	return d.prepared && d.processor ? d.processor->getLatencySamples() : 0;
}

auto HostedPlugin::tail() const -> int
{
	const auto& d = *m_impl;
	if (!d.prepared || !d.processor) { return 0; }
	const uint32 frames = d.processor->getTailSamples();
	return frames == kInfiniteTail ? -1 : static_cast<int>(std::min<uint32>(frames, INT32_MAX));
}

void HostedPlugin::setLatencyChangedCallback(std::function<void()> callback)
{
	m_impl->onLatencyChanged = std::move(callback);
	if (m_impl->handlerImpl != nullptr) { m_impl->handlerImpl->setOnLatencyChanged(m_impl->onLatencyChanged); }
}

auto HostedPlugin::outputEventCount() const -> int { return m_impl->blockOutputCount; }

auto HostedPlugin::outputEvent(int index) const -> const MidiEventIn&
{
	return m_impl->blockOutput[static_cast<std::size_t>(index)];
}

} // namespace lmms::vst3
