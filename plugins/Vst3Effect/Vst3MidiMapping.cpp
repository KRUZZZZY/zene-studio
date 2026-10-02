/*
 * Vst3MidiMapping.cpp - MIDI controllers to a VST3 plug-in.s parameters (IMidiMapping)
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

#include "Vst3MidiMapping.h"

#include "Midi.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace lmms::vst3
{

void MidiControllerMap::build(IEditController* controller, std::int32_t busIndex)
{
	m_params.fill(Unmapped);
	m_mapped = 0;
	if (controller == nullptr || busIndex < 0) { return; }
	FUnknownPtr<IMidiMapping> mapping(controller);
	if (!mapping) { return; }
	for (int channel = 0; channel < 16; ++channel)
	{
		for (int number = 0; number < Controllers; ++number)
		{
			ParamID id = 0;
			if (mapping->getMidiControllerAssignment(busIndex, static_cast<int16>(channel),
					static_cast<CtrlNumber>(number), id) == kResultTrue)
			{
				m_params[static_cast<std::size_t>(channel * Controllers + number)] = id;
				++m_mapped;
			}
		}
	}
}

auto MidiControllerMap::apply(const MidiEventIn& midi, IParameterChanges* changes) const -> bool
{
	int number = -1;
	ParamValue value = 0.0;
	switch (midi.type)
	{
	case MidiControlChange: number = midi.data0 & 0x7f; value = midi.data1 / 127.0; break;
	case MidiChannelPressure: number = AfterTouch; value = midi.data0 / 127.0; break;
	case MidiPitchBend: number = PitchBend; value = ((midi.data1 << 7) | midi.data0) / 16383.0; break;
	default: return false;
	}
	const std::uint32_t id = m_params[static_cast<std::size_t>((midi.channel & 0x0f) * Controllers + number)];
	if (id == Unmapped || changes == nullptr) { return false; }
	int32 queueIndex = 0;
	if (IParamValueQueue* queue = changes->addParameterData(static_cast<ParamID>(id), queueIndex))
	{
		int32 pointIndex = 0;
		// Block rate: the change is a point at the request's first frame, as host parameter
		// changes already are (HostedPlugin::process step 0).
		queue->addPoint(0, value, pointIndex);
	}
	return true;
}

} // namespace lmms::vst3
