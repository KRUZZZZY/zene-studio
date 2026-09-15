/*
 * Vst3Host.h - in-process VST3 host
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

#ifndef LMMS_VST3_HOST_H
#define LMMS_VST3_HOST_H

#include <cstdint>
#include <memory>
#include <cstddef>
#include <vector>

#include <QByteArray>
#include <QString>

#include "PluginHostChunking.h"
#include "Vst3BusMap.h"
#include "Vst3MidiQueue.h"
#include "Vst3ParamDescriptor.h"

namespace lmms::vst3
{

//! A VST3 class as reported by a module's plug-in factory
struct Vst3ClassInfo
{
	QString name;
	QString category;
	QString subCategories;
	QString vendor;
	bool isInstrument = false;
};

//! Enumerates the audio classes of a VST3 module. GUI thread, allocates.
auto listClasses(const QString& modulePath, QString* error) -> std::vector<Vst3ClassInfo>;

/*! The counters behind HostedPlugin::process(), shared with the control
 * surface: the engine half is include/PluginHostChunking.h (core), because the
 * host is a plugin module and the `plugin.host_chunking` command is not.
 */
using HostChunkingStats = control::PluginHostChunkingStats;

//! Process-wide counters behind HostedPlugin::process(). Any thread.
auto hostChunkingStats() -> HostChunkingStats;

//! The MIDI queue every instrument's events travel through, and the plain data
//! they are made of (Vst3MidiQueue.h). The queue itself is an implementation
//! detail: callers only need pushMidiEvent().
/**
 * Hosts one VST3 audio class in-process.
 *
 * Threading contract:
 *  - load()/prepare()/release()/saveState()/loadState()/paramDisplayValue()
 *    are GUI thread operations and may allocate.
 *  - setParamNormalized() is lock free and may be called from any thread.
 *  - pushMidiEvent() is lock free and may be called from any thread.
 *  - process() is the audio thread path: it allocates nothing and takes no
 *    locks. All buffers, parameter queues, the VST3 event list and the MIDI
 *    queue are allocated in prepare().
 */
class HostedPlugin
{
public:
	HostedPlugin();
	~HostedPlugin();

	HostedPlugin(const HostedPlugin&) = delete;
	auto operator=(const HostedPlugin&) -> HostedPlugin& = delete;

	//! Loads `classId` from the module at `modulePath`. If `classId` is empty
	//! the first audio effect class is used.
	auto load(const QString& modulePath, const QString& classId, QString* error) -> bool;

	auto parameters() const -> const std::vector<Vst3ParamDescriptor>&;
	auto busLayout() const -> const BusLayout&;
	auto className() const -> QString;
	auto vendor() const -> QString;
	auto isInstrument() const -> bool;
	//! True once load() succeeded
	auto isLoaded() const -> bool;

	//! True when MIDI pushed with pushMidiEvent() reaches the loaded plug-in:
	//! it is an instrument AND it declares an input event bus. Always false
	//! for an effect, whose ProcessData::inputEvents stays nullptr exactly as
	//! it was before the MIDI path existed.
	auto receivesMidi() const -> bool;
	//! The input event bus index the host drives, or -1 when there is none.
	//! A plug-in may declare several event inputs; this slice drives the first
	//! bus that is active by default (and deactivates the others), which is
	//! the policy written down in docs/VST3-INSTRUMENT-HOSTING.md.
	auto eventInputBusIndex() const -> int;
	//! Lock free and allocation free. Callable from the audio thread (a note
	//! handle built during the current period) and from the MIDI/GUI thread.
	//! `event.frameOffset` is in frames from the start of the block the
	//! plug-in processes next.
	void pushMidiEvent(const MidiEventIn& event);
	//! MIDI events dropped because the queue was full. Diagnostics only.
	auto droppedMidiEvents() const -> std::uint64_t;

	//! Lock free, any thread
	void setParamNormalized(std::uint32_t id, float normalized);
	//! Lock free, any thread
	auto paramNormalized(std::uint32_t id) const -> float;
	//! -1 if the plug-in has no such parameter
	auto paramIndex(std::uint32_t id) const -> int;
	//! GUI thread: also informs the edit controller (GUI-side value mirror)
	void notifyController(std::uint32_t id, float normalized);

	auto saveState(QByteArray* componentState, QByteArray* controllerState) -> bool;
	auto loadState(const QByteArray& componentState, const QByteArray& controllerState) -> bool;

	//! GUI thread. May be called again after release() when the sample rate
	//! or the block size changed.
	auto prepare(double sampleRate, int maxBlockSize, QString* error) -> bool;
	//! GUI thread
	void release();
	auto isPrepared() const -> bool;
	auto preparedSampleRate() const -> double;

	void setTempo(double bpm);
	void setTransportPlaying(bool playing);

	//! Audio thread. `inputs`/`outputs` hold `numInputs`/`numOutputs` planar
	//! channels of `frames` samples each.
	/**
	 * Over-run and tail rule.
	 *
	 * The host processes EXACTLY `frames` frames of every channel the caller
	 * supplies, and it never asks the plug-in for more than the block size it
	 * was prepared with:
	 *  - the request is split into chunks of at most the prepare() block size;
	 *    every chunk but the last is exactly that long and the last carries the
	 *    remainder, so a request that is not a multiple of the prepared block is
	 *    processed whole rather than truncated;
	 *  - before the call returns, every frame of [0, frames) has been written on
	 *    every channel the caller passed, and on no other memory: a channel the
	 *    caller does not supply reads from a zeroed block and writes to a scratch
	 *    block, both exactly the prepared block size, and a chunk's window into
	 *    them is never longer than that;
	 *  - the plug-in sees one process() call per chunk with
	 *    ProcessData::numSamples == that chunk's length. It is never handed a
	 *    buffer shorter than the frames it is asked for (which is what an
	 *    unclamped request would do to the scratch buffers) and it is never
	 *    asked for more frames than it declared in setupProcessing();
	 *  - parameter changes are delivered once, at the start of the request;
	 *    MIDI events are delivered in the chunk their sample offset falls in,
	 *    with the offset rebased to that chunk, and an event whose offset is at
	 *    or beyond the end of the request is delivered with the LAST chunk at
	 *    that chunk's end offset - exactly the `std::clamp(offset, 0, frames)`
	 *    the host applied before it chunked, so a note-off written at a block
	 *    boundary still releases the note.
	 *
	 * A request of 0 frames does nothing at all. Calling process() before
	 * prepare() (or after release()) does nothing at all.
	 */
	void process(const float* const* inputs, float* const* outputs,
		int numInputs, int numOutputs, int frames);

	//! GUI thread: plug-in rendered string for a normalized value
	auto paramDisplayValue(std::uint32_t id, float normalized) const -> QString;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace lmms::vst3

#endif // LMMS_VST3_HOST_H
