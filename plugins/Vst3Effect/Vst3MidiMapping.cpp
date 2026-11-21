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
	m_deferred.fill(Deferred{});
	m_deferredCount = 0;
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

auto MidiControllerMap::apply(const MidiEventIn& midi, IParameterChanges* changes, std::int32_t sampleOffset)
	-> bool
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
	const auto key = static_cast<std::size_t>((midi.channel & 0x0f) * Controllers + number);
	const std::uint32_t id = m_params[key];
	if (id == Unmapped || changes == nullptr) { return false; }
	int32 queueIndex = 0;
	if (IParamValueQueue* queue = changes->addParameterData(static_cast<ParamID>(id), queueIndex))
	{
		// 0.5.0 W4: sample accurate - the point sits at the controller's own frame, not at the
		// request's first frame (HostedPlugin::Impl::runChunk slices it into the right chunk).
		if (!addBoundedPoint(queue, sampleOffset, value)) { defer(key, sampleOffset, value); }
	}
	return true;
}

void MidiControllerMap::defer(std::size_t key, std::int32_t sampleOffset, double value)
{
	// Two controllers can be mapped to one parameter (an alias) and share its queue, so the
	// pending point of that parameter is the newest one, whichever controller carried it.
	const std::uint32_t parameter = m_params[key];
	for (int n = 0; n < m_deferredCount; ++n)
	{
		const auto deferredKey = m_deferredKeys[static_cast<std::size_t>(n)];
		if (m_params[deferredKey] != parameter) { continue; }
		Deferred& deferred = m_deferred[deferredKey];
		deferred.sampleOffset = sampleOffset;
		deferred.value = value;
		return;
	}
	Deferred& deferred = m_deferred[key];
	if (!deferred.pending)
	{
		deferred.pending = true;
		m_deferredKeys[static_cast<std::size_t>(m_deferredCount++)] = static_cast<std::uint16_t>(key);
	}
	// Events arrive in time order (drainMidiIntoEventList sorts them), so this is the newest point.
	deferred.sampleOffset = sampleOffset;
	deferred.value = value;
}

void MidiControllerMap::flush(IParameterChanges* changes)
{
	for (int n = 0; n < m_deferredCount; ++n)
	{
		const auto key = m_deferredKeys[static_cast<std::size_t>(n)];
		Deferred& deferred = m_deferred[key];
		int32 queueIndex = 0;
		// The last slot addBoundedPoint kept free; the SDK queue holds it without growing.
		if (changes != nullptr)
		{
			if (IParamValueQueue* queue = changes->addParameterData(static_cast<ParamID>(m_params[key]), queueIndex);
				queue != nullptr && queue->getPointCount() < kMaxPointsPerParameter)
			{
				queue->addPoint(deferred.sampleOffset, deferred.value, queueIndex);
			}
		}
		deferred.pending = false;
	}
	m_deferredCount = 0;
}

bool addBoundedPoint(IParamValueQueue* queue, std::int32_t sampleOffset, double value)
{
	if (queue->getPointCount() >= kMaxPointsPerParameter - 1) { return false; }
	int32 pointIndex = 0;
	queue->addPoint(sampleOffset, value, pointIndex);
	return true;
}

namespace
{

//! One parameter's slice of sliceParameterChanges: its points in [begin, end), rebased to begin.
void sliceQueue(IParamValueQueue& queue, IParameterChanges& to, int32 begin, int32 end)
{
	IParamValueQueue* target = nullptr;
	const int32 points = queue.getPointCount();
	for (int32 p = 0; p < points; ++p)
	{
		int32 sampleOffset = 0;
		ParamValue value = 0.0;
		if (queue.getPoint(p, sampleOffset, value) != kResultTrue || sampleOffset < begin || sampleOffset >= end)
		{
			continue;
		}
		int32 index = 0;
		if (target == nullptr) { target = to.addParameterData(queue.getParameterId(), index); }
		if (target == nullptr) { return; }
		target->addPoint(sampleOffset - begin, value, index);
	}
}

} // namespace

void sliceParameterChanges(IParameterChanges& from, IParameterChanges& to, std::int32_t offset, std::int32_t chunk)
{
	const int32 count = from.getParameterCount();
	for (int32 i = 0; i < count; ++i)
	{
		if (IParamValueQueue* queue = from.getParameterData(i)) { sliceQueue(*queue, to, offset, offset + chunk); }
	}
}

} // namespace lmms::vst3
