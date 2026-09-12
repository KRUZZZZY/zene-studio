/*
 * Vst3Instrument.h - native VST3 instrument host
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

#ifndef LMMS_VST3_INSTRUMENT_H
#define LMMS_VST3_INSTRUMENT_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <QDomDocument>
#include <QDomElement>

#include "AudioPlugin.h"
#include "Vst3Host.h"
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

protected:
	auto processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
		PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus override;

	auto instantiateView(QWidget* parent) -> gui::PluginView* override;

private slots:
	//! GUI thread: pull plug-in side parameter changes into the models and
	//! service the audio thread's re-prepare request.
	void poll();

private:
	vst3::HostedPlugin m_plugin;
	std::vector<Vst3ParamModel*> m_paramModels;
	QTimer* m_pollTimer = nullptr;
	bool m_syncing = false;
	std::atomic<bool> m_needsReprepare{false};
};

} // namespace lmms

#endif // LMMS_VST3_INSTRUMENT_H
