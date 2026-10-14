/*
 * Vst3HostInternals.h - the VST3 host's private state, shared by its translation units
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

#ifndef LMMS_VST3_HOST_INTERNALS_H
#define LMMS_VST3_HOST_INTERNALS_H

// Internal to the VST3 host (Vst3Host.cpp, Vst3HostReports.cpp): moved out of Vst3Host.cpp in
// 0.5.0 so that file stays inside its file-length ratchet, as the CLAP host keeps its state in
// ClapHostInternals.h. Not for other includers: it brings the SDK's names into lmms::vst3.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QString>

#include "Vst3BusMap.h"
#include "Vst3ComponentHandler.h"
#include "Vst3Host.h"
#include "Vst3MidiMapping.h"
#include "Vst3MidiQueue.h"
#include "Vst3ParamDescriptor.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

namespace lmms::vst3
{

using namespace Steinberg;
using namespace Steinberg::Vst;

struct HostedPlugin::Impl
{
	VST3::Hosting::Module::Ptr module;
	std::unique_ptr<PlugProvider> provider;
	IPtr<IComponent> component;
	IPtr<IAudioProcessor> processor;
	IPtr<IEditController> controller;
	IPtr<IComponentHandler> handler;
	//! Called when the plug-in reports kLatencyChanged (set by the effect that hosts it).
	std::function<void()> onLatencyChanged;
	//! The handler `handler` holds, to clear its callback at teardown (non-owning).
	Vst3ComponentHandler* handlerImpl = nullptr;
	bool singleComponent = false;

	QString className;
	QString vendor;
	bool instrument = false;

	//! The input event bus this host drives, or -1. Only ever set for an
	//! instrument: an effect's event buses are left exactly as they were.
	int32 eventInputBusIndex = -1;
	//! true when MIDI pushed into midiQueue reaches the plug-in. Atomic: the MIDI producers read it
	//! while prepare()/release() write it (round 6).
	std::atomic<bool> midiEnabled{false};
	//! pushMidiEvent() calls in flight; release() waits for none before it resets midiQueue.
	std::atomic<int> midiProducers{0};
	//! MIDI path -> audio thread. Bounded, lock free, never grows.
	MidiQueue midiQueue;
	//! Events dropped because midiQueue was full (counted by every producer)
	std::atomic<std::uint64_t> droppedMidi{0};
	//! The event list handed to the plug-in as ProcessData::inputEvents.
	//! Sized once, in its constructor; cleared and refilled per block.
	EventList inputEvents{kMaxMidiEventsPerBlock};
	//! The slice of inputEvents that belongs to the chunk being processed, with
	//! its sample offsets rebased to that chunk's start. Sized once, in its
	//! constructor: process() never allocates (see runChunk()).
	EventList chunkEvents{kMaxMidiEventsPerBlock};
	//! ProcessData::outputEvents (E7), and what the whole request's chunks wrote, as MIDI
	EventList outputEvents{kMaxMidiEventsPerBlock};
	std::array<MidiEventIn, kMaxMidiEventsPerBlock> blockOutput{};
	int blockOutputCount = 0;
	//! Output notes that did not fit blockOutput; atomic because diagnostics reads it off the audio thread.
	std::atomic<std::uint64_t> droppedOutput{0};
	//! Audio thread scratch for ordering the drained events by sample offset
	std::vector<MidiEventIn> midiEvents;

	std::vector<Vst3ParamDescriptor> params;
	std::vector<std::pair<std::uint32_t, std::size_t>> indexById; // sorted by id
	std::unique_ptr<std::atomic<float>[]> paramValues;
	std::vector<float> lastParamValues;

	BusLayout layout;
	std::vector<BusDescriptor> inputBusDescs;
	std::vector<BusDescriptor> outputBusDescs;

	//! 0.5.0 MX-120: one note-expression type the plug-in declares on its event input bus.
	struct NoteExpression
	{
		std::uint32_t typeId = 0;
		QString title;
		std::uint32_t parameter = 0;   //!< its associated parameter, when hasParameter
		bool hasParameter = false;
	};
	std::vector<NoteExpression> noteExpressions;  //!< read by refreshNoteExpressions(), GUI thread

	// audio thread scratch, all allocated in prepare()
	std::vector<AudioBusBuffers> inputBuses;
	std::vector<AudioBusBuffers> outputBuses;
	std::vector<std::vector<float*>> inputChannelPtrs;
	std::vector<std::vector<float*>> outputChannelPtrs;
	std::vector<float> silence;
	std::vector<float> scratchOutput;

	//! The whole request's parameter changes (host changes at 0, mapped controllers at their frame).
	ParameterChanges inputParamChanges;
	//! The slice of inputParamChanges that belongs to the chunk being processed, rebased to the
	//! chunk's start (sliceParameterChanges). Sized in prepare(), as inputParamChanges is.
	ParameterChanges chunkParamChanges;
	MidiControllerMap controllerMap;  //!< E10, built in prepare()
	ProcessData processData{};
	ProcessContext context{};

	//! One chunk of one process() request: the pointer mapping, the parameter
	//! and event delivery and the processor call for [offset, offset + chunk).
	//! `chunk` is never larger than maxBlockSize, `lastChunk` is true for the
	//! chunk the request ends in (see the event boundary rule there). See the
	//! over-run and tail rule documented on HostedPlugin::process() in
	//! Vst3Host.h.
	void runChunk(const float* const* inputs, float* const* outputs, int numInputs,
		int numOutputs, int offset, int chunk, bool firstChunk, bool lastChunk);
	// runChunk's three steps (split for the complexity target, unchanged).
	void sliceEvents(int offset, int chunk, bool lastChunk);
	void mapBuses(const float* const* inputs, float* const* outputs, int numInputs, int numOutputs, int offset, int chunk);
	void collectOutputEvents(int offset);

	double sampleRate = 0.0;
	int maxBlockSize = 0;
	bool prepared = false;
	std::int64_t continuousSamples = 0;
};

} // namespace lmms::vst3

#endif // LMMS_VST3_HOST_INTERNALS_H
