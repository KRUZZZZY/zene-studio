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

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <cstddef>
#include <optional>
#include <thread>
#include <vector>

#include <QByteArray>
#include <QString>

#include "PluginHostChunking.h"
#include "Vst3BusMap.h"
#include "Vst3MidiQueue.h"
#include "Vst3ParamDescriptor.h"

namespace Steinberg::Vst { class IEditController; }
namespace lmms { enum class PluginWindowSystem; }  // PluginWindowSystem.h

namespace lmms::vst3
{

class Vst3EditorSession;

//! 0.5.0 MX-094: how the open editor took the device's zoom (HostedPlugin::setEditorScale).
enum class EditorScaling
{
	Unscaled,  //!< the zoom is 1, or the view could neither scale itself nor be resized
	ByView,    //!< the view scales itself (IPlugViewContentScaleSupport) and asked for its size
	ByWindow   //!< the host gave the view the scaled size, through the view's own size constraint
};

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

/*! Lock-free admission gate between audio processing and GUI-side re-prepare.
 *
 * The audio side only performs bounded atomic operations. A reconfiguration
 * closes admission and waits off the audio thread for every admitted call to
 * leave before it mutates HostedPlugin's prepared buffers.
 */
class AudioProcessGate
{
public:
	static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
		"VST3 audio process admission must be lock free");

	//! Audio thread: enter only if no reconfiguration is pending.
	auto tryEnterAudioProcess() noexcept -> bool
	{
		if (m_reconfiguring.load(std::memory_order_seq_cst)) { return false; }
		m_activeCalls.fetch_add(1, std::memory_order_seq_cst);
		if (m_reconfiguring.load(std::memory_order_seq_cst))
		{
			m_activeCalls.fetch_sub(1, std::memory_order_seq_cst);
			return false;
		}
		return true;
	}

	//! Audio thread: leave after the last HostedPlugin access in the call.
	void leaveAudioProcess() noexcept
	{
		m_activeCalls.fetch_sub(1, std::memory_order_seq_cst);
	}

	//! GUI/control thread only: stop admission and wait for existing calls to leave.
	void beginReconfigure() noexcept
	{
		m_reconfiguring.store(true, std::memory_order_seq_cst);
		while (m_activeCalls.load(std::memory_order_seq_cst) != 0)
		{
			std::this_thread::yield();
		}
	}

	//! GUI/control thread only: reopen admission after prepare/cache work is complete.
	void endReconfigure() noexcept
	{
		m_reconfiguring.store(false, std::memory_order_seq_cst);
	}

	//! Diagnostic state used by the focused concurrency regression.
	auto reconfigurationPending() const noexcept -> bool
	{
		return m_reconfiguring.load(std::memory_order_seq_cst);
	}

private:
	std::atomic<std::uint32_t> m_activeCalls{0};
	std::atomic<bool> m_reconfiguring{false};
};

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
	//! 0.5.0 MX-114: every audio OUTPUT bus the plug-in declares, active or not, in its own order.
	auto outputBusDescriptors() const -> const std::vector<BusDescriptor>&;
	/*! Activates output buses 1..@a upTo - a prefix, so ProcessData's bus array keeps the plug-in's own
	 *  bus indexes - and re-reads the layout. GUI thread, and only while not prepared (VST3 activates
	 *  buses on an inactive component). False when the component refuses one. */
	auto activateOutputBuses(int upTo) -> bool;
	/*! 0.5.0 MX-120: reads the note-expression types the plug-in declares on its event input bus
	 *  (INoteExpressionController, channel 0). GUI thread; none when it has no such controller. */
	void refreshNoteExpressions();
	auto hasNoteExpressions() const -> bool;
	//! The note-expression type for parameter @a id (its associated parameter, else the same title), or -1.
	auto noteExpressionFor(std::uint32_t id, const QString& title) const -> std::int64_t;
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
	//! Output notes dropped because one request wrote more than kMaxMidiEventsPerBlock. Diagnostics only.
	auto droppedOutputEvents() const -> std::uint64_t;

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
	//! GUI thread: the plug-in's processing latency in samples (IAudioProcessor::
	//! getLatencySamples), 0 while not prepared. BUGS 11.34.
	auto latency() const -> std::uint32_t;
	//! GUI thread: the plug-in's tail in frames (getTailSamples), -1 for infinite (E6).
	auto tail() const -> int;
	//! Audio thread, after process(): the notes the plug-in wrote to its output event list during
	//! that request, as MIDI with block frame offsets (engine audit E7).
	auto outputEventCount() const -> int;
	auto outputEvent(int index) const -> const MidiEventIn&;
	//! GUI thread: @a callback runs when the plug-in reports kLatencyChanged.
	void setLatencyChangedCallback(std::function<void()> callback);

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
	 *  - parameter changes are delivered once each: a host change at the start
	 *    of the request, a mapped MIDI controller (IMidiMapping) at its own
	 *    frame, bounded to [0, frames - 1], in the chunk that frame falls in and
	 *    rebased to it (0.5.0 W4), so no point lands past numSamples;
	 *    MIDI events are delivered in the chunk their sample offset falls in,
	 *    with the offset rebased to that chunk, so every event offset lies in
	 *    [0, chunk). An event whose offset is at or beyond the end of the
	 *    request is carried into the NEXT request and delivered there at offset
	 *    0, so it is still seen exactly once: a note-off written at a block
	 *    boundary releases the note, and no event is ever handed to the plug-in
	 *    at sampleOffset == numSamples.
	 *
	 * A request of 0 frames does nothing at all. If there are frames but the
	 * host is not prepared (or has been released), every supplied output channel
	 * is cleared to silence and the plug-in is not called.
	 */
	void process(const float* const* inputs, float* const* outputs,
		int numInputs, int numOutputs, int frames);

#ifdef LMMS_TESTING
	//! Test-only: how many events the LAST chunk handed to the plug-in carried.
	auto lastChunkEventCount() const -> int;
	//! Test-only: sample offset of that chunk's event @a index, or -1 when there is none. A VST3
	//! event offset is always in [0, chunk length).
	auto lastChunkEventOffset(int index) const -> int;
#endif

	//! GUI thread: plug-in rendered string for a normalized value
	auto paramDisplayValue(std::uint32_t id, float normalized) const -> QString;

	// ---- R4.2: the plug-in's own editor (Vst3EditorSession.h) --------------------------
	//! Main thread: attach the plug-in's editor to @a parentWindow - the platform's native window
	//! (Q050-490: an X11 window id, an HWND or an NSView*, whatever QWidget::winId() gives there).
	auto openEditor(void* parentWindow, QString* error) -> bool;
	//! Q050-490: the window system the next openEditor() embeds in, instead of the running one, so a
	//! test proves the Windows and macOS selection on any machine. Nothing in the product calls it.
	void setEditorWindowSystem(PluginWindowSystem system);
	void closeEditor();
	auto editorOpen() const -> bool;
	auto resizeEditor(int width, int height) -> bool;
	void editorSize(int* width, int* height) const;
	//! Timers the open editor registered through Linux::IRunLoop, and onTimer calls delivered.
	auto editorTimerCount() const -> int;
	auto editorTimerDeliveries() const -> std::uint32_t;
	//! 0.5.0 MX-094: the zoom the editor opens at - the device's editor-scale option. 1 (the default)
	//! asks nothing of the view; it applies at the next openEditor(), as the CLAP host's does.
	void setEditorScale(double scale);
	auto editorScaling() const -> EditorScaling;

private:
	struct Impl;
	//! R4.2: the plug-in's edit controller, for the editor session (Vst3EditorSession.cpp).
	auto editController() const -> Steinberg::Vst::IEditController*;
	std::unique_ptr<Impl> m_impl;
	//! R4.2: the open editor, if any. Declared after m_impl so it is destroyed first, and
	//! reset explicitly in the destructor before the controller is released.
	std::unique_ptr<Vst3EditorSession> m_editor;
	double m_editorScale = 1.0;
	std::optional<PluginWindowSystem> m_editorWindowSystem;
};

} // namespace lmms::vst3

#endif // LMMS_VST3_HOST_H
