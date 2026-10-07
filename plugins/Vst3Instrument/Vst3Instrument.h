/*
 * Vst3Instrument.h - native VST3 instrument host
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

#ifndef LMMS_VST3_INSTRUMENT_H
#define LMMS_VST3_INSTRUMENT_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <QDomDocument>
#include <QDomElement>

#include "AudioPlugin.h"
#include "InstrumentOutputBuses.h"
#include "Vst3Host.h"
#include "PluginNativeEditor.h"
#include "Vst3Parameter.h"

class QTimer;

namespace lmms
{

namespace gui
{
class PluginView;
}

/**
 * A VST3 instrument on one InstrumentTrack.
 *
 * Shape (and why):
 *  - `AudioPlugin<Instrument, ...>` already provides the transport, the
 *    AudioPorts router and the "MIDI-based instrument" contract
 *    (include/AudioPlugin.h:134-207), so this class only has to translate.
 *  - The instrument declares NO audio input, like every VST3 instrument, and
 *    a dynamic number of output channels. The host substitutes silence for
 *    any audio input bus a plug-in asks for but the track does not provide.
 *  - MIDI arrives through Instrument::handleMidiEvent(), which the track calls
 *    from its existing path (src/tracks/InstrumentTrack.cpp:467-522), and is
 *    handed to Vst3Host's lock free MIDI queue. The queue is drained into
 *    ProcessData::inputEvents with the sample offset LMMS already computed, so
 *    an event at frame N affects exactly frame N.
 *  - `IsMidiBased` is what tells the engine this instrument is driven by MIDI
 *    events rather than by NotePlayHandles (include/Instrument.h:61);
 *    `IsSingleStreamed` says it renders one stream for all of its notes. The
 *    AudioPlugin instrument base documents that playNote() is then a no-op
 *    (include/AudioPlugin.h:192-203) - and, crucially,
 *    InstrumentPlayHandle::play() processes the track's NotePlayHandles
 *    *before* it calls play(), which is why the MIDI for a block is already
 *    queued by the time processImpl() drains it
 *    (src/core/InstrumentPlayHandle.cpp:43-69).
 */
inline constexpr AudioPortsSettings Vst3InstrumentAudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = DynamicChannelCount,
	.outputs = DynamicChannelCount,
	.inplace = false,
	.buffered = false};

class Vst3Instrument : public AudioPluginExt<Instrument, Vst3InstrumentAudioPortsSettings>
{
	Q_OBJECT
public:
	Vst3Instrument(InstrumentTrack* parent, const Descriptor::SubPluginFeatures::Key* key);
	~Vst3Instrument() override;

	auto vst3Plugin() -> vst3::HostedPlugin* { return &m_plugin; }

	//! R4.4: the plug-in's own editor, in its own window (PluginNativeEditor + Vst3EditorSession).
	bool openNativeEditor(QString* error) override;
	void closeNativeEditor() override;
	bool nativeEditorOpen() const override;

	auto paramModels() const -> const std::vector<Vst3ParamModel*>& { return m_paramModels; }
	auto modelForParam(std::uint32_t id) -> Vst3ParamModel*;

	//! GUI thread: re-create the processing setup, e.g. after a sample rate
	//! change. Not real-time safe, never call from the audio thread.
	void reprepare();

	//! Set from the audio thread when the plug-in is not prepared; polled on
	//! the GUI thread, which then calls reprepare().
	auto needsReprepare() const -> bool
	{
		return m_needsReprepare.load(std::memory_order_relaxed);
	}

	//! The plug-in's own state, written under <instrument>, next to the key.
	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	auto nodeName() const -> QString override { return QStringLiteral("vst3instrument"); }

	//! The track's MIDI path. Lock free and allocation free: it only fills a
	//! POD and pushes it into the host's bounded queue.
	bool handleMidiEvent(const MidiEvent& event, const TimePos& time = TimePos(),
		f_cnt_t offset = 0) override;

	//! 0.5.0 MX-114: the plug-in's output buses past the main one (an aux bus is activated when routed).
	auto outputBuses() -> InstrumentOutputBuses* override { return &m_outputBuses; }
	//! 0.5.0 MX-120: a parameter a note-expression type stands for takes per-note values.
	auto perNoteTarget(const AutomatableModel* parameter) const -> std::int64_t override;
	void modulateNote(std::int64_t target, int channel, int key, float offset, float normalized) override;

protected:
	auto processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
		PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus override;

	auto instantiateView(QWidget* parent) -> gui::PluginView* override;

private slots:
	//! GUI thread: pull plug-in side parameter changes into the models and
	//! service the audio thread's re-prepare request.
	void poll();

private:
	//! Audio thread: hand the notes the plug-in wrote this block to the track's MIDI output.
	void forwardOutputEvents();
	//! The transport's channel counts for the plug-in's current bus layout.
	void applyChannelCounts();
	//! MX-114: activates output buses 1..@a bus (re-preparing the plug-in) when a route asks for one.
	bool enableOutputBus(int bus);
	/*! MX-157 (BUGS 11.34), GUI thread: the plug-in's latency (IAudioProcessor::getLatencySamples) after
	 *  its last prepare, reported to the track's delay compensation (Instrument::reportLatency). A
	 *  kLatencyChanged from the plug-in only raises m_latencyChanged - it may come from any thread - and
	 *  poll() reads the new value. */
	void cacheLatency();
	vst3::HostedPlugin m_plugin;
	gui::PluginNativeEditor m_editor;
	std::vector<Vst3ParamModel*> m_paramModels;
	QTimer* m_pollTimer = nullptr;
	bool m_syncing = false;
	std::atomic<bool> m_needsReprepare{false};
	InstrumentOutputBuses m_outputBuses;  //!< MX-114
	std::atomic<bool> m_noteIds{false};   //!< MX-120: the plug-in takes note expressions, so notes carry ids
	std::atomic<bool> m_latencyChanged{false};  //!< MX-157: kLatencyChanged arrived; poll() re-reads
};

} // namespace lmms

#endif // LMMS_VST3_INSTRUMENT_H
