/*
 * ClapBusMap.h - maps CLAP audio ports onto the AudioPorts transport
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

#ifndef LMMS_CLAP_BUS_MAP_H
#define LMMS_CLAP_BUS_MAP_H

#include <cstdint>
#include <vector>

#include <QString>

namespace lmms::clap
{

//! Direction of a CLAP audio port relative to the plug-in.
enum class PortDirection
{
	Input,
	Output
};

/*!
 * One port as reported by clap.audio-ports. Deliberately free of CLAP types so
 * the mapping can be unit tested without loading a plug-in.
 */
struct PortDescriptor
{
	//! plug-in scoped port id (clap_audio_port_info_t::id)
	std::uint32_t id = 0;
	PortDirection direction = PortDirection::Input;
	QString name;
	//! "mono", "stereo" or a plug-in specific port type
	QString portType;
	int channelCount = 0;
	//! clap_audio_ports: CLAP_AUDIO_PORT_IS_MAIN
	bool isMain = false;
};

/*!
 * Flat layout the AudioPorts transport consumes: total input/output channel
 * counts plus the per-port partition used to fill the plug-in's planar buffers.
 */
struct PortLayout
{
	int inputs = 0;
	int outputs = 0;
	//! a second input port is treated as a side-chain
	bool hasSideChain = false;
	std::vector<int> inputPortChannels;
	std::vector<int> outputPortChannels;

	auto isValid() const -> bool { return inputs > 0 && outputs > 0; }
};

/*!
 * Maps CLAP audio ports onto the AudioPorts transport layout.
 *
 * - ports with a non-positive channel count are ignored
 * - a port never pushes a direction past MaxChannelsPerAudioBuffer; the excess
 *   channels are dropped so the transport stays within its fixed limits
 * - a second input port marks the layout as having a side-chain
 */
auto mapPorts(const std::vector<PortDescriptor>& ports) -> PortLayout;

} // namespace lmms::clap

#endif // LMMS_CLAP_BUS_MAP_H
