/*
 * ClapHost.cpp - in-process CLAP host for LMMS
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

#include "ClapHost.h"

#include "ClapLoader.h"
#include "PluginHostChunking.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>

#include <clap/clap.h>

#include <QFile>

namespace lmms::clap
{

namespace
{

//! Thread-local answers for clap.thread-check. The host APIs that are only
//! legal on the main thread mark their thread once; process() marks the audio
//! thread. This keeps the check lock-free and allocation-free.
thread_local bool t_isMainThread = false;
thread_local bool t_isAudioThread = false;

void setError(QString* error, const QString& message)
{
	if (error) { *error = message; }
}

//! Value of a parameter mapped into 0..1; 0 for degenerate ranges.
auto normalize(const ParamDescriptor& descriptor, double value) -> float
{
	const auto range = descriptor.maxValue - descriptor.minValue;
	if (range <= 0.0) { return 0.0f; }
	return static_cast<float>((value - descriptor.minValue) / range);
}

auto describe(const clap_plugin_descriptor_t& descriptor) -> ClassInfo
{
	ClassInfo info;
	info.id = QString::fromUtf8(descriptor.id ? descriptor.id : "");
	info.name = QString::fromUtf8(descriptor.name ? descriptor.name : "");
	info.vendor = QString::fromUtf8(descriptor.vendor ? descriptor.vendor : "");
	info.version = QString::fromUtf8(descriptor.version ? descriptor.version : "");
	info.description = QString::fromUtf8(descriptor.description ? descriptor.description : "");
	if (descriptor.features)
	{
		for (auto feature = descriptor.features; *feature; ++feature)
		{
			if (std::strcmp(*feature, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0 ||
				std::strcmp(*feature, CLAP_PLUGIN_FEATURE_SYNTHESIZER) == 0 ||
				std::strcmp(*feature, CLAP_PLUGIN_FEATURE_SAMPLER) == 0)
			{
				info.isInstrument = true;
			}
		}
	}
	return info;
}

} // namespace

//! The input event list handed to the plug-in: a flat, preallocated array of
//! parameter change events, rebuilt every block. No allocation, no locking.
struct EventListState
{
	const clap_event_param_value_t* events = nullptr;
	std::uint32_t count = 0;
};

struct HostedPlugin::Impl
{
	struct ParamSlot
	{
		std::atomic<double> value{0.0};
		std::atomic<std::uint32_t> revision{0};
	};

	// --- module -----------------------------------------------------------
	//! The module, the clap_entry inside it and its factory. Their lifetime is
	//! owned by Library::reset() (ClapLoader.h), which is also what releases
	//! them in unload(). `status` is the TYPED answer of the last load():
	//! "library not found" / "no clap_entry" / "version incompatible" are
	//! distinct codes, not one prose string (see ClapLoader.h).
	loader::Library loaded;
	loader::Status status;
	const clap_plugin_t* plugin = nullptr;

	// --- plug-in description ---------------------------------------------
	ClassInfo info;
	std::vector<ParamDescriptor> params;
	std::vector<std::uint32_t> paramIds;
	std::vector<void*> paramCookies;
	std::vector<PortDescriptor> ports;
	PortLayout layout;

	const clap_plugin_params_t* paramsExt = nullptr;
	const clap_plugin_audio_ports_t* audioPortsExt = nullptr;
	const clap_plugin_state_t* stateExt = nullptr;
	const clap_plugin_latency_t* latencyExt = nullptr;

	// --- processing configuration ----------------------------------------
	bool prepared = false;
	double sampleRate = 0.0;
	int maxFrames = 0;
	std::int64_t steadyTime = 0;

	// --- real-time state (all buffers sized in prepare()) -----------------
	std::unique_ptr<ParamSlot[]> paramSlots;
	std::vector<std::uint32_t> lastSeenRevision;
	std::vector<clap_event_param_value_t> paramEvents;
	EventListState eventState;
	clap_input_events_t inEvents{};
	clap_output_events_t outEvents{};

	std::vector<const float*> inPtrs;
	std::vector<float*> outPtrs;
	std::vector<clap_audio_buffer_t> inBuffers;
	std::vector<clap_audio_buffer_t> outBuffers;
	std::vector<float> silence;
	std::vector<float> outputScratch;
	clap_event_transport_t transport{};

	//! One chunk of one process() request: the pointer mapping, the transport
	//! event and the plugin->process() call for [offset, offset + chunk).
	//! `chunk` is never larger than maxFrames. See the over-run and tail rule
	//! documented on HostedPlugin::process() in ClapHost.h.
	auto runChunk(const float* const* inputs, float* const* outputs, int inputChannels,
		int outputChannels, int offset, int chunk) -> bool;

	std::atomic<bool> needsReprepare{false};
	std::atomic<bool> callbackRequested{false};
	std::atomic<double> tempo{120.0};
	std::atomic<bool> playing{false};

	clap_host_t host{};
	clap_host_params_t hostParams{};
	clap_host_audio_ports_t hostAudioPorts{};
	clap_host_state_t hostState{};
	clap_host_latency_t hostLatency{};
	clap_host_log_t hostLog{};
	clap_host_thread_check_t hostThreadCheck{};

	auto indexOfParam(std::uint32_t id) const -> int
	{
		for (std::size_t i = 0; i < paramIds.size(); ++i)
		{
			if (paramIds[i] == id) { return static_cast<int>(i); }
		}
		return -1;
	}

	// --- clap.host --------------------------------------------------------
	static auto CLAP_ABI getExtension(const clap_host_t* host, const char* id) -> const void*
	{
		const auto* self = static_cast<const Impl*>(host->host_data);
		if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) { return &self->hostParams; }
		if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) { return &self->hostAudioPorts; }
		if (std::strcmp(id, CLAP_EXT_STATE) == 0) { return &self->hostState; }
		if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) { return &self->hostLatency; }
		if (std::strcmp(id, CLAP_EXT_LOG) == 0) { return &self->hostLog; }
		if (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0) { return &self->hostThreadCheck; }
		return nullptr;
	}

	static void CLAP_ABI requestRestart(const clap_host_t* host)
	{
		static_cast<Impl*>(host->host_data)->needsReprepare.store(true, std::memory_order_relaxed);
	}

	static void CLAP_ABI requestProcess(const clap_host_t*) {}

	static void CLAP_ABI requestCallback(const clap_host_t* host)
	{
		static_cast<Impl*>(host->host_data)->callbackRequested.store(true, std::memory_order_relaxed);
	}

	static void CLAP_ABI paramsRescan(const clap_host_t*, clap_param_rescan_flags) {}

	static void CLAP_ABI paramsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}

	static void CLAP_ABI paramsRequestFlush(const clap_host_t*) {}

	static void CLAP_ABI audioPortsRescan(const clap_host_t*, std::uint32_t) {}

	static void CLAP_ABI stateMarkDirty(const clap_host_t*) {}

	static void CLAP_ABI latencyChanged(const clap_host_t*) {}

	static void CLAP_ABI logMessage(const clap_host_t*, clap_log_severity severity, const char* message)
	{
		// Only the main thread may format and emit; the audio thread must stay
		// free of allocation and locking, so its messages are dropped.
		if (!t_isMainThread || !message) { return; }
		switch (severity)
		{
		case CLAP_LOG_ERROR:
		case CLAP_LOG_FATAL:
		case CLAP_LOG_PLUGIN_MISBEHAVING:
			qWarning("CLAP: %s", message);
			break;
		default:
			qDebug("CLAP: %s", message);
			break;
		}
	}

	static auto CLAP_ABI isMainThread(const clap_host_t*) -> bool { return t_isMainThread; }
	static auto CLAP_ABI isAudioThread(const clap_host_t*) -> bool { return t_isAudioThread; }

	// --- event lists ------------------------------------------------------
	static auto CLAP_ABI inEventsSize(const clap_input_events_t* list) -> std::uint32_t
	{
		return static_cast<const EventListState*>(list->ctx)->count;
	}

	static auto CLAP_ABI inEventsGet(const clap_input_events_t* list, std::uint32_t index)
		-> const clap_event_header_t*
	{
		const auto* state = static_cast<const EventListState*>(list->ctx);
		if (index >= state->count) { return nullptr; }
		return &state->events[index].header;
	}

	static auto CLAP_ABI outEventsTryPush(const clap_output_events_t* list,
		const clap_event_header_t* event) -> bool
	{
		auto* self = static_cast<Impl*>(list->ctx);
		if (event->space_id == CLAP_CORE_EVENT_SPACE_ID &&
			event->type == CLAP_EVENT_PARAM_VALUE &&
			event->size >= sizeof(clap_event_param_value_t))
		{
			const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
			const auto index = self->indexOfParam(value->param_id);
			if (index >= 0)
			{
				// Plug-in -> host change: publish the value without bumping the
				// revision, otherwise the host would echo it back next block.
				self->paramSlots[index].value.store(value->value, std::memory_order_relaxed);
			}
		}
		return true;
	}

	void initHost()
	{
		host.clap_version = CLAP_VERSION_INIT;
		host.host_data = this;
		host.name = "Zene Studio";
		host.vendor = "Zene Studio";
		host.url = "https://github.com/KRUZZZZY/zene-studio";
		host.version = "1.0.0";
		host.get_extension = &Impl::getExtension;
		host.request_restart = &Impl::requestRestart;
		host.request_process = &Impl::requestProcess;
		host.request_callback = &Impl::requestCallback;

		hostParams.rescan = &Impl::paramsRescan;
		hostParams.clear = &Impl::paramsClear;
		hostParams.request_flush = &Impl::paramsRequestFlush;

		hostAudioPorts.rescan = &Impl::audioPortsRescan;

		hostState.mark_dirty = &Impl::stateMarkDirty;
		hostLatency.changed = &Impl::latencyChanged;
		hostLog.log = &Impl::logMessage;
		hostThreadCheck.is_main_thread = &Impl::isMainThread;
		hostThreadCheck.is_audio_thread = &Impl::isAudioThread;
	}

	void freeRealtimeBuffers()
	{
		inPtrs.clear();
		outPtrs.clear();
		inBuffers.clear();
		outBuffers.clear();
		silence.clear();
		outputScratch.clear();
		paramEvents.clear();
		lastSeenRevision.clear();
		eventState = {};
		inEvents = {};
		outEvents = {};
	}
};

HostedPlugin::HostedPlugin() : m_impl{std::make_unique<Impl>()}
{
	m_impl->initHost();
}

HostedPlugin::~HostedPlugin() { unload(); }

auto HostedPlugin::isLoaded() const -> bool { return m_impl->plugin != nullptr; }

//! The typed answer of the last load(): which failure it was, not just its
//! sentence. Code::None after a successful load, and it survives the unload()
//! every failure path calls, so a caller can read it after load() returns false.
auto HostedPlugin::lastLoadFailure() const -> const loader::Status& { return m_impl->status; }

auto HostedPlugin::classInfo() const -> const ClassInfo& { return m_impl->info; }

auto HostedPlugin::className() const -> QString { return m_impl->info.name; }

auto HostedPlugin::vendor() const -> QString { return m_impl->info.vendor; }

auto HostedPlugin::isInstrument() const -> bool { return m_impl->info.isInstrument; }

auto HostedPlugin::parameters() const -> const std::vector<ParamDescriptor>& { return m_impl->params; }

auto HostedPlugin::ports() const -> const std::vector<PortDescriptor>& { return m_impl->ports; }

auto HostedPlugin::busLayout() const -> const PortLayout& { return m_impl->layout; }

auto HostedPlugin::latency() const -> std::uint32_t
{
	const auto& impl = *m_impl;
	return impl.latencyExt ? impl.latencyExt->get(impl.plugin) : 0;
}

auto HostedPlugin::paramIndex(std::uint32_t id) const -> int { return m_impl->indexOfParam(id); }

auto HostedPlugin::paramPlain(std::uint32_t id) const -> double
{
	const auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	return index >= 0 ? impl.paramSlots[index].value.load(std::memory_order_relaxed) : 0.0;
}

auto HostedPlugin::paramNormalized(std::uint32_t id) const -> float
{
	const auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	if (index < 0) { return 0.0f; }
	return normalize(impl.params[index], impl.paramSlots[index].value.load(std::memory_order_relaxed));
}

auto HostedPlugin::paramDisplayValue(std::uint32_t id, double plainValue) const -> QString
{
	const auto& impl = *m_impl;
	if (impl.paramsExt)
	{
		char text[CLAP_NAME_SIZE] = {};
		if (impl.paramsExt->value_to_text(impl.plugin, id, plainValue, text, sizeof(text)))
		{
			return QString::fromUtf8(text);
		}
	}
	return QString::number(plainValue, 'g', 6);
}

void HostedPlugin::setParamPlain(std::uint32_t id, double value)
{
	auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	if (index < 0 || impl.params[index].readOnly) { return; }
	auto& descriptor = impl.params[index];
	const auto clamped = std::clamp(value, descriptor.minValue, descriptor.maxValue);
	auto& slot = impl.paramSlots[index];
	if (slot.value.load(std::memory_order_relaxed) == clamped) { return; }
	slot.value.store(clamped, std::memory_order_relaxed);
	slot.revision.fetch_add(1, std::memory_order_relaxed);
}

void HostedPlugin::setParamNormalized(std::uint32_t id, float normalized)
{
	auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	if (index < 0) { return; }
	const auto& descriptor = impl.params[index];
	const auto range = descriptor.maxValue - descriptor.minValue;
	setParamPlain(id, range > 0.0 ? descriptor.minValue + normalized * range : descriptor.minValue);
}

void HostedPlugin::setTempo(double bpm) { m_impl->tempo.store(bpm, std::memory_order_relaxed); }

void HostedPlugin::setTransportPlaying(bool playing)
{
	m_impl->playing.store(playing, std::memory_order_relaxed);
}

auto HostedPlugin::needsReprepare() const -> bool
{
	return m_impl->needsReprepare.load(std::memory_order_relaxed);
}

void HostedPlugin::clearNeedsReprepare()
{
	m_impl->needsReprepare.store(false, std::memory_order_relaxed);
}

auto HostedPlugin::takeCallbackRequest() -> bool
{
	return m_impl->callbackRequested.exchange(false, std::memory_order_relaxed);
}

auto HostedPlugin::saveState(QByteArray* state) const -> bool
{
	const auto& impl = *m_impl;
	if (!state) { return false; }
	state->clear();
	if (!impl.stateExt) { return false; }

	clap_ostream_t stream{};
	stream.ctx = state;
	stream.write = [](const clap_ostream_t* self, const void* buffer, std::uint64_t size) -> std::int64_t {
		auto* out = static_cast<QByteArray*>(self->ctx);
		out->append(static_cast<const char*>(buffer), static_cast<qsizetype>(size));
		return static_cast<std::int64_t>(size);
	};
	return impl.stateExt->save(impl.plugin, &stream);
}

auto HostedPlugin::loadState(const QByteArray& state) -> bool
{
	auto& impl = *m_impl;
	if (!impl.stateExt) { return false; }

	struct Reader
	{
		const QByteArray* data;
		qsizetype offset;
	};
	Reader reader{&state, 0};
	clap_istream_t stream{};
	stream.ctx = &reader;
	stream.read = [](const clap_istream_t* self, void* buffer, std::uint64_t size) -> std::int64_t {
		auto* reader = static_cast<Reader*>(self->ctx);
		const auto available = reader->data->size() - reader->offset;
		if (available <= 0) { return 0; }
		const auto count = std::min<std::uint64_t>(size, static_cast<std::uint64_t>(available));
		std::memcpy(buffer, reader->data->constData() + reader->offset, static_cast<std::size_t>(count));
		reader->offset += static_cast<qsizetype>(count);
		return static_cast<std::int64_t>(count);
	};
	if (!impl.stateExt->load(impl.plugin, &stream)) { return false; }

	// The plug-in restored its own values; mirror them into the lock-free paramSlots
	// without bumping the revisions (that would echo the values back).
	if (impl.paramsExt)
	{
		for (std::size_t i = 0; i < impl.params.size(); ++i)
		{
			double value = impl.params[i].defaultValue;
			if (impl.paramsExt->get_value(impl.plugin, impl.params[i].id, &value))
			{
				impl.paramSlots[i].value.store(value, std::memory_order_relaxed);
			}
		}
	}
	return true;
}

auto HostedPlugin::isPrepared() const -> bool { return m_impl->prepared; }

auto HostedPlugin::load(const QString& modulePath, const QString& pluginId, QString* error) -> bool
{
	t_isMainThread = true;
	unload();
	auto& impl = *m_impl;

	// The module ladder (open -> clap_entry -> version -> init -> factory) is
	// ClapLoader.cpp's job now, and every step of it leaves a TYPED answer in
	// impl.status: LibraryUnavailable / SymbolMissing / VersionUnsupported /
	// EntryInitFailed / FactoryMissing. The sentence it carries is unchanged.
	if (!loader::load(modulePath, impl.loaded, impl.status))
	{
		setError(error, impl.status.message());
		unload();
		return false;
	}

	const clap_plugin_descriptor_t* descriptor = nullptr;
	const auto count = impl.loaded.factory->get_plugin_count(impl.loaded.factory);
	for (std::uint32_t i = 0; i < count && !descriptor; ++i)
	{
		const auto* candidate = impl.loaded.factory->get_plugin_descriptor(impl.loaded.factory, i);
		if (!candidate || !candidate->id) { continue; }
		if (pluginId.isEmpty() || pluginId == QString::fromUtf8(candidate->id) ||
			pluginId == QString::fromUtf8(candidate->name ? candidate->name : ""))
		{
			descriptor = candidate;
		}
	}
	if (!descriptor)
	{
		impl.status = {loader::Code::PluginNotFound,
			QStringLiteral("'%1' has no plug-in with id '%2'").arg(modulePath, pluginId)};
		setError(error, impl.status.message());
		unload();
		return false;
	}
	impl.info = describe(*descriptor);

	const QByteArray idUtf8{descriptor->id};
	impl.plugin = impl.loaded.factory->create_plugin(impl.loaded.factory, &impl.host, idUtf8.constData());
	if (!impl.plugin)
	{
		impl.status = {loader::Code::PluginCreateFailed,
			QStringLiteral("create_plugin() failed for '%1'").arg(pluginId)};
		setError(error, impl.status.message());
		unload();
		return false;
	}
	if (!impl.plugin->init(impl.plugin))
	{
		impl.status = {loader::Code::PluginInitFailed,
			QStringLiteral("clap_plugin.init() failed for '%1'").arg(pluginId)};
		setError(error, impl.status.message());
		unload();
		return false;
	}

	impl.paramsExt =
		static_cast<const clap_plugin_params_t*>(impl.plugin->get_extension(impl.plugin, CLAP_EXT_PARAMS));
	impl.audioPortsExt = static_cast<const clap_plugin_audio_ports_t*>(
		impl.plugin->get_extension(impl.plugin, CLAP_EXT_AUDIO_PORTS));
	impl.stateExt =
		static_cast<const clap_plugin_state_t*>(impl.plugin->get_extension(impl.plugin, CLAP_EXT_STATE));
	impl.latencyExt =
		static_cast<const clap_plugin_latency_t*>(impl.plugin->get_extension(impl.plugin, CLAP_EXT_LATENCY));

	if (!impl.audioPortsExt)
	{
		impl.status = {loader::Code::ExtensionMissing,
			QStringLiteral("'%1' does not implement %2").arg(pluginId, CLAP_EXT_AUDIO_PORTS)};
		setError(error, impl.status.message());
		unload();
		return false;
	}
	for (const auto isInput : {true, false})
	{
		const auto portCount = impl.audioPortsExt->count(impl.plugin, isInput);
		for (std::uint32_t i = 0; i < portCount; ++i)
		{
			clap_audio_port_info_t portInfo{};
			if (!impl.audioPortsExt->get(impl.plugin, i, isInput, &portInfo)) { continue; }
			PortDescriptor port;
			port.id = portInfo.id;
			port.direction = isInput ? PortDirection::Input : PortDirection::Output;
			port.name = QString::fromUtf8(portInfo.name);
			port.portType = QString::fromUtf8(portInfo.port_type ? portInfo.port_type : "");
			port.channelCount = static_cast<int>(portInfo.channel_count);
			port.isMain = (portInfo.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0;
			impl.ports.push_back(port);
		}
	}
	impl.layout = mapPorts(impl.ports);
	if (!impl.layout.isValid())
	{
		impl.status = {loader::Code::NoAudioPorts,
			QStringLiteral("'%1' has no usable audio ports").arg(pluginId)};
		setError(error, impl.status.message());
		unload();
		return false;
	}

	if (impl.paramsExt)
	{
		const auto paramCount = impl.paramsExt->count(impl.plugin);
		impl.params.reserve(paramCount);
		impl.paramIds.reserve(paramCount);
		impl.paramCookies.reserve(paramCount);
		for (std::uint32_t i = 0; i < paramCount; ++i)
		{
			clap_param_info_t paramInfo{};
			if (!impl.paramsExt->get_info(impl.plugin, i, &paramInfo)) { continue; }
			ParamDescriptor descriptor;
			descriptor.id = paramInfo.id;
			descriptor.title = QString::fromUtf8(paramInfo.name);
			descriptor.module = QString::fromUtf8(paramInfo.module);
			descriptor.minValue = paramInfo.min_value;
			descriptor.maxValue = paramInfo.max_value;
			descriptor.defaultValue = paramInfo.default_value;
			descriptor.stepped = (paramInfo.flags & CLAP_PARAM_IS_STEPPED) != 0;
			descriptor.periodic = (paramInfo.flags & CLAP_PARAM_IS_PERIODIC) != 0;
			descriptor.readOnly = (paramInfo.flags & CLAP_PARAM_IS_READONLY) != 0;
			descriptor.bypass = (paramInfo.flags & CLAP_PARAM_IS_BYPASS) != 0;
			descriptor.automatable = (paramInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0;
			descriptor.hidden = (paramInfo.flags & CLAP_PARAM_IS_HIDDEN) != 0;
			descriptor.requiresProcess = (paramInfo.flags & CLAP_PARAM_REQUIRES_PROCESS) != 0;
			descriptor.defaultNormalized = normalize(descriptor, descriptor.defaultValue);
			impl.params.push_back(descriptor);
			impl.paramIds.push_back(descriptor.id);
			impl.paramCookies.push_back(paramInfo.cookie);
		}
	}

	impl.paramSlots = std::make_unique<Impl::ParamSlot[]>(std::max<std::size_t>(impl.params.size(), 1));
	for (std::size_t i = 0; i < impl.params.size(); ++i)
	{
		double value = impl.params[i].defaultValue;
		if (impl.paramsExt && impl.paramsExt->get_value(impl.plugin, impl.params[i].id, &value))
		{
			value = std::clamp(value, impl.params[i].minValue, impl.params[i].maxValue);
		}
		impl.paramSlots[i].value.store(value, std::memory_order_relaxed);
	}
	return true;
}

void HostedPlugin::unload()
{
	auto& impl = *m_impl;
	release();
	if (impl.plugin)
	{
		impl.plugin->destroy(impl.plugin);
		impl.plugin = nullptr;
	}
	// deinit() (when init() succeeded) then close the module: one call, so the
	// two cannot drift apart here and in the loader's own failure paths.
	impl.loaded.reset();
	impl.paramsExt = nullptr;
	impl.audioPortsExt = nullptr;
	impl.stateExt = nullptr;
	impl.latencyExt = nullptr;
	impl.params.clear();
	impl.paramIds.clear();
	impl.paramCookies.clear();
	impl.ports.clear();
	impl.layout = {};
	impl.info = {};
	impl.paramSlots.reset();
	impl.needsReprepare.store(false, std::memory_order_relaxed);
	impl.callbackRequested.store(false, std::memory_order_relaxed);
}

auto HostedPlugin::prepare(double sampleRate, int maxBlockSize, QString* error) -> bool
{
	auto& impl = *m_impl;
	if (!impl.plugin)
	{
		setError(error, QStringLiteral("No CLAP plug-in loaded"));
		return false;
	}
	if (sampleRate <= 0.0 || maxBlockSize <= 0)
	{
		setError(error, QStringLiteral("Invalid sample rate or block size"));
		return false;
	}
	release();

	impl.sampleRate = sampleRate;
	impl.maxFrames = maxBlockSize;
	impl.steadyTime = 0;

	// Every buffer the audio thread touches is allocated here, once.
	impl.inPtrs.assign(std::max<std::size_t>(impl.layout.inputs, 1), nullptr);
	impl.outPtrs.assign(std::max<std::size_t>(impl.layout.outputs, 1), nullptr);
	impl.inBuffers.assign(impl.layout.inputPortChannels.size(), clap_audio_buffer_t{});
	impl.outBuffers.assign(impl.layout.outputPortChannels.size(), clap_audio_buffer_t{});
	impl.silence.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
	impl.outputScratch.assign(static_cast<std::size_t>(impl.layout.outputs) * maxBlockSize, 0.0f);
	impl.paramEvents.assign(impl.params.size(), clap_event_param_value_t{});
	impl.lastSeenRevision.assign(impl.params.size(), 0);

	impl.eventState.events = impl.paramEvents.data();
	impl.eventState.count = 0;
	impl.inEvents.ctx = &impl.eventState;
	impl.inEvents.size = &Impl::inEventsSize;
	impl.inEvents.get = &Impl::inEventsGet;
	impl.outEvents.ctx = &impl;
	impl.outEvents.try_push = &Impl::outEventsTryPush;

	if (!impl.plugin->activate(impl.plugin, sampleRate, 1, static_cast<std::uint32_t>(maxBlockSize)))
	{
		setError(error, QStringLiteral("clap_plugin.activate() failed"));
		impl.freeRealtimeBuffers();
		return false;
	}
	if (!impl.plugin->start_processing(impl.plugin))
	{
		impl.plugin->deactivate(impl.plugin);
		setError(error, QStringLiteral("clap_plugin.start_processing() failed"));
		impl.freeRealtimeBuffers();
		return false;
	}
	impl.prepared = true;
	impl.needsReprepare.store(false, std::memory_order_relaxed);
	return true;
}

void HostedPlugin::release()
{
	auto& impl = *m_impl;
	if (impl.plugin && impl.prepared)
	{
		impl.plugin->stop_processing(impl.plugin);
		impl.plugin->deactivate(impl.plugin);
		impl.prepared = false;
	}
	impl.freeRealtimeBuffers();
}

auto HostedPlugin::process(const float* const* inputs, float* const* outputs, int inputChannels,
	int outputChannels, int frames) -> bool
{
	auto& impl = *m_impl;
	if (!impl.prepared || !impl.plugin) { return false; }
	if (frames <= 0) { return true; }
	t_isAudioThread = true;

	// Chunking counters, in the CORE (include/PluginHostChunking.h): the audio
	// path increments, the control surface reads through `plugin.host_chunking`.
	// Relaxed atomics, no allocation, no lock, no per-frame traffic.
	control::PluginHostChunkingCounters& counters = control::clapHostChunkingCounters();
	counters.recordRequest(frames, impl.maxFrames);

	// --- parameter changes -> preallocated event list ---------------------
	// Built once for the whole request; the first chunk carries them and the
	// chunks after it are handed an empty list, so each change is delivered
	// exactly once.
	std::uint32_t eventCount = 0;
	for (std::size_t i = 0; i < impl.params.size(); ++i)
	{
		const auto revision = impl.paramSlots[i].revision.load(std::memory_order_relaxed);
		if (revision == impl.lastSeenRevision[i]) { continue; }
		impl.lastSeenRevision[i] = revision;
		auto& event = impl.paramEvents[eventCount];
		event = {};
		event.header.size = sizeof(clap_event_param_value_t);
		event.header.time = 0;
		event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
		event.header.type = CLAP_EVENT_PARAM_VALUE;
		event.header.flags = 0;
		event.param_id = impl.params[i].id;
		event.cookie = impl.paramCookies[i];
		event.note_id = -1;
		event.port_index = -1;
		event.channel = -1;
		event.key = -1;
		event.value = impl.paramSlots[i].value.load(std::memory_order_relaxed);
		++eventCount;
	}
	impl.eventState.count = eventCount;

	// --- chunks -----------------------------------------------------------
	// As many chunks of at most the activated block size as the request needs;
	// the last one carries the remainder. No frame is dropped and no chunk is
	// ever longer than the plug-in was activated for.
	bool ok = true;
	int offset = 0;
	while (offset < frames && ok)
	{
		const int chunk = std::min(frames - offset, impl.maxFrames);
		ok = impl.runChunk(inputs, outputs, inputChannels, outputChannels, offset, chunk);
		// The parameter events belong to the chunk that starts the request.
		impl.eventState.count = 0;
		counters.recordChunk();
		offset += chunk;
	}
	return ok;
}

auto HostedPlugin::Impl::runChunk(const float* const* inputs, float* const* outputs,
	int inputChannels, int outputChannels, int offset, int chunk) -> bool
{
	// --- planar channel pointers -> clap_audio_buffer_t -------------------
	int channel = 0;
	for (std::size_t p = 0; p < layout.inputPortChannels.size(); ++p)
	{
		const auto channels = layout.inputPortChannels[p];
		auto& buffer = inBuffers[p];
		buffer = {};
		buffer.channel_count = static_cast<std::uint32_t>(channels);
		// clap_audio_buffer_t::data32 is float** even for inputs; the plug-in
		// contract is that input buffers are read-only.
		buffer.data32 = const_cast<float**>(inPtrs.data() + channel);
		for (int c = 0; c < channels; ++c)
		{
			// A channel the caller does not supply reads from the zeroed
			// block; a chunk is never longer than that block, so this points
			// into it rather than past its end.
			inPtrs[channel + c] = (channel + c) < inputChannels ? inputs[channel + c] + offset
																: silence.data();
		}
		channel += channels;
	}
	channel = 0;
	for (std::size_t p = 0; p < layout.outputPortChannels.size(); ++p)
	{
		const auto channels = layout.outputPortChannels[p];
		auto& buffer = outBuffers[p];
		buffer = {};
		buffer.channel_count = static_cast<std::uint32_t>(channels);
		buffer.data32 = outPtrs.data() + channel;
		for (int c = 0; c < channels; ++c)
		{
			// Same for a channel the caller does not supply: the scratch is
			// maxFrames per channel, a chunk never longer than that.
			outPtrs[channel + c] = (channel + c) < outputChannels
				? outputs[channel + c] + offset
				: outputScratch.data() + static_cast<std::size_t>(channel + c) * maxFrames;
		}
		channel += channels;
	}

	// --- transport --------------------------------------------------------
	transport = {};
	transport.header.size = sizeof(clap_event_transport_t);
	transport.header.time = 0;
	transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
	transport.header.type = CLAP_EVENT_TRANSPORT;
	transport.flags = CLAP_TRANSPORT_HAS_TEMPO |
		(playing.load(std::memory_order_relaxed) ? CLAP_TRANSPORT_IS_PLAYING : 0);
	transport.tempo = tempo.load(std::memory_order_relaxed);

	clap_process_t request{};
	request.steady_time = steadyTime;
	request.frames_count = static_cast<std::uint32_t>(chunk);
	request.transport = &transport;
	request.audio_inputs = inBuffers.data();
	request.audio_inputs_count = static_cast<std::uint32_t>(inBuffers.size());
	request.audio_outputs = outBuffers.data();
	request.audio_outputs_count = static_cast<std::uint32_t>(outBuffers.size());
	request.in_events = &inEvents;
	request.out_events = &outEvents;

	steadyTime += chunk;
	const auto status = plugin->process(plugin, &request);
	return status != CLAP_PROCESS_ERROR;
}

auto listClasses(const QString& modulePath, loader::Status* status, QString* error) -> std::vector<ClassInfo>
{
	std::vector<ClassInfo> classes;
	// The same ladder load() uses, so a scan reports the SAME typed reason an
	// instance would: a module with no clap_entry, a CLAP 0.x module and a
	// module whose init() fails are three different answers here too, not one
	// "not a usable CLAP module" (which is what this function used to say).
	loader::Library library;
	loader::Status failure;
	if (!loader::load(modulePath, library, failure))
	{
		if (status) { *status = failure; }
		setError(error, failure.message());
		return classes;
	}
	const auto* factory = library.factory;
	const auto count = factory->get_plugin_count(factory);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		const auto* descriptor = factory->get_plugin_descriptor(factory, i);
		if (descriptor) { classes.push_back(describe(*descriptor)); }
	}
	if (status) { *status = {}; }
	library.reset();
	return classes;
}

auto listClasses(const QString& modulePath, QString* error) -> std::vector<ClassInfo>
{
	return listClasses(modulePath, nullptr, error);
}

auto hostChunkingStats() -> HostChunkingStats
{
	return control::clapHostChunkingCounters().read();
}

} // namespace lmms::clap
