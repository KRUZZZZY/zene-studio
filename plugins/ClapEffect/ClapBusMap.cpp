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

namespace
{

//! One usable input port with the main flag the MAP uses and its position among the plug-in's input
//! ports as clap.audio-ports reports them: the host hands the plug-in its audio_inputs in that order, so
//! the index is what the plug-in reads its port by, whatever order the transport lays the channels in.
struct MappedInput
{
	const PortDescriptor* port;
	bool main;
	int pluginIndex;
};

//! The input ports main-first: a plug-in is free to list its side-chain input before its main one, and
//! the transport's first channels are the main signal. Everything behind the main port is side-chain.
bool isUsableInput(const PortDescriptor& port)
{
	return port.direction == PortDirection::Input && port.channelCount > 0;
}

bool anyMain(const std::vector<MappedInput>& inputs)
{
	return std::any_of(inputs.begin(), inputs.end(), [](const MappedInput& input) { return input.main; });
}

auto orderedInputs(const std::vector<PortDescriptor>& ports) -> std::vector<MappedInput>
{
	std::vector<MappedInput> ordered;
	ordered.reserve(ports.size());
	int pluginIndex = 0;
	for (const auto& port : ports)
	{
		if (port.direction != PortDirection::Input) { continue; }
		if (isUsableInput(port)) { ordered.push_back({&port, port.isMain, pluginIndex}); }
		++pluginIndex;
	}
	std::stable_partition(ordered.begin(), ordered.end(), [](const MappedInput& input) { return input.main; });
	// No input port carries the flag: the reporting order decides, and its first port is the main one.
	if (!ordered.empty() && !anyMain(ordered)) { ordered.front().main = true; }
	return ordered;
}

} // namespace

auto mapPorts(const std::vector<PortDescriptor>& ports) -> PortLayout
{
	PortLayout layout;
	constexpr auto maxChannels = static_cast<int>(MaxChannelsPerAudioBuffer);

	// --- inputs, main port first ------------------------------------------
	int mainChannels = 0;
	layout.pluginInputPorts = static_cast<int>(std::count_if(ports.begin(), ports.end(),
		[](const PortDescriptor& port) { return port.direction == PortDirection::Input; }));
	for (const auto& input : orderedInputs(ports))
	{
		const auto channels = std::min(input.port->channelCount, maxChannels - layout.inputs);
		if (channels <= 0) { continue; }
		layout.inputPortChannels.push_back(channels);
		layout.inputPortIndices.push_back(input.pluginIndex);
		layout.inputs += channels;
		if (input.main) { mainChannels += channels; }
	}

	// --- outputs, in the plug-in's own order -------------------------------
	for (const auto& port : ports)
	{
		if (port.direction != PortDirection::Output || port.channelCount <= 0) { continue; }
		const auto channels = std::min(port.channelCount, maxChannels - layout.outputs);
		if (channels <= 0) { continue; }
		layout.outputPortChannels.push_back(channels);
		layout.outputs += channels;
	}

	// The side-chain is what follows the main port on the flattened inputs; a plug-in with a single
	// input port (or one whose extra ports the channel budget dropped) has none.
	layout.hasSideChain = mainChannels < layout.inputs;
	layout.sideChainChannel = layout.hasSideChain ? mainChannels : -1;
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
		buses.push_back({port.name, port.channelCount, channels > 0 ? next : -1});
		next += std::max(channels, 0);
	}
	return buses;
}

} // namespace lmms::clap
