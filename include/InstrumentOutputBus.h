/*
 * InstrumentOutputBus.h - 0.5.0 MX-114: one audio output bus of a hosted
 *                         instrument, as its host reports it.
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

#ifndef LMMS_INSTRUMENT_OUTPUT_BUS_H
#define LMMS_INSTRUMENT_OUTPUT_BUS_H

#include <QString>

namespace lmms
{

/*! One audio output bus of a hosted instrument. Plain data with no engine dependency, so the hosts' bus
 *  maps (ClapBusMap, Vst3BusMap) build it in their SDK-free, separately tested units. */
struct InstrumentOutputBus
{
	QString name;
	int channels = 0;       //!< the bus's own channel count (1 = mono, 2 = stereo, ...)
	int firstChannel = -1;  //!< its first channel in the plug-in's flat output; -1 while the bus is inactive
};

} // namespace lmms

#endif // LMMS_INSTRUMENT_OUTPUT_BUS_H
