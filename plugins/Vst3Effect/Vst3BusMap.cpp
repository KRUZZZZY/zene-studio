/*
 * Vst3BusMap.cpp - SDK independent VST3 bus/channel mapping
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

#include "Vst3BusMap.h"

namespace lmms::vst3
{

auto mapBuses(const std::vector<BusDescriptor>& buses) -> BusLayout
{
	BusLayout layout;
	bool seenInputBus = false;

	for (const auto& bus : buses)
	{
		if (!bus.active || bus.channelCount <= 0)
		{
			continue;
		}

		if (bus.direction == BusDescriptor::Direction::Input)
		{
			layout.inputBusChannels.push_back(bus.channelCount);
			layout.inputs += bus.channelCount;
			if (seenInputBus || bus.type == BusDescriptor::Type::Aux)
			{
				layout.hasSideChain = true;
			}
			seenInputBus = true;
		}
		else
		{
			layout.outputBusChannels.push_back(bus.channelCount);
			layout.outputs += bus.channelCount;
		}
	}

	return layout;
}

} // namespace lmms::vst3
