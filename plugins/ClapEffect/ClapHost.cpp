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

//! The CLAP host core: construction, the small accessors, the activation ladder
//! (prepare, release), the chunked process() the plug-in is driven with, and the
//! module scan. The load/unload ladder lives in ClapHostLoad.cpp, the note path
//! in ClapHostNotes.cpp, the parameter and state path in ClapHostParams.cpp, and
//! the state all four share in ClapHostInternals.h.

#include "ClapHost.h"
#include "ClapHostInternals.h"

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

//! Defined here, declared in ClapHostInternals.h - one instance per module, as
//! they had when this file was the host's only TU.
thread_local bool t_isMainThread = false;
thread_local bool t_isAudioThread = false;

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
	impl.notePortsExt = static_cast<const clap_plugin_note_ports_t*>(
		impl.plugin->get_extension(impl.plugin, CLAP_EXT_NOTE_PORTS));
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
	// A GENERATOR is output-only: an instrument that produces its audio from
	// notes declares no audio input port, so PortLayout::isValid()'s
	// "both directions" rule is the EFFECT rule and cannot be the gate here
	// (feature row 79). What must hold either way is a usable output: a
	// plug-in with nowhere to write its audio is the failure this rejects.
	const bool usableAudio = impl.layout.outputs > 0 &&
		(impl.layout.inputs > 0 || impl.info.isInstrument);
	if (!usableAudio)
	{
		impl.status = {loader::Code::NoAudioPorts,
			QStringLiteral("'%1' has no usable audio ports").arg(pluginId)};
		setError(error, impl.status.message());
		unload();
		return false;
	}

	// --- note input ports (clap.note-ports, feature row 79) ---------------
	// Read while the plug-in is deactivated, which is where load() is. Their
	// ABSENCE is not a failure - a host must not require note ports of a
	// plug-in that takes no notes (every effect). Kept as a member of Impl
	// rather than inline here so load()'s own complexity does not grow for
	// the note path (tests/complexity-gate.sh measures it).
	impl.scanNotePorts();

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
	impl.notePortsExt = nullptr;
	impl.stateExt = nullptr;
	impl.latencyExt = nullptr;
	impl.params.clear();
	impl.paramIds.clear();
	impl.paramCookies.clear();
	impl.ports.clear();
	impl.notePorts.clear();
	impl.notePortIndex = 0;
	impl.layout = {};
	impl.info = {};
	impl.paramSlots.reset();
	impl.needsReprepare.store(false, std::memory_order_relaxed);
	impl.callbackRequested.store(false, std::memory_order_relaxed);
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

	// --- notes -> the same event list -------------------------------------
	// Drained once per request: the notes belong to the chunk that starts it,
	// like the parameter changes, so an event at frame N of the block affects
	// frame N of that chunk.
	impl.drainNoteQueue();

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
		// The parameter events and the notes belong to the chunk that starts
		// the request; the later chunks are handed an empty list.
		impl.eventState.count = 0;
		impl.eventState.noteCount = 0;
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

auto hostChunkingStats() -> HostChunkingStats
{
	return control::clapHostChunkingCounters().read();
}
} // namespace lmms::clap
