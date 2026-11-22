/*
 * AuInstrument.h - an Audio Unit (AUv2) instrument on an instrument track (Q050-491, macOS only)
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
 */

#ifndef LMMS_AU_INSTRUMENT_H
#define LMMS_AU_INSTRUMENT_H

#include <atomic>

#include <QDomDocument>
#include <QDomElement>

#include "AudioPlugin.h"
#include "AuHost.h"
#include "AuParameter.h"
#include "PluginNativeEditor.h"

class QTimer;

namespace lmms
{

inline constexpr AudioPortsSettings AuInstrumentAudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = DynamicChannelCount,
	.outputs = DynamicChannelCount,
	.inplace = false,
	.buffered = false};

/*! An aumu component on an instrument track. MIDI-based and single-streamed, like the CLAP and VST3
 *  instruments: the track's MIDI (clip notes on the audio thread, live input on the MIDI thread) goes
 *  through handleMidiEvent() into the host's bounded lock-free queue, and process() hands each message to
 *  MusicDeviceMIDIEvent at its frame offset before rendering the block. No audio input; stereo out. */
class AuInstrument : public AudioPluginExt<Instrument, AuInstrumentAudioPortsSettings>
{
	Q_OBJECT
public:
	AuInstrument(InstrumentTrack* parent, const Descriptor::SubPluginFeatures::Key* key);
	~AuInstrument() override;
	QString unavailableReason() const override { return m_unit.isLoaded() ? QString() : m_loadError; }

	auto unit() -> au::HostedUnit* { return &m_unit; }
	auto paramModels() const -> const std::vector<AuParamModel*>& { return m_params.models(); }

	bool openNativeEditor(QString* error) override;
	void closeNativeEditor() override;
	bool nativeEditorOpen() const override;

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	auto nodeName() const -> QString override { return QStringLiteral("auinstrument"); }

	//! Lock free and allocation free: one bounded push.
	bool handleMidiEvent(const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0) override;

	//! GUI thread: initialize the unit again (a new sample rate, or after a failed prepare).
	void reprepare();

protected:
	auto processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
		PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus override;
	auto instantiateView(QWidget* parent) -> gui::PluginView* override;

private slots:
	void poll();

private:
	au::HostedUnit m_unit;
	QString m_loadError;
	gui::PluginNativeEditor m_editor;
	AuParamModelList m_params;
	QTimer* m_pollTimer = nullptr;
	std::atomic<bool> m_needsReprepare{false};
};

} // namespace lmms

#endif // LMMS_AU_INSTRUMENT_H
