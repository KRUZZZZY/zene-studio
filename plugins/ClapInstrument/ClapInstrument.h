/*
 * ClapInstrument.h - native CLAP instrument host
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

#ifndef LMMS_CLAP_INSTRUMENT_H
#define LMMS_CLAP_INSTRUMENT_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <QDomDocument>
#include <QDomElement>

#include "AudioPlugin.h"
#include "ClapHost.h"
#include "ClapParameter.h"

class QTimer;

namespace lmms
{

namespace gui
{
class PluginView;
}

/**
 * A CLAP instrument on one InstrumentTrack (feature row 79, board task #669).
 *
 * Shape (the VST3 instrument lane's, plugins/Vst3Instrument/Vst3Instrument.h):
 *  - `AudioPluginExt<Instrument, ...>` provides the transport, the AudioPorts
 *    router and the "MIDI-based instrument" contract (include/AudioPlugin.h),
 *    so this class only translates.
 *  - The instrument declares NO audio input port - a generator's own shape,
 *    which clap.note-ports + clap.audio-ports describe - and a dynamic number
 *    of output channels; the host substitutes silence for any input bus a
 *    plug-in asks for but the track does not provide.
 *  - Notes arrive through Instrument::handleMidiEvent(), which the track calls
 *    from its existing path (src/tracks/InstrumentTrack.cpp), and are handed
 *    to the CLAP host's lock-free note queue. The queue is drained into the
 *    plug-in's input event list with the sample offset LMMS already computed,
 *    so a note at frame N affects exactly frame N.
 *  - `IsMidiBased` is what tells the engine this instrument is driven by MIDI
 *    events rather than by NotePlayHandles (include/Instrument.h);
 *    `IsSingleStreamed` says it renders one stream for all of its notes.
 *
 * The lifecycle discipline is the CLAP effect host's, unchanged: load() while
 * deactivated, prepare()/release() on the main thread only, process() and the
 * note/parameter setters on the audio thread only, a restart request from the
 * plug-in recorded by an atomic and serviced by reprepare() on the GUI thread,
 * and the plug-in's own state (clap.state) carried in the .mmp.
 */
inline constexpr AudioPortsSettings ClapInstrumentAudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = DynamicChannelCount,
	.outputs = DynamicChannelCount,
	.inplace = false,
	.buffered = false};

class ClapInstrument : public AudioPluginExt<Instrument, ClapInstrumentAudioPortsSettings>
{
	Q_OBJECT
public:
	ClapInstrument(InstrumentTrack* parent, const Descriptor::SubPluginFeatures::Key* key);
	~ClapInstrument() override;

	auto clapPlugin() -> clap::HostedPlugin* { return &m_plugin; }

	auto paramModels() const -> const std::vector<ClapParamModel*>& { return m_paramModels; }
	auto modelForParam(std::uint32_t id) -> ClapParamModel*;

	//! GUI thread: re-create the processing setup, e.g. after a sample rate
	//! change. Not real-time safe, never call from the audio thread.
	void reprepare();

	//! Set from the audio thread when the plug-in is not prepared; polled on
	//! the GUI thread, which then calls reprepare().
	auto needsReprepare() const -> bool
	{
		return m_needsReprepare.load(std::memory_order_relaxed);
	}

	//! The plug-in's own state, written under <clapinstrument>, next to the key.
	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	auto nodeName() const -> QString override { return QStringLiteral("clapinstrument"); }

	//! The track's MIDI path. Lock free and allocation free: it only fills a
	//! POD and pushes it into the host's bounded note queue.
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
	clap::HostedPlugin m_plugin;
	std::vector<ClapParamModel*> m_paramModels;
	QTimer* m_pollTimer = nullptr;
	bool m_syncing = false;
	std::atomic<bool> m_needsReprepare{false};
};

} // namespace lmms

#endif // LMMS_CLAP_INSTRUMENT_H
