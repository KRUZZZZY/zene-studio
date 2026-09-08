/*
 * Vst3Host.cpp - in-process VST3 host
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

#include "Vst3Host.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <QDebug>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
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

void setError(QString* error, const QString& message)
{
	if (error) { *error = message; }
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

/**
 * Minimal IComponentHandler. performEdit() only mirrors the plug-in's own
 * parameter changes into the lock free parameter array of the host, using a
 * binary search over the pre-sorted parameter index: no allocation, no lock.
 */
class Vst3ComponentHandler : public IComponentHandler
{
public:
	// `release()` deletes through `this`, so the destructor must be virtual to
	// keep the polymorphic delete well defined (-Wdelete-non-virtual-dtor).
	virtual ~Vst3ComponentHandler() = default;

	Vst3ComponentHandler(std::atomic<float>* values,
			const std::vector<std::pair<std::uint32_t, std::size_t>>* indexById) :
		m_values{values},
		m_indexById{indexById}
	{
	}

	tresult PLUGIN_API beginEdit(ParamID) SMTG_OVERRIDE { return kResultOk; }

	tresult PLUGIN_API performEdit(ParamID id, ParamValue valueNormalized) SMTG_OVERRIDE
	{
		const auto it = std::lower_bound(m_indexById->begin(), m_indexById->end(),
			std::make_pair(static_cast<std::uint32_t>(id), std::size_t{0}));
		if (it != m_indexById->end() && it->first == static_cast<std::uint32_t>(id))
		{
			m_values[it->second].store(static_cast<float>(valueNormalized),
				std::memory_order_relaxed);
		}
		return kResultOk;
	}

	tresult PLUGIN_API endEdit(ParamID) SMTG_OVERRIDE { return kResultOk; }

	tresult PLUGIN_API restartComponent(int32) SMTG_OVERRIDE { return kResultOk; }

	tresult PLUGIN_API queryInterface(const TUID, void** obj) SMTG_OVERRIDE
	{
		if (obj) { *obj = nullptr; }
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return ++m_refCount; }

	uint32 PLUGIN_API release() SMTG_OVERRIDE
	{
		const auto count = --m_refCount;
		if (count == 0) { delete this; }
		return count;
	}

private:
	std::atomic<float>* m_values;
	const std::vector<std::pair<std::uint32_t, std::size_t>>* m_indexById;
	std::atomic<uint32> m_refCount{1};
};

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

struct HostedPlugin::Impl
{
	VST3::Hosting::Module::Ptr module;
	std::unique_ptr<PlugProvider> provider;
	IPtr<IComponent> component;
	IPtr<IAudioProcessor> processor;
	IPtr<IEditController> controller;
	IPtr<IComponentHandler> handler;
	bool singleComponent = false;

	QString className;
	QString vendor;
	bool instrument = false;

	std::vector<Vst3ParamDescriptor> params;
	std::vector<std::pair<std::uint32_t, std::size_t>> indexById; // sorted by id
	std::unique_ptr<std::atomic<float>[]> paramValues;
	std::vector<float> lastParamValues;

	BusLayout layout;
	std::vector<BusDescriptor> inputBusDescs;
	std::vector<BusDescriptor> outputBusDescs;

	// audio thread scratch, all allocated in prepare()
	std::vector<AudioBusBuffers> inputBuses;
	std::vector<AudioBusBuffers> outputBuses;
	std::vector<std::vector<float*>> inputChannelPtrs;
	std::vector<std::vector<float*>> outputChannelPtrs;
	std::vector<float> silence;
	std::vector<float> scratchOutput;

	ParameterChanges inputParamChanges;
	ProcessData processData{};
	ProcessContext context{};

	double sampleRate = 0.0;
	int maxBlockSize = 0;
	bool prepared = false;
	std::int64_t continuousSamples = 0;
};

HostedPlugin::HostedPlugin() :
	m_impl{std::make_unique<Impl>()}
{
}

HostedPlugin::~HostedPlugin()
{
	auto& d = *m_impl;
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

auto HostedPlugin::load(const QString& modulePath, const QString& classId, QString* error) -> bool
{
	auto& d = *m_impl;
	std::string errorText;
	d.module = VST3::Hosting::Module::create(modulePath.toStdString(), errorText);
	if (!d.module)
	{
		setError(error, QString::fromStdString(errorText));
		return false;
	}

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
	if (!found)
	{
		setError(error, QString("no audio class '%1' in %2").arg(classId, modulePath));
		return false;
	}

	hostContext();
	d.provider = std::make_unique<PlugProvider>(factory, classInfo);
	if (!d.provider->initialize())
	{
		setError(error, QString("failed to initialize '%1'").arg(classId));
		return false;
	}
	d.component = d.provider->getComponentPtr();
	d.controller = d.provider->getControllerPtr();
	FUnknownPtr<IAudioProcessor> processor{d.component.get()};
	d.processor = processor;
	if (!d.component || !d.processor)
	{
		setError(error, QString("'%1' is not an audio processor").arg(classId));
		return false;
	}

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
		for (int32 i = 0; i < d.component->getBusCount(kAudio, kInput); ++i)
		{
			BusInfo info{};
			if (d.component->getBusInfo(kAudio, kInput, i, info) != kResultOk) { continue; }
			BusDescriptor bus;
			bus.name = fromString128(info.name);
			bus.direction = BusDescriptor::Direction::Input;
			bus.type = info.busType == kMain ? BusDescriptor::Type::Main
							 : BusDescriptor::Type::Aux;
			bus.channelCount = info.channelCount;
			bus.active = (info.flags & BusInfo::kDefaultActive) != 0;
			d.inputBusDescs.push_back(bus);
			buses.push_back(std::move(bus));
		}
		for (int32 i = 0; i < d.component->getBusCount(kAudio, kOutput); ++i)
		{
			BusInfo info{};
			if (d.component->getBusInfo(kAudio, kOutput, i, info) != kResultOk) { continue; }
			BusDescriptor bus;
			bus.name = fromString128(info.name);
			bus.direction = BusDescriptor::Direction::Output;
			bus.type = info.busType == kMain ? BusDescriptor::Type::Main
							 : BusDescriptor::Type::Aux;
			bus.channelCount = info.channelCount;
			bus.active = (info.flags & BusInfo::kDefaultActive) != 0;
			d.outputBusDescs.push_back(bus);
			buses.push_back(std::move(bus));
		}
	}
	d.layout = mapBuses(buses);

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
			desc.defaultNormalized = static_cast<float>(
				d.controller->getParamNormalized(info.id));
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
		for (std::size_t i = 0; i < d.params.size(); ++i)
		{
			d.paramValues[i].store(d.params[i].defaultNormalized, std::memory_order_relaxed);
		}
	}

	d.handler = owned(new Vst3ComponentHandler(d.paramValues.get(), &d.indexById));
	if (d.controller) { d.controller->setComponentHandler(d.handler); }

	return true;
}

auto HostedPlugin::parameters() const -> const std::vector<Vst3ParamDescriptor>&
{
	return m_impl->params;
}

auto HostedPlugin::busLayout() const -> const BusLayout&
{
	return m_impl->layout;
}

auto HostedPlugin::className() const -> QString
{
	return m_impl->className;
}

auto HostedPlugin::vendor() const -> QString
{
	return m_impl->vendor;
}

auto HostedPlugin::isInstrument() const -> bool
{
	return m_impl->instrument;
}

auto HostedPlugin::isLoaded() const -> bool
{
	return m_impl->component != nullptr && m_impl->processor != nullptr;
}

void HostedPlugin::setParamNormalized(std::uint32_t id, float normalized)
{
	auto& d = *m_impl;
	const auto index = paramIndex(id);
	if (index < 0) { return; }
	const auto clamped = std::clamp(normalized, 0.f, 1.f);
	d.paramValues[index].store(clamped, std::memory_order_relaxed);
}

void HostedPlugin::notifyController(std::uint32_t id, float normalized)
{
	auto& d = *m_impl;
	setParamNormalized(id, normalized);
	if (d.controller)
	{
		d.controller->setParamNormalized(static_cast<ParamID>(id),
			std::clamp(normalized, 0.f, 1.f));
	}
}

auto HostedPlugin::paramNormalized(std::uint32_t id) const -> float
{
	const auto index = paramIndex(id);
	return index < 0 ? 0.f : m_impl->paramValues[index].load(std::memory_order_relaxed);
}

auto HostedPlugin::paramIndex(std::uint32_t id) const -> int
{
	const auto& indexById = m_impl->indexById;
	const auto it = std::lower_bound(indexById.begin(), indexById.end(),
		std::make_pair(id, std::size_t{0}));
	if (it == indexById.end() || it->first != id) { return -1; }
	return static_cast<int>(it->second);
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
			d.paramValues[i].store(static_cast<float>(
				d.controller->getParamNormalized(static_cast<ParamID>(d.params[i].id))),
				std::memory_order_relaxed);
		}
	}
	return ok;
}

auto HostedPlugin::prepare(double sampleRate, int maxBlockSize, QString* error) -> bool
{
	auto& d = *m_impl;
	release();
	if (!d.component || !d.processor)
	{
		setError(error, "no plug-in loaded");
		return false;
	}
	if (sampleRate <= 0.0 || maxBlockSize <= 0)
	{
		setError(error, "invalid sample rate or block size");
		return false;
	}

	ProcessSetup setup{};
	setup.processMode = kRealtime;
	setup.symbolicSampleSize = kSample32;
	setup.maxSamplesPerBlock = maxBlockSize;
	setup.sampleRate = sampleRate;
	if (d.processor->setupProcessing(setup) != kResultOk)
	{
		setError(error, "setupProcessing() failed");
		return false;
	}
	if (d.component->setActive(true) != kResultOk)
	{
		setError(error, "setActive() failed");
		return false;
	}
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
	d.lastParamValues.assign(d.params.size(), std::numeric_limits<float>::lowest());

	d.processData = ProcessData{};
	d.processData.processMode = kRealtime;
	d.processData.symbolicSampleSize = kSample32;
	d.processData.inputs = d.inputBuses.empty() ? nullptr : d.inputBuses.data();
	d.processData.numInputs = static_cast<int32>(d.inputBuses.size());
	d.processData.outputs = d.outputBuses.empty() ? nullptr : d.outputBuses.data();
	d.processData.numOutputs = static_cast<int32>(d.outputBuses.size());
	d.processData.inputParameterChanges = &d.inputParamChanges;
	d.processData.processContext = &d.context;

	// Transport state is reported per block through setTransportPlaying() and
	// setTempo(); until then no state flag is valid.
	d.context = ProcessContext{};
	d.context.sampleRate = sampleRate;
	d.context.state = 0;
	d.continuousSamples = 0;

	d.sampleRate = sampleRate;
	d.maxBlockSize = maxBlockSize;
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
	if (!d.prepared || !d.processor) { return; }

	// 1. parameter changes: lock free, queues were sized in prepare()
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

	// 2. map the planar LMMS channels onto the plug-in's buses. Channels the
	//    track does not provide read from a pre-allocated silence buffer and
	//    write into a pre-allocated scratch buffer.
	std::size_t channel = 0;
	for (std::size_t b = 0; b < d.inputBuses.size(); ++b)
	{
		auto& bus = d.inputBuses[b];
		auto& ptrs = d.inputChannelPtrs[b];
		for (int32 c = 0; c < bus.numChannels; ++c, ++channel)
		{
			ptrs[c] = channel < static_cast<std::size_t>(numInputs)
				? const_cast<float*>(inputs[channel])
				: d.silence.data();
		}
		bus.channelBuffers32 = ptrs.data();
		bus.silenceFlags = 0;
	}
	channel = 0;
	for (std::size_t b = 0; b < d.outputBuses.size(); ++b)
	{
		auto& bus = d.outputBuses[b];
		auto& ptrs = d.outputChannelPtrs[b];
		for (int32 c = 0; c < bus.numChannels; ++c, ++channel)
		{
			ptrs[c] = channel < static_cast<std::size_t>(numOutputs)
				? outputs[channel]
				: d.scratchOutput.data();
			std::memset(ptrs[c], 0, static_cast<std::size_t>(frames) * sizeof(float));
		}
		bus.channelBuffers32 = ptrs.data();
		bus.silenceFlags = 0;
	}

	d.processData.numSamples = frames;
	d.context.continousTimeSamples = d.continuousSamples;
	d.context.projectTimeSamples = d.continuousSamples;
	d.continuousSamples += frames;

	d.processor->process(d.processData);
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

} // namespace lmms::vst3
