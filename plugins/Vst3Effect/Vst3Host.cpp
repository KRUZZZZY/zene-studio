/*
 * Vst3Host.cpp - in-process VST3 host
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

#include "Vst3Host.h"

#include <functional>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <thread>

#include <QDebug>

#include "PluginHostChunking.h"
#include "Vst3HostInternals.h"
#include "Vst3EditorSession.h"
#include "Vst3MidiEvent.h"
#include "Vst3MidiMapping.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace lmms::vst3
{

namespace
{

constexpr int MaxHostedBlockSize = 1'048'576;

auto validPrepareRequest(double sampleRate, int maxBlockSize) -> bool
{
	return std::isfinite(sampleRate) && sampleRate > 0.0 && maxBlockSize > 0 &&
		maxBlockSize <= MaxHostedBlockSize;
}

void setError(QString* error, const QString& message)
{
	if (error) { *error = message; }
}

//! setError, then false: the one refusal every load step returns.
auto refuse(QString* error, const QString& why) -> bool
{
	setError(error, why);
	return false;
}

void clearCallerOutputs(float* const* outputs, int count, int frames)
{
	if (outputs == nullptr || frames <= 0) { return; }
	const auto bytes = static_cast<std::size_t>(frames) * sizeof(float);
	for (int channel = 0; channel < count; ++channel)
	{
		if (outputs[channel] != nullptr) { std::memset(outputs[channel], 0, bytes); }
	}
}

auto fromString128(const String128 text) -> QString
{
	return QString::fromUtf16(reinterpret_cast<const char16_t*>(text)).trimmed();
}

/**
 * The host application object handed to every plug-in via
 * IComponent::initialize(). PlugProvider forwards
 * PluginContextFactory::instance()'s context, which must be set before the
 * first PlugProvider is constructed.
 */
auto hostContext() -> IPtr<FUnknown>
{
	static IPtr<HostApplication> application = [] {
		IPtr<HostApplication> app = owned(new HostApplication);
		PluginContextFactory::instance().setPluginContext(app);
		return app;
	}();
	return application;
}

auto writeToStream(MemoryStream& stream, const QByteArray& data) -> bool
{
	if (data.isEmpty()) { return true; }
	stream.setSize(static_cast<TSize>(data.size()));
	int32 written = 0;
	const auto result = stream.write(const_cast<char*>(data.constData()),
			   static_cast<int32>(data.size()), &written);
	// Plug-ins read their state from the start of the stream; write() left the
	// cursor at the end.
	stream.seek(0, IBStream::kIBSeekSet, nullptr);
	return result == kResultOk && written == data.size();
}

auto readFromStream(IBStream* stream) -> QByteArray
{
	QByteArray out;
	if (!stream) { return out; }
	if (stream->seek(0, IBStream::kIBSeekSet, nullptr) != kResultOk) { return out; }
	char buffer[4096];
	while (true)
	{
		int32 numRead = 0;
		const auto result = stream->read(buffer, sizeof(buffer), &numRead);
		if (numRead > 0) { out.append(buffer, static_cast<int>(numRead)); }
		if (result != kResultOk || numRead <= 0) { break; }
	}
	return out;
}

} // namespace


HostedPlugin::HostedPlugin() :
	m_impl{std::make_unique<Impl>()}
{
}

HostedPlugin::~HostedPlugin()
{
	auto& d = *m_impl;
	m_editor.reset();  // R4.2: the editor goes before the controller does
	if (d.handlerImpl != nullptr) { d.handlerImpl->detach(); }
	d.handlerImpl = nullptr;
	release();
	d.handler = nullptr;
	d.processor = nullptr;
	d.controller = nullptr;
	d.component = nullptr;
	d.provider.reset();
	d.module.reset();
}

auto listClasses(const QString& modulePath, QString* error) -> std::vector<Vst3ClassInfo>
{
	std::vector<Vst3ClassInfo> result;
	std::string errorText;
	auto module = VST3::Hosting::Module::create(modulePath.toStdString(), errorText);
	if (!module)
	{
		setError(error, QString::fromStdString(errorText));
		return result;
	}
	for (const auto& info : module->getFactory().classInfos())
	{
		if (info.category() != kVstAudioEffectClass) { continue; }
		Vst3ClassInfo entry;
		entry.name = QString::fromStdString(info.name());
		entry.category = QString::fromStdString(info.category());
		entry.subCategories = QString::fromStdString(info.subCategoriesString());
		entry.vendor = QString::fromStdString(info.vendor());
		entry.isInstrument = entry.subCategories.contains("Instrument", Qt::CaseInsensitive);
		result.push_back(std::move(entry));
	}
	return result;
}

namespace
{

//! Every audio bus of @a component in @a direction, described into @a own (the input or output list)
//! and @a all (the layout's list, inputs first).
void describeAudioBuses(IComponent* component, BusDirection direction, std::vector<BusDescriptor>* own,
	std::vector<BusDescriptor>* all)
{
	for (int32 i = 0; i < component->getBusCount(kAudio, direction); ++i)
	{
		BusInfo info{};
		if (component->getBusInfo(kAudio, direction, i, info) != kResultOk) { continue; }
		BusDescriptor bus;
		bus.name = fromString128(info.name);
		bus.direction = direction == kInput ? BusDescriptor::Direction::Input : BusDescriptor::Direction::Output;
		bus.type = info.busType == kMain ? BusDescriptor::Type::Main : BusDescriptor::Type::Aux;
		bus.channelCount = info.channelCount;
		bus.active = (info.flags & BusInfo::kDefaultActive) != 0;
		own->push_back(bus);
		all->push_back(std::move(bus));
	}
}

//! An instrument whose output buses are all off by default still plays into the first one, as its
//! event input does (resolveEventInputBus): Vitalium (DISTRHO-Ports) reports its only output that way,
//! and with no bus mapped it renders nothing (cluster stress-2). Returns whether @a outputs changed.
auto preferFirstOutputBus(std::vector<BusDescriptor>* outputs) -> bool
{
	const auto active = [](const BusDescriptor& bus) { return bus.active && bus.channelCount > 0; };
	if (outputs->empty() || std::any_of(outputs->begin(), outputs->end(), active)) { return false; }
	if (outputs->front().channelCount <= 0) { return false; }
	outputs->front().active = true;
	return true;
}

//! Activates the audio buses of @a component in @a direction that the layout maps. kDefaultActive only
//! asks the host to do it ("activateBus call is requested", ivstcomponent.h): a bus starts inactive, and
//! a JUCE plug-in writes its audio only into a host-activated bus (cluster stress-2).
void activateMappedAudioBuses(IComponent* component, BusDirection direction, const std::vector<BusDescriptor>& buses)
{
	for (std::size_t i = 0; i < buses.size(); ++i)
	{
		if (!buses[i].active || buses[i].channelCount <= 0) { continue; }
		component->activateBus(kAudio, direction, static_cast<int32>(i), true);
	}
}

} // namespace

auto HostedPlugin::load(const QString& modulePath, const QString& classId, QString* error) -> bool
{
	auto& d = *m_impl;
	std::string errorText;
	d.module = VST3::Hosting::Module::create(modulePath.toStdString(), errorText);
	if (!d.module) { return refuse(error, QString::fromStdString(errorText)); }

	const auto& factory = d.module->getFactory();
	bool found = false;
	VST3::Hosting::ClassInfo classInfo;
	for (const auto& info : factory.classInfos())
	{
		if (info.category() != kVstAudioEffectClass) { continue; }
		if (!classId.isEmpty() && QString::fromStdString(info.name()) != classId) { continue; }
		classInfo = info;
		found = true;
		break;
	}
	if (!found) { return refuse(error, QString("no audio class '%1' in %2").arg(classId, modulePath)); }

	hostContext();
	d.provider = std::make_unique<PlugProvider>(factory, classInfo);
	if (!d.provider->initialize()) { return refuse(error, QString("failed to initialize '%1'").arg(classId)); }
	d.component = d.provider->getComponentPtr();
	d.controller = d.provider->getControllerPtr();
	FUnknownPtr<IAudioProcessor> processor{d.component.get()};
	d.processor = processor;
	if (!d.component || !d.processor) { return refuse(error, QString("'%1' is not an audio processor").arg(classId)); }

	FUnknown* componentAsController = nullptr;
	d.singleComponent = d.component->queryInterface(IEditController::iid,
				  reinterpret_cast<void**>(&componentAsController)) == kResultOk &&
		componentAsController != nullptr;
	if (componentAsController) { componentAsController->release(); }

	d.className = QString::fromStdString(classInfo.name());
	d.vendor = QString::fromStdString(classInfo.vendor());
	const auto subCategories = QString::fromStdString(classInfo.subCategoriesString());
	d.instrument = subCategories.contains("Instrument", Qt::CaseInsensitive);

	// bus layout
	std::vector<BusDescriptor> buses;
	if (d.component)
	{
		describeAudioBuses(d.component.get(), kInput, &d.inputBusDescs, &buses);
		describeAudioBuses(d.component.get(), kOutput, &d.outputBusDescs, &buses);
	}
	if (d.instrument && preferFirstOutputBus(&d.outputBusDescs))
	{
		buses = d.inputBusDescs;
		buses.insert(buses.end(), d.outputBusDescs.begin(), d.outputBusDescs.end());
	}
	d.layout = mapBuses(buses);

	// event input bus - the MIDI path
	//
	// An instrument receives MIDI as events on a kEvent input bus, so the host
	// has to know which bus to fill. The policy for a plug-in that declares
	// more than one: drive the first bus that is active by default, and
	// deactivate the rest (Vst3MidiEvent.cpp, docs/VST3-INSTRUMENT-HOSTING.md).
	// Effects are deliberately left alone - their event buses and
	// ProcessData::inputEvents are exactly what they were before this path
	// existed, so an existing project's render cannot change because of it.
	d.eventInputBusIndex = d.instrument ? resolveEventInputBus(d.component.get()) : -1;
	if (d.eventInputBusIndex < 0) { d.midiQueue.reset(); }

	// parameters
	if (d.controller)
	{
		const auto count = d.controller->getParameterCount();
		d.params.reserve(static_cast<std::size_t>(count));
		for (int32 i = 0; i < count; ++i)
		{
			ParameterInfo info{};
			if (d.controller->getParameterInfo(i, info) != kResultOk) { continue; }
			Vst3ParamDescriptor desc;
			desc.id = static_cast<std::uint32_t>(info.id);
			desc.title = fromString128(info.title);
			desc.shortTitle = fromString128(info.shortTitle);
			desc.units = fromString128(info.units);
			desc.stepCount = info.stepCount;
			desc.defaultNormalized = sanitizedNormalized(
				d.controller->getParamNormalized(info.id), 0.f);
			desc.readOnly = (info.flags & ParameterInfo::kIsReadOnly) != 0;
			desc.hidden = (info.flags & ParameterInfo::kIsHidden) != 0;
			desc.stepped = info.stepCount > 0;
			desc.bypass = (info.flags & ParameterInfo::kIsBypass) != 0;
			d.params.push_back(std::move(desc));
		}
		d.indexById.resize(d.params.size());
		for (std::size_t i = 0; i < d.params.size(); ++i)
		{
			d.indexById[i] = {d.params[i].id, i};
		}
		std::sort(d.indexById.begin(), d.indexById.end());
		d.paramValues = std::make_unique<std::atomic<float>[]>(d.params.size());
		d.paramGestures = std::make_unique<PluginParamGesture[]>(d.params.size());
		for (std::size_t i = 0; i < d.params.size(); ++i)
		{
			d.paramValues[i].store(d.params[i].defaultNormalized, std::memory_order_relaxed);
		}
		// What the processor already holds: the values the plug-in reported. process() sends a parameter
		// only when the host's value moves away from the last one sent, across prepare()s too. Sending
		// every reported value on the first block pushed values some plug-ins report but do not play over
		// their patch (Dexed's controller says 0 for Output and Cutoff), and Dexed and Surge XT then
		// rendered silence (cluster stress-2).
		d.lastParamValues.assign(d.params.size(), 0.f);
		for (std::size_t i = 0; i < d.params.size(); ++i)
		{
			d.lastParamValues[i] = d.params[i].defaultNormalized;
		}
	}

	if (d.handlerImpl != nullptr) { d.handlerImpl->detach(); }
	auto* handler = new Vst3ComponentHandler(d.paramValues.get(), &d.indexById, d.paramGestures.get());
	handler->setOnLatencyChanged(d.onLatencyChanged);
	d.handlerImpl = handler;
	d.handler = owned(handler);
	if (d.controller) { d.controller->setComponentHandler(d.handler); }

	return true;
}

auto HostedPlugin::saveState(QByteArray* componentState, QByteArray* controllerState) -> bool
{
	auto& d = *m_impl;
	bool ok = true;
	if (componentState && d.component)
	{
		MemoryStream stream;
		// kNotImplemented means "this plug-in keeps no state", which is not
		// an error (EditController's base implementation returns it too).
		const auto result = d.component->getState(&stream);
		ok = (result == kResultOk || result == kNotImplemented) && ok;
		*componentState = readFromStream(&stream);
	}
	if (controllerState && d.controller && !d.singleComponent)
	{
		MemoryStream stream;
		const auto result = d.controller->getState(&stream);
		ok = (result == kResultOk || result == kNotImplemented) && ok;
		*controllerState = readFromStream(&stream);
	}
	return ok;
}

auto HostedPlugin::loadState(const QByteArray& componentState,
	const QByteArray& controllerState) -> bool
{
	auto& d = *m_impl;
	bool ok = true;
	if (!componentState.isEmpty() && d.component)
	{
		MemoryStream stream;
		if (!writeToStream(stream, componentState)) { return false; }
		const auto componentResult = d.component->setState(&stream);
		ok = (componentResult == kResultOk || componentResult == kNotImplemented) && ok;
		if (!d.singleComponent && d.controller)
		{
			stream.seek(0, IBStream::kIBSeekSet, nullptr);
			const auto controllerResult = d.controller->setComponentState(&stream);
			ok = (controllerResult == kResultOk || controllerResult == kNotImplemented) && ok;
		}
	}
	if (!controllerState.isEmpty() && d.controller && !d.singleComponent)
	{
		MemoryStream stream;
		if (!writeToStream(stream, controllerState)) { return false; }
		const auto result = d.controller->setState(&stream);
		ok = (result == kResultOk || result == kNotImplemented) && ok;
	}
	if (d.controller)
	{
		for (std::size_t i = 0; i < d.params.size(); ++i)
		{
			// A restored value the plug-in reports is clamped like any other one, and
			// a value with no meaning leaves the array's own value alone (cluster 3625).
			const auto keep = d.paramValues[i].load(std::memory_order_relaxed);
			d.paramValues[i].store(sanitizedNormalized(
					static_cast<double>(d.controller->getParamNormalized(
						static_cast<ParamID>(d.params[i].id))),
					keep),
				std::memory_order_relaxed);
		}
	}
	return ok;
}

auto HostedPlugin::prepare(double sampleRate, int maxBlockSize, QString* error) -> bool
{
	auto& d = *m_impl;
	release();
	if (!d.component || !d.processor) { return refuse(error, "no plug-in loaded"); }
	if (!validPrepareRequest(sampleRate, maxBlockSize))
	{
		return refuse(error, "invalid sample rate or block size");
	}

	ProcessSetup setup{};
	setup.processMode = kRealtime;
	setup.symbolicSampleSize = kSample32;
	setup.maxSamplesPerBlock = maxBlockSize;
	setup.sampleRate = sampleRate;
	if (d.processor->setupProcessing(setup) != kResultOk) { return refuse(error, "setupProcessing() failed"); }
	// The buses ProcessData carries, activated while the component is still inactive: after
	// setupProcessing() and before setActive(true), on every prepare.
	activateMappedAudioBuses(d.component.get(), kInput, d.inputBusDescs);
	activateMappedAudioBuses(d.component.get(), kOutput, d.outputBusDescs);
	if (d.component->setActive(true) != kResultOk) { return refuse(error, "setActive() failed"); }
	// The SDK's AudioEffect base class returns kNotImplemented here and the
	// SDK's own host ignores the return value (audiohost/audioclient.cpp:
	// "processor->setProcessing (true); // != kResultOk"). Only an explicit
	// kResultFalse is a real failure.
	const auto processingResult = d.processor->setProcessing(true);
	if (processingResult != kResultOk && processingResult != kNotImplemented)
	{
		d.component->setActive(false);
		setError(error, QString("setProcessing() failed (0x%1)")
			.arg(static_cast<quint32>(processingResult), 8, 16, QLatin1Char('0')));
		return false;
	}

	const bool midiEnabled = d.eventInputBusIndex >= 0;
	try
	{
		// pre-allocate every audio thread structure
		d.inputBuses.assign(d.layout.inputBusChannels.size(), AudioBusBuffers{});
		d.inputChannelPtrs.assign(d.layout.inputBusChannels.size(), {});
		for (std::size_t b = 0; b < d.layout.inputBusChannels.size(); ++b)
		{
			d.inputBuses[b].numChannels = d.layout.inputBusChannels[b];
			d.inputBuses[b].silenceFlags = 0;
			d.inputChannelPtrs[b].assign(d.inputBuses[b].numChannels, nullptr);
		}
		d.outputBuses.assign(d.layout.outputBusChannels.size(), AudioBusBuffers{});
		d.outputChannelPtrs.assign(d.layout.outputBusChannels.size(), {});
		for (std::size_t b = 0; b < d.layout.outputBusChannels.size(); ++b)
		{
			d.outputBuses[b].numChannels = d.layout.outputBusChannels[b];
			d.outputBuses[b].silenceFlags = 0;
			d.outputChannelPtrs[b].assign(d.outputBuses[b].numChannels, nullptr);
		}
		d.silence.assign(static_cast<std::size_t>(maxBlockSize), 0.f);
		d.scratchOutput.assign(static_cast<std::size_t>(maxBlockSize), 0.f);
		d.inputParamChanges.setMaxParameters(static_cast<int32>(d.params.size()));
		d.chunkParamChanges.setMaxParameters(static_cast<int32>(d.params.size()));

		// MIDI: the event list is already sized (its constructor did it), the
		// ordering scratch is sized here, and the queue starts empty. All three
		// are pre-allocated so process() never touches the allocator.
		// release() above emptied the queue with the producers held off; they stay off
		// (midiEnabled) until the end of this function.
		d.droppedMidi = 0;
		d.droppedOutput.store(0, std::memory_order_relaxed);
		d.midiEvents.assign(static_cast<std::size_t>(kMaxMidiEventsPerBlock), MidiEventIn{});

		d.processData = ProcessData{};
		d.processData.processMode = kRealtime;
		d.processData.symbolicSampleSize = kSample32;
		d.processData.inputs = d.inputBuses.empty() ? nullptr : d.inputBuses.data();
		d.processData.numInputs = static_cast<int32>(d.inputBuses.size());
		d.processData.outputs = d.outputBuses.empty() ? nullptr : d.outputBuses.data();
		d.processData.numOutputs = static_cast<int32>(d.outputBuses.size());
		d.processData.inputParameterChanges = &d.inputParamChanges;
		d.controllerMap.build(d.controller, d.eventInputBusIndex);
		// Instruments with an input event bus get one; effects keep nullptr, which
		// is what every effect has always been given.
		d.processData.inputEvents = midiEnabled ? &d.inputEvents : nullptr;
		d.processData.processContext = &d.context;
	}
	catch (const std::exception&)
	{
		d.processor->setProcessing(false);
		d.component->setActive(false);
		release();
		return refuse(error, "failed to allocate VST3 host buffers");
	}

	// Transport state is reported per block through setTransportPlaying() and
	// setTempo(); until then no state flag is valid.
	d.context = ProcessContext{};
	d.context.sampleRate = sampleRate;
	d.context.state = 0;
	d.continuousSamples = 0;

	d.sampleRate = sampleRate;
	d.maxBlockSize = maxBlockSize;
	d.midiEnabled = midiEnabled;
	d.prepared = true;
	return true;
}

void HostedPlugin::release()
{
	auto& d = *m_impl;
	if (d.prepared)
	{
		if (d.processor) { d.processor->setProcessing(false); }
		if (d.component) { d.component->setActive(false); }
	}
	d.prepared = false;
	d.sampleRate = 0.0;
	d.maxBlockSize = 0;
	d.inputBuses.clear();
	d.outputBuses.clear();
	d.inputChannelPtrs.clear();
	d.outputChannelPtrs.clear();
	d.silence.clear();
	d.scratchOutput.clear();
	// No stale MIDI may survive into the next prepare(): a note-on that was
	// queued but never processed would otherwise sound on the first block
	// after a sample-rate change, or after the project is reloaded.
	d.midiEnabled = false;
	while (d.midiProducers.load() != 0) { std::this_thread::yield(); }
	d.midiQueue.reset();
	d.inputEvents.clear();
	d.deferredEvents.clear();
	d.midiEvents.clear();
}

auto HostedPlugin::isPrepared() const -> bool
{
	return m_impl->prepared;
}

auto HostedPlugin::preparedSampleRate() const -> double
{
	return m_impl->sampleRate;
}

void HostedPlugin::setTempo(double bpm)
{
	auto& context = m_impl->context;
	context.tempo = bpm;
	context.state |= static_cast<uint32>(ProcessContext::kTempoValid);
}

void HostedPlugin::setTransportPlaying(bool playing)
{
	auto& state = m_impl->context.state;
	if (playing) { state |= static_cast<uint32>(ProcessContext::kPlaying); }
	else { state &= ~static_cast<uint32>(ProcessContext::kPlaying); }
}

void HostedPlugin::process(const float* const* inputs, float* const* outputs,
	int numInputs, int numOutputs, int frames)
{
	auto& d = *m_impl;
	d.blockOutputCount = 0;
	clearCallerOutputs(outputs, numOutputs, frames);
	if (!d.prepared || !d.processor || frames <= 0) { return; }

	// Chunking counters, in the CORE (include/PluginHostChunking.h): the audio
	// path increments, the control surface reads through `plugin.host_chunking`.
	// Relaxed atomics, no allocation, no lock, no per-frame traffic.
	control::PluginHostChunkingCounters& counters = control::vst3HostChunkingCounters();
	counters.recordRequest(frames, d.maxBlockSize);

	// 0. parameter changes: queued once, for the whole request. A host change is
	//    a time-0 point (so it reaches the chunk that starts the request); a
	//    mapped MIDI controller (step 1) is a point at its own frame. runChunk()
	//    slices the list, so each point is delivered once, in its own chunk.
	d.inputParamChanges.clearQueue();
	int32 queueIndex = 0;
	for (std::size_t i = 0; i < d.params.size(); ++i)
	{
		const auto value = d.paramValues[i].load(std::memory_order_relaxed);
		if (value == d.lastParamValues[i]) { continue; }
		d.lastParamValues[i] = value;
		if (auto* queue = d.inputParamChanges.addParameterData(
				static_cast<ParamID>(d.params[i].id), queueIndex))
		{
			queue->addPoint(0, value, queueIndex);
		}
	}

	// 1. MIDI: the queue is drained ONCE, with the whole request as the horizon,
	//    and runChunk() then slices the resulting list per chunk - an event is
	//    delivered in the chunk its sample offset falls in, rebased to that
	//    chunk's start, so no event is dropped and none is delivered twice.
	if (d.processData.inputEvents != nullptr)
	{
		d.inputEvents.clear();
		// Events carried from the previous request belong at this request's first sample, and go
		// in before the events drained now: a note-off left at the previous boundary must release
		// its note before a note-on at offset 0 starts the next one. The carried count comes out
		// of this request's event-list room, so a drain can never push a carried event out.
		const int32 deferred = d.prependDeferredEvents();
		drainMidiIntoEventList(d.midiQueue, d.midiEvents, d.inputEvents,
			d.eventInputBusIndex, frames, &d.controllerMap, &d.inputParamChanges,
			kMaxMidiEventsPerBlock - deferred);
	}

	// 2. chunks: every chunk but the last is exactly maxBlockSize long and the
	//    last carries the remainder, so a request that is not a multiple of the
	//    prepared block is processed whole - as many chunks as it takes, with
	//    the plug-in never handed a buffer shorter than the frames it is asked
	//    for and never asked for more than the block size it was prepared with.
	int offset = 0;
	while (offset < frames)
	{
		const int chunk = std::min(frames - offset, d.maxBlockSize);
		d.runChunk(inputs, outputs, numInputs, numOutputs, offset, chunk, offset == 0,
			offset + chunk == frames);
		counters.recordChunk();
		offset += chunk;
	}

	// The next request drains into the full list again.
	if (d.midiEnabled) { d.processData.inputEvents = &d.inputEvents; }
}

auto HostedPlugin::Impl::prependDeferredEvents() -> int
{
	const int32 count = deferredEvents.getEventCount();
	for (int32 i = 0; i < count; ++i)
	{
		Event event{};
		if (deferredEvents.getEvent(i, event) != kResultOk) { continue; }
		event.sampleOffset = 0;
		inputEvents.addEvent(event);
	}
	deferredEvents.clear();
	return count;
}

void HostedPlugin::Impl::runChunk(const float* const* inputs, float* const* outputs,
	int numInputs, int numOutputs, int offset, int chunk, bool firstChunk, bool lastChunk)
{
	// 1a. parameter changes, sliced as the events are (0.5.0 W4): a point belongs
	//     to the chunk its sample offset falls in, rebased to that chunk's start,
	//     so a point never lands past numSamples. A request processed in one
	//     chunk hands the plug-in the request's own list.
	processData.inputParameterChanges = &inputParamChanges;
	if (!(firstChunk && lastChunk))
	{
		chunkParamChanges.clearQueue();
		sliceParameterChanges(inputParamChanges, chunkParamChanges, offset, chunk);
		processData.inputParameterChanges = &chunkParamChanges;
	}

	sliceEvents(offset, chunk, lastChunk);
	mapBuses(inputs, outputs, numInputs, numOutputs, offset, chunk);

	// 3. the chunk itself
	processData.numSamples = chunk;
	context.continousTimeSamples = continuousSamples;
	context.projectTimeSamples = continuousSamples;
	continuousSamples += chunk;

	outputEvents.clear();
	processData.outputEvents = &outputEvents;
	processor->process(processData);
	collectOutputEvents(offset);
}

void HostedPlugin::Impl::sliceEvents(int offset, int chunk, bool lastChunk)
{
	// 1b. this chunk's slice of the drained MIDI list. An event belongs to the
	//     chunk its sample offset falls in, rebased to that chunk's start. The
	//     BOUNDARY RULE: an event whose offset is at or beyond the end of the
	//     request is not inside any chunk, so it is carried into the next
	//     request and delivered there at offset 0. It is still seen exactly
	//     once - dropping it would turn a note-off written at the block
	//     boundary into a note that never releases - and the plug-in is never
	//     handed an offset outside [0, chunk), which is what the old
	//     `std::clamp(frameOffset, 0, frames)` rule produced on the last chunk.
	if (processData.inputEvents != nullptr)
	{
		chunkEvents.clear();
		const int32 count = inputEvents.getEventCount();
		const int32 limit = offset + chunk;
		for (int32 i = 0; i < count; ++i)
		{
			Event event{};
			if (inputEvents.getEvent(i, event) != kResultOk) { continue; }
			if (event.sampleOffset < offset) { continue; }
			if (event.sampleOffset >= limit)
			{
				if (!lastChunk) { continue; }
				event.sampleOffset = 0;
				deferredEvents.addEvent(event);
				continue;
			}
			event.sampleOffset -= offset;
			chunkEvents.addEvent(event);
		}
		processData.inputEvents = &chunkEvents;
	}
}

void HostedPlugin::Impl::mapBuses(const float* const* inputs, float* const* outputs, int numInputs, int numOutputs,
	int offset, int chunk)
{
	// 2. map the planar LMMS channels onto the plug-in's buses. Channels the
	//    track does not provide read from a pre-allocated silence buffer and
	//    write into a pre-allocated scratch buffer. Both are maxBlockSize long,
	//    and a chunk is never longer than that, so the mapping points INTO them
	//    (at their start) rather than past their end.
	std::size_t channel = 0;
	for (std::size_t b = 0; b < inputBuses.size(); ++b)
	{
		auto& bus = inputBuses[b];
		auto& ptrs = inputChannelPtrs[b];
		for (int32 c = 0; c < bus.numChannels; ++c, ++channel)
		{
			ptrs[c] = channel < static_cast<std::size_t>(numInputs)
				? const_cast<float*>(inputs[channel]) + offset
				: silence.data();
		}
		bus.channelBuffers32 = ptrs.data();
		bus.silenceFlags = 0;
	}
	channel = 0;
	for (std::size_t b = 0; b < outputBuses.size(); ++b)
	{
		auto& bus = outputBuses[b];
		auto& ptrs = outputChannelPtrs[b];
		for (int32 c = 0; c < bus.numChannels; ++c, ++channel)
		{
			const bool hasCallerOutput = channel < static_cast<std::size_t>(numOutputs);
			ptrs[c] = hasCallerOutput
				? outputs[channel] + offset
				: scratchOutput.data();
			if (!hasCallerOutput)
			{
				std::memset(ptrs[c], 0, static_cast<std::size_t>(chunk) * sizeof(float));
			}
		}
		bus.channelBuffers32 = ptrs.data();
		bus.silenceFlags = 0;
	}
}

void HostedPlugin::Impl::collectOutputEvents(int offset)
{
	Event event{};
	MidiEventIn spare{};
	for (int32 i = 0; i < outputEvents.getEventCount(); ++i)
	{
		// Bounded (no growth on the audio thread); a note past the bound is counted, not lost unseen (review r4 L20 #15).
		const bool room = blockOutputCount < kMaxMidiEventsPerBlock;
		MidiEventIn* out = room ? &blockOutput[static_cast<std::size_t>(blockOutputCount)] : &spare;
		if (outputEvents.getEvent(i, event) != kResultOk || !vst3EventToMidi(event, offset, out)) { continue; }
		if (room) { ++blockOutputCount; } else { droppedOutput.fetch_add(1, std::memory_order_relaxed); }
	}
}

auto HostedPlugin::paramDisplayValue(std::uint32_t id, float normalized) const -> QString
{
	const auto& d = *m_impl;
	if (!d.controller) { return {}; }
	String128 text{};
	if (d.controller->getParamStringByValue(static_cast<ParamID>(id), normalized, text) ==
		kResultOk)
	{
		return fromString128(text);
	}
	return {};
}

#ifdef LMMS_TESTING
auto HostedPlugin::lastChunkEventCount() const -> int
{
	return m_impl->chunkEvents.getEventCount();
}

auto HostedPlugin::lastParameterChangeCount() const -> int
{
	return m_impl->inputParamChanges.getParameterCount();
}

auto HostedPlugin::lastChunkEventOffset(int index) const -> int
{
	const auto* event = m_impl->chunkEvents.getEventByIndex(index);
	return event != nullptr ? event->sampleOffset : -1;
}
#endif

auto hostChunkingStats() -> HostChunkingStats
{
	return control::vst3HostChunkingCounters().read();
}

auto HostedPlugin::editController() const -> IEditController* { return m_impl->controller.get(); }

} // namespace lmms::vst3
