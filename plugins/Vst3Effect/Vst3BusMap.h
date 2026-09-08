/*
 * Vst3BusMap.h - SDK independent VST3 bus/channel mapping
 *
 * Copyright (c) 2026 LMMS contributors
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

#ifndef LMMS_VST3_BUS_MAP_H
#define LMMS_VST3_BUS_MAP_H

#include <vector>

#include <QString>

namespace lmms::vst3
{

//! SDK independent description of one VST3 audio bus
struct BusDescriptor
{
	enum class Direction
	{
		Input,
		Output
	};

	enum class Type
	{
		Main,
		Aux
	};

	QString name;
	Direction direction = Direction::Input;
	Type type = Type::Main;
	int channelCount = 0;
	bool active = true;
};

//! Result of flattening a VST3 bus layout onto the LMMS AudioPorts transport.
//!
//! The AudioPorts transport (Part B) has a single set of input channels and a
//! single set of output channels, so every active VST3 bus is summed into the
//! corresponding direction. `inputBusChannels` / `outputBusChannels` keep the
//! per-bus split so the host can partition the planar buffers again when it
//! feeds the plug-in.
struct BusLayout
{
	std::vector<int> inputBusChannels;
	std::vector<int> outputBusChannels;
	int inputs = 0;
	int outputs = 0;
	//! true if there is an active auxiliary input bus (e.g. a side-chain)
	bool hasSideChain = false;

	auto operator==(const BusLayout& other) const -> bool
	{
		return inputBusChannels == other.inputBusChannels
			&& outputBusChannels == other.outputBusChannels
			&& inputs == other.inputs && outputs == other.outputs
			&& hasSideChain == other.hasSideChain;
	}
};

//! Flattens the active audio buses of a plug-in into a BusLayout.
//!
//! Inactive buses are ignored. Auxiliary input buses (anything but the first
//! active input bus) are reported as side-chain inputs.
auto mapBuses(const std::vector<BusDescriptor>& buses) -> BusLayout;

} // namespace lmms::vst3

#endif // LMMS_VST3_BUS_MAP_H
