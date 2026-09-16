/*
 * ClapHost.h - in-process CLAP host for LMMS
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

#ifndef LMMS_CLAP_HOST_H
#define LMMS_CLAP_HOST_H

#include <cstdint>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QString>

#include "ClapBusMap.h"
#include "ClapLoader.h"
#include "ClapNoteQueue.h"
#include "ClapParamDescriptor.h"
#include "PluginHostChunking.h"

namespace lmms::clap
{

//! The parameter description lives in the plug-in namespace (like the VST3
//! lane's Vst3ParamDescriptor) so that the host and the controls share it.
using ParamDescriptor = ClapParamDescriptor;

//! Static identity of one plug-in class inside a CLAP module.
struct ClassInfo
{
	QString id;
	QString name;
	QString vendor;
	QString version;
	QString description;
	bool isInstrument = false;
};

/*!
 * Lists every plug-in class inside a CLAP module. Loads the module, queries the
 * clap.plugin-factory extension and unloads it again. Main thread only.
 */
auto listClasses(const QString& modulePath, QString* error) -> std::vector<ClassInfo>;

/*!
 * The same scan, reporting WHY a module could not be read as a typed code
 * (`status` receives Code::None when it could). The failures that look alike in
 * prose -- no such file, no `clap_entry`, a CLAP 0.x module, an init() that
 * fails -- are distinct codes, which is what a scan of a user's plug-in
 * directory has to report to be actionable. See ClapLoader.h.
 */
auto listClasses(const QString& modulePath, loader::Status* status, QString* error)
	-> std::vector<ClassInfo>;

/*! The counters behind HostedPlugin::process(), shared with the control
 * surface: the engine half is include/PluginHostChunking.h (core), because the
 * host is a plugin module and the `plugin.host_chunking` command is not.
 */
using HostChunkingStats = control::PluginHostChunkingStats;

//! Process-wide counters behind HostedPlugin::process(). Any thread.
auto hostChunkingStats() -> HostChunkingStats;

/*!
 * In-process host for one CLAP plug-in instance. Mirrors the VST3 lane's
 * HostedPlugin: all lifetime and state calls happen on the main thread, while
 * process() and the parameter setters are safe to call from the audio thread.
 */
class HostedPlugin
{
public:
	HostedPlugin();
	~HostedPlugin();

	HostedPlugin(const HostedPlugin&) = delete;
	HostedPlugin& operator=(const HostedPlugin&) = delete;

	// --- main thread ------------------------------------------------------
	auto load(const QString& modulePath, const QString& pluginId, QString* error) -> bool;
	void unload();
	auto isLoaded() const -> bool;
	//! The typed reason the last load() failed: Code::None when it did not.
	//! Where `error` carries the sentence a user reads, this is what a caller
	//! matches on -- see ClapLoader.h for the codes and their meaning.
	auto lastLoadFailure() const -> const loader::Status&;
	auto classInfo() const -> const ClassInfo&;
	auto className() const -> QString;
	auto vendor() const -> QString;
	auto isInstrument() const -> bool;
	auto parameters() const -> const std::vector<ParamDescriptor>&;
	auto ports() const -> const std::vector<PortDescriptor>&;
	auto busLayout() const -> const PortLayout&;
	auto latency() const -> std::uint32_t;

	//! The plug-in's note INPUT ports (clap.note-ports), in the plug-in's own
	//! index order. Empty for a plug-in that does not implement the extension
	//! - which is every effect, and an instrument that takes no notes - so an
	//! empty list is a fact about the plug-in, not a failure.
	auto noteInputPorts() const -> const std::vector<NotePortDescriptor>&;
	//! True when the plug-in declared at least one note input port. What
	//! tells the instrument host whether a track's MIDI is worth queueing.
	auto acceptsNotes() const -> bool;
	//! The index (into noteInputPorts(), i.e. the plug-in's own port index)
	//! notes are delivered to: the port that declared it prefers the CLAP
	//! dialect, else the first one.
	auto preferredNotePort() const -> std::uint32_t;
	//! What the note path has done so far. Any thread.
	auto noteCounters() const -> NoteCounters;

	auto paramIndex(std::uint32_t id) const -> int;
	auto paramPlain(std::uint32_t id) const -> double;
	auto paramNormalized(std::uint32_t id) const -> float;
	auto paramDisplayValue(std::uint32_t id, double plainValue) const -> QString;

	//! Persists the plug-in state through clap.state (empty if unsupported).
	auto saveState(QByteArray* state) const -> bool;
	//! Restores the plug-in state and re-reads the parameter values.
	auto loadState(const QByteArray& state) -> bool;

	auto prepare(double sampleRate, int maxBlockSize, QString* error) -> bool;
	void release();
	auto isPrepared() const -> bool;

	//! The plug-in asked for a restart (block size/sample rate change).
	auto needsReprepare() const -> bool;
	void clearNeedsReprepare();
	//! The plug-in asked to be called on the main thread; poll from the GUI.
	auto takeCallbackRequest() -> bool;

	void setTempo(double bpm);
	void setTransportPlaying(bool playing);

	// --- audio thread -----------------------------------------------------
	//! Writes the value of one parameter into the plug-in's event stream.
	//! Lock-free and allocation-free; the change is delivered with the next
	//! process() call.
	void setParamPlain(std::uint32_t id, double value);
	void setParamNormalized(std::uint32_t id, float normalized);

	/*!
	 * Queues one note for the plug-in's note input port. Audio thread safe:
	 * it fills a POD and pushes it into a bounded lock-free queue, so nothing
	 * allocates and nothing locks; the event is delivered with the chunk that
	 * starts the next process() call, at `frameOffset` frames into it.
	 *
	 * A plug-in with no note input port (acceptsNotes() false) refuses the
	 * event rather than queue it, and counts the refusal - see
	 * noteCounters(). A full queue drops the event and counts it too.
	 *
	 * @param velocity for a note-on: 0..1 (CLAP's own scale), 0 is silent
	 */
	void setNoteOn(std::uint8_t channel, std::int16_t key, double velocity,
		std::int32_t frameOffset = 0);
	void setNoteOff(std::uint8_t channel, std::int16_t key, std::int32_t frameOffset = 0);
	//! A note choke: silence `key` without a release stage (clap_event_note's
	//! CLAP_EVENT_NOTE_CHOKE). Ignored in velocity terms.
	void setNoteChoke(std::uint8_t channel, std::int16_t key, std::int32_t frameOffset = 0);

	/*!
	 * Processes one block. inputs/outputs are planar channel pointer arrays;
	 * channels beyond the plug-in's port layout are ignored on input and left
	 * untouched on output, missing channels are fed silence. Real-time safe:
	 * no allocation, no locking, no I/O.
	 *
	 * OVER-RUN AND TAIL RULE. The host processes EXACTLY `frames` frames of
	 * every channel the caller supplies, and never asks the plug-in for more
	 * than the block size it was activated with:
	 *  - the request is split into chunks of at most that block size; every
	 *    chunk but the last is exactly that long and the last carries the
	 *    remainder, so a request that is not a multiple of the prepared block
	 *    is processed whole rather than truncated (which is what the
	 *    `frames = min(frames, maxFrames)` clamp used to do: it silently left
	 *    the tail of the caller's buffers holding whatever was there before) ;
	 *  - before the call returns, every frame of [0, frames) has been written
	 *    on every channel the caller passed, and on no other memory: a channel
	 *    the caller does not supply reads from a zeroed block and writes to a
	 *    scratch block sized per channel as the prepared block, and a chunk's
	 *    window into them is never longer than that;
	 *  - the plug-in sees one process() call per chunk with
	 *    clap_process_t::frames_count == that chunk's length;
	 *  - parameter changes are delivered with the chunk that starts the
	 *    request, and with that chunk only.
	 *
	 * A request of 0 frames is a no-op and returns true. A chunk that returns
	 * CLAP_PROCESS_ERROR stops the request: the remaining frames are left
	 * untouched and the call returns false, so the caller can fall back to dry
	 * audio for the whole block it asked for.
	 */
	auto process(const float* const* inputs, float* const* outputs, int inputChannels,
		int outputChannels, int frames) -> bool;

private:
	//! The one insertion point into the note queue: refuses (and counts) an
	//! event when the plug-in has no note input port, and counts a full queue.
	void pushNote(const NoteEventIn& event);

	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace lmms::clap

#endif // LMMS_CLAP_HOST_H
