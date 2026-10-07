/*
 * ClapBusMap.cpp - maps CLAP audio ports onto the AudioPorts transport
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

#include "ClapBusMap.h"

#include <algorithm>

#include "lmms_constants.h"

namespace lmms::clap
{

auto mapPorts(const std::vector<PortDescriptor>& ports) -> PortLayout
{
	PortLayout layout;
	constexpr auto maxChannels = static_cast<int>(MaxChannelsPerAudioBuffer);

	for (const auto& port : ports)
	{
		if (port.channelCount <= 0) { continue; }

		const auto budget = port.direction == PortDirection::Input
			? maxChannels - layout.inputs
			: maxChannels - layout.outputs;
		const auto channels = std::min(port.channelCount, budget);
		if (channels <= 0) { continue; }

		if (port.direction == PortDirection::Input)
		{
			layout.inputPortChannels.push_back(channels);
			layout.inputs += channels;
		}
		else
		{
			layout.outputPortChannels.push_back(channels);
			layout.outputs += channels;
		}
	}

	layout.hasSideChain = layout.inputPortChannels.size() > 1;
	layout.sideChainChannel = layout.hasSideChain ? layout.inputPortChannels[0] : -1;
	return layout;
}

auto outputBusesOf(const std::vector<PortDescriptor>& ports) -> std::vector<InstrumentOutputBus>
{
	std::vector<InstrumentOutputBus> buses;
	constexpr auto maxChannels = static_cast<int>(MaxChannelsPerAudioBuffer);
	int next = 0;
	for (const auto& port : ports)
	{
		if (port.direction != PortDirection::Output || port.channelCount <= 0) { continue; }
		const int channels = std::min(port.channelCount, maxChannels - next);
		const int mapped = std::max(channels, 0);
		buses.push_back({port.name, mapped, channels > 0 ? next : -1});
		next += mapped;
	}
	return buses;
}

} // namespace lmms::clap
