/*
 * Vst3Host.h - in-process VST3 host
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

#ifndef LMMS_VST3_HOST_H
#define LMMS_VST3_HOST_H

#include <cstdint>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QString>

#include "Vst3BusMap.h"
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

//! One MIDI event on its way from LMMS' MIDI path to a plug-in's input event
//! bus. Plain data by design: the queue between the two is a fixed-size array
//! of these, so an audio block that carries MIDI never allocates.
struct MidiEventIn
{
	std::uint8_t type = 0;        //!< lmms::MidiEventTypes (0x80 .. 0xEF)
	std::uint8_t channel = 0;     //!< 0 .. 15
	std::uint8_t data0 = 0;       //!< note key / controller number
	std::uint8_t data1 = 0;       //!< note velocity / controller value
	std::int32_t frameOffset = 0; //!< frames from the start of the block
};

//! How many MIDI events one audio block can carry. Fixed at compile time so
//! the audio thread sizes its VST3 event list once, in prepare(), and never
//! grows it while processing.
inline constexpr int kMaxMidiEventsPerBlock = 256;

//! Bound on the queue between the MIDI path and the audio thread. A power of
//! two: the ring masks its index. A full queue drops the event and counts it
//! (see droppedMidiEvents()) instead of growing or blocking.
inline constexpr std::size_t kMidiQueueCapacity = 1024;

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
