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

#ifndef LMMS_CLAP_HOST_INTERNALS_H
#define LMMS_CLAP_HOST_INTERNALS_H

//! HostedPlugin's private state and the host's shared private helpers, used by
//! the host's four TUs: ClapHost.cpp (the host core), ClapHostLoad.cpp (the
//! load/unload ladder), ClapHostNotes.cpp (the note path) and
//! ClapHostParams.cpp (the parameter and state path). Not a public header -
//! nothing outside plugins/ClapEffect/ includes it, and the class the rest of
//! the product talks to is ClapHost.h.

#include "ClapHost.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <clap/clap.h>

#include "PluginEditorHost.h"
#include "PluginWindowSystem.h"

namespace lmms::clap
{

//! Thread-local answers for clap.thread-check, DECLARED here and DEFINED in
//! ClapHost.cpp so the whole module keeps ONE instance of each (as it had when
//! both lived in that one TU). The host APIs that are only legal on the main
//! thread mark their thread once; process() marks the audio thread. This keeps
//! the check lock-free and allocation-free. Impl's answers below read them from
//! whatever TU they are inlined into, so they must not be TU-local.
extern thread_local bool t_isMainThread;
extern thread_local bool t_isAudioThread;

//! Shared by the host TUs: one definition for the whole module, like the
//! thread answers above (a header definition without `inline` is a definition
//! in every TU, which the linker refuses).
inline void setError(QString* error, const QString& message)
{
	if (error) { *error = message; }
}

//! Value of a parameter mapped into 0..1; 0 for degenerate ranges.
inline auto normalize(const ParamDescriptor& descriptor, double value) -> float
{
	const auto range = descriptor.maxValue - descriptor.minValue;
	if (range <= 0.0) { return 0.0f; }
	return static_cast<float>((value - descriptor.minValue) / range);
}

//! A value on its way into a parameter slot, from either boundary (the caller and the plug-in's own
//! reported value). std::clamp does NOT sanitize NaN - every comparison against it is false, so a
//! non-finite value would be returned unchanged and then handed to the processor as a
//! CLAP_EVENT_PARAM_VALUE. A value that is not a number falls back to the descriptor's default.
inline auto sanitizedValue(const ParamDescriptor& descriptor, double value) -> double
{
	return std::isfinite(value) ? std::clamp(value, descriptor.minValue, descriptor.maxValue)
							   : descriptor.defaultValue;
}

//! Static identity of one CLAP class inside a module. DECLARED here and
//! DEFINED in ClapHost.cpp: its two callers are listClasses()
//! (ClapHostLifecycle.cpp) and load() (ClapHost.cpp), and the definition
//! stays in the TU tests/complexity-baseline.tsv keys it to, so a split
//! that moves either caller does not move the baseline entry with it.
auto describe(const clap_plugin_descriptor_t& descriptor) -> ClassInfo;

//! The input event list handed to the plug-in: a flat, preallocated array of
//! parameter change events - plus, for a plug-in with note input ports, the
//! notes this block carries - rebuilt every block. No allocation, no locking.
//!
//! The two arrays are served as ONE list, parameter changes first (they are
//! the events the host generates) and notes after them, so a plug-in that
//! reads `size()`/`get()` in order sees every event of the block. Both counts
//! are zero outside a process() call.
//! One input event of the note path: a note or (E11) a note expression, so both reach the
//! plug-in in one list, in time order.
union NoteOrExpression
{
	clap_event_header_t header;
	clap_event_note_t note;
	clap_event_note_expression_t expression;
	clap_event_param_mod_t paramMod;  //!< 0.5.0 MX-120: a per-note modulation (a per-voice modulator)
};

struct EventListState
{
	const clap_event_param_value_t* events = nullptr;
	std::uint32_t count = 0;
	//! The note and note-expression events of this block (feature row 79, E11), served after
	//! `events`, sorted by time.
	const NoteOrExpression* notes = nullptr;
	std::uint32_t noteCount = 0;
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

	// --- note input (clap.note-ports, feature row 79) ---------------------
	//! The note INPUT ports the plug-in declares, in its own index order.
	//! Empty for a plug-in without the extension, which is a fact and not a
	//! failure (every effect).
	std::vector<NotePortDescriptor> notePorts;
	//! The plug-in's own index of the port notes are delivered to.
	std::uint32_t notePortIndex = 0;
	//! The notes this block carries, sized once in prepare() and filled from
	//! noteQueue by drainNoteQueue(); eventState.notes/noteCount hand them to
	//! the plug-in after the parameter events.
	std::vector<NoteOrExpression> noteEvents;
	NoteQueue noteQueue;
	std::atomic<std::uint32_t> notesPushed{0};
	std::atomic<std::uint32_t> notesDropped{0};
	std::atomic<std::uint32_t> notesDelivered{0};
	std::atomic<std::uint32_t> notesPlayed{0};

	const clap_plugin_params_t* paramsExt = nullptr;
	const clap_plugin_audio_ports_t* audioPortsExt = nullptr;
	const clap_plugin_note_ports_t* notePortsExt = nullptr;
	const clap_plugin_state_t* stateExt = nullptr;
	const clap_plugin_latency_t* latencyExt = nullptr;
	const clap_plugin_tail_t* tailExt = nullptr;

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

	//! The queue-draining half of the note path: pops what the MIDI route
	//! pushed into `noteQueue` and turns each event into a clap_event_note_t
	//! at the front of `noteEvents`. Real-time safe, and a no-op for a
	//! plug-in with no note input port.
	void drainNoteQueue();

	//! The discovery half (feature row 79): reads clap.note-ports while the
	//! plug-in is deactivated, remembers the plug-in's own delivery port
	//! index, and records the ports and the audio layout in the process-wide
	//! note counters `plugin.host_notes` reports. Main thread, from load().
	void scanNotePorts();

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
	//! E8: what the plug-in asked of the host, serviced on the main thread (serviceMainThread)
	std::atomic<std::uint32_t> rescanFlags{0};
	std::atomic<bool> flushRequested{false};
	std::atomic<bool> stateDirty{false};
	//! review L01-1 #2: the plug-in changed its ports; the next prepare() re-reads them
	std::atomic<bool> portsChanged{false};
	std::uint32_t layoutRevision = 0;
	//! W6: the plug-in asked for CLAP_PARAM_RESCAN_ALL; the next prepare() re-reads the list, deactivated
	std::atomic<bool> paramsChanged{false};
	//! Counts parameter list (re)scans and in-place info or text refreshes; the owner re-syncs its models on a move
	std::uint32_t paramsRevision = 0;
	std::atomic<double> tempo{120.0};
	std::atomic<bool> playing{false};

	clap_host_t host{};
	clap_host_params_t hostParams{};
	clap_host_audio_ports_t hostAudioPorts{};
	clap_host_state_t hostState{};
	clap_host_latency_t hostLatency{};
	clap_host_log_t hostLog{};
	clap_host_thread_check_t hostThreadCheck{};
	// R4.1: the editor's run loop - descriptors and timers on the host's main thread.
	clap_host_posix_fd_support_t hostPosixFd{};
	clap_host_timer_support_t hostTimer{};
	std::unique_ptr<PluginEditorHost> editorHost;
	//! on_timer calls delivered to the plugin: nonzero only when BOTH halves exist.
	std::atomic<std::uint32_t> timerDeliveries{0};

	// R4.3: the plug-in's own editor (ClapHostGui.cpp). guiCreated between create() and
	// destroy(); guiRequestedSize is the last request_resize the plug-in made.
	clap_host_gui_t hostGui{};
	bool guiCreated = false;
	std::uint32_t guiRequestedWidth = 0;
	std::uint32_t guiRequestedHeight = 0;
	bool guiClosedByPlugin = false;
	//! 0.5.0 L10 (MX-094): the zoom the editor is created at (clap.gui set_scale), the device's option.
	double editorScale = 1.0;
	//! Q050-490: the window system a test asked for (HostedPlugin::setEditorWindowSystem); unset, the running one.
	std::optional<PluginWindowSystem> editorWindowSystem;
	auto guiExt() const -> const clap_plugin_gui_t*
	{
		return plugin != nullptr ? static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin, CLAP_EXT_GUI))
			: nullptr;
	}

	PluginEditorHost& editorLoop()
	{
		if (editorHost == nullptr) { editorHost = std::make_unique<PluginEditorHost>(); }
		return *editorHost;
	}

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
		if (std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) == 0) { return &self->hostPosixFd; }
		if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) { return &self->hostTimer; }
		if (std::strcmp(id, CLAP_EXT_GUI) == 0) { return &self->hostGui; }
		return nullptr;
	}

	// --- clap.gui, the host half (R4.3) ---------------------------------------
	static void CLAP_ABI guiResizeHintsChanged(const clap_host_t*) {}

	//! The plug-in asks for a new size: granted (the embedding window follows the view).
	static auto CLAP_ABI guiRequestResize(const clap_host_t* host, std::uint32_t width, std::uint32_t height) -> bool
	{
		auto* self = static_cast<Impl*>(host->host_data);
		self->guiRequestedWidth = width;
		self->guiRequestedHeight = height;
		const auto* gui = self->guiExt();
		return gui != nullptr && self->guiCreated && gui->set_size(self->plugin, width, height);
	}

	static auto CLAP_ABI guiRequestShow(const clap_host_t*) -> bool { return true; }
	static auto CLAP_ABI guiRequestHide(const clap_host_t*) -> bool { return true; }

	//! The plug-in closed its own editor. `wasDestroyed` says whether it took the GUI down with it:
	//! if it did, there is nothing left to show or destroy, so the host forgets the GUI and the next
	//! openEditor() creates a fresh one; if it did not, the GUI is alive but hidden, and the next
	//! openEditor() shows it again. Before, both cases only set guiClosedByPlugin, so a reopen
	//! returned true without showing anything (editorOpen() stayed false) and closeEditor() hid and
	//! destroyed a GUI the plug-in had already destroyed.
	static void CLAP_ABI guiClosed(const clap_host_t* host, bool wasDestroyed)
	{
		auto* self = static_cast<Impl*>(host->host_data);
		self->guiClosedByPlugin = true;
		if (wasDestroyed) { self->guiCreated = false; }
	}

	// --- clap.posix-fd-support / clap.timer-support (R4.1) -----------------
	// The plugin's own half is looked up when a descriptor or timer FIRES, not when it is
	// registered: a plugin may register from init(), before the host fetches its extensions.
	// CLAP's fd flags are the same three bits PluginEditorHost uses.
	static auto CLAP_ABI registerFd(const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) -> bool
	{
		auto* self = static_cast<Impl*>(host->host_data);
		return self->editorLoop().registerFd(fd, static_cast<unsigned>(flags), [self](int readyFd, unsigned ready) {
			const auto* ext = static_cast<const clap_plugin_posix_fd_support_t*>(
				self->plugin != nullptr ? self->plugin->get_extension(self->plugin, CLAP_EXT_POSIX_FD_SUPPORT) : nullptr);
			if (ext != nullptr && ext->on_fd != nullptr) { ext->on_fd(self->plugin, readyFd, static_cast<clap_posix_fd_flags_t>(ready)); }
		});
	}

	static auto CLAP_ABI modifyFd(const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) -> bool
	{
		return static_cast<Impl*>(host->host_data)->editorLoop().modifyFd(fd, static_cast<unsigned>(flags));
	}

	static auto CLAP_ABI unregisterFd(const clap_host_t* host, int fd) -> bool
	{
		return static_cast<Impl*>(host->host_data)->editorLoop().unregisterFd(fd);
	}

	static auto CLAP_ABI registerTimer(const clap_host_t* host, std::uint32_t periodMs, clap_id* timerId) -> bool
	{
		auto* self = static_cast<Impl*>(host->host_data);
		std::uint32_t id = 0;
		const bool ok = self->editorLoop().registerTimer(periodMs, [self](std::uint32_t fired) {
			const auto* ext = static_cast<const clap_plugin_timer_support_t*>(
				self->plugin != nullptr ? self->plugin->get_extension(self->plugin, CLAP_EXT_TIMER_SUPPORT) : nullptr);
			if (ext != nullptr && ext->on_timer != nullptr)
			{
				ext->on_timer(self->plugin, fired);
				self->timerDeliveries.fetch_add(1, std::memory_order_relaxed);
			}
		}, &id);
		if (ok && timerId != nullptr) { *timerId = id; }
		return ok;
	}

	static auto CLAP_ABI unregisterTimer(const clap_host_t* host, clap_id timerId) -> bool
	{
		return static_cast<Impl*>(host->host_data)->editorLoop().unregisterTimer(timerId);
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

	//! A rescan of the whole list is only legal deactivated (the CLAP rule; a plug-in that is active asks for a
	//! restart and rescans from deactivate()): it is taken by the next prepare(), right after it deactivates the
	//! plug-in - the same call when the plug-in rescans from inside that deactivate(). The rest waits for the poll.
	static void CLAP_ABI paramsRescan(const clap_host_t* host, clap_param_rescan_flags flags)
	{
		auto* self = static_cast<Impl*>(host->host_data);
		self->rescanFlags.fetch_or(flags, std::memory_order_relaxed);
		if ((flags & CLAP_PARAM_RESCAN_ALL) == 0) { return; }
		self->paramsChanged.store(true, std::memory_order_relaxed);
		self->needsReprepare.store(true, std::memory_order_relaxed);
	}

	//! The host keeps no per-parameter references beyond the plug-in's own list (automation
	//! reaches a parameter through its model, which the next rescan re-reads), so there is
	//! nothing to drop.
	static void CLAP_ABI paramsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}

	static void CLAP_ABI paramsRequestFlush(const clap_host_t* host)
	{
		static_cast<Impl*>(host->host_data)->flushRequested.store(true, std::memory_order_relaxed);
	}

	//! A changed port layout needs a new processing setup: the next main-thread poll re-prepares.
	static void CLAP_ABI audioPortsRescan(const clap_host_t* host, std::uint32_t)
	{
		auto* self = static_cast<Impl*>(host->host_data);
		self->portsChanged.store(true, std::memory_order_relaxed);
		self->needsReprepare.store(true, std::memory_order_relaxed);
	}

	static bool CLAP_ABI audioPortsIsRescanFlagSupported(const clap_host_t*, std::uint32_t flag)
	{
		switch (flag)
		{
		case CLAP_AUDIO_PORTS_RESCAN_NAMES:
		case CLAP_AUDIO_PORTS_RESCAN_FLAGS:
		case CLAP_AUDIO_PORTS_RESCAN_CHANNEL_COUNT:
		case CLAP_AUDIO_PORTS_RESCAN_PORT_TYPE:
		case CLAP_AUDIO_PORTS_RESCAN_IN_PLACE_PAIR:
		case CLAP_AUDIO_PORTS_RESCAN_LIST:
			return true;
		default:
			return false;
		}
	}

	//! Re-read clap.audio-ports (main thread, deactivated).
	void scanAudioPorts();

	// --- the parameter list (ClapHostParams.cpp, W6) ------------------------
	//! The plug-in's parameter descriptors and cookies, in its own order.
	void readParams(std::vector<ParamDescriptor>* descriptors, std::vector<void*>* cookies) const;
	//! (Re)build the list, the ids, the cookies and the value slots (main thread: load, or deactivated).
	void scanParams();
	//! CLAP_PARAM_RESCAN_INFO: names, modules and the flags that may change in place; anything more goes the
	//! full way, through the next prepare().
	void refreshParamInfo();
	//! The rescan flags the plug-in raised since the last poll, serviced on the main thread.
	void serviceRescan(std::uint32_t flags);
	//! Main thread, deactivated (prepare()): re-read what the plug-in said changed - its ports, its parameters.
	void rescanWhileInactive();

	static void CLAP_ABI stateMarkDirty(const clap_host_t* host)
	{
		static_cast<Impl*>(host->host_data)->stateDirty.store(true, std::memory_order_relaxed);
	}

	//! Main thread: mirror the plug-in's current parameter values into the lock-free slots,
	//! without bumping the revisions (that would echo them back).
	void mirrorParamValues()
	{
		if (paramsExt == nullptr) { return; }
		for (std::size_t i = 0; i < params.size(); ++i)
		{
			double value = params[i].defaultValue;
			if (paramsExt->get_value(plugin, params[i].id, &value))
			{
				paramSlots[i].value.store(sanitizedValue(params[i], value), std::memory_order_relaxed);
			}
		}
	}

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
		const auto* state = static_cast<const EventListState*>(list->ctx);
		return state->count + state->noteCount;
	}

	static auto CLAP_ABI inEventsGet(const clap_input_events_t* list, std::uint32_t index)
		-> const clap_event_header_t*
	{
		const auto* state = static_cast<const EventListState*>(list->ctx);
		if (index < state->count) { return &state->events[index].header; }
		index -= state->count;
		if (index >= state->noteCount) { return nullptr; }
		return &state->notes[index].header;
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

		hostAudioPorts.is_rescan_flag_supported = &Impl::audioPortsIsRescanFlagSupported;
		hostAudioPorts.rescan = &Impl::audioPortsRescan;

		hostState.mark_dirty = &Impl::stateMarkDirty;
		hostLatency.changed = &Impl::latencyChanged;
		hostLog.log = &Impl::logMessage;
		hostThreadCheck.is_main_thread = &Impl::isMainThread;
		hostThreadCheck.is_audio_thread = &Impl::isAudioThread;
		hostPosixFd.register_fd = &Impl::registerFd;
		hostPosixFd.modify_fd = &Impl::modifyFd;
		hostPosixFd.unregister_fd = &Impl::unregisterFd;
		hostTimer.register_timer = &Impl::registerTimer;
		hostTimer.unregister_timer = &Impl::unregisterTimer;
		hostGui.resize_hints_changed = &Impl::guiResizeHintsChanged;
		hostGui.request_resize = &Impl::guiRequestResize;
		hostGui.request_show = &Impl::guiRequestShow;
		hostGui.request_hide = &Impl::guiRequestHide;
		hostGui.closed = &Impl::guiClosed;
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
		noteEvents.clear();
		lastSeenRevision.clear();
		eventState = {};
		inEvents = {};
		outEvents = {};
	}
};

} // namespace lmms::clap

#endif // LMMS_CLAP_HOST_INTERNALS_H
