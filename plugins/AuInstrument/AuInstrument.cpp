/*
 * AuInstrument.cpp - an Audio Unit (AUv2) instrument on an instrument track (Q050-491, macOS only)
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

#include "AuInstrument.h"

#include <algorithm>
#include <exception>

#include <QDebug>
#include <QTimer>

#include "AuInstrumentView.h"
#include "AuSubPluginFeatures.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "HostedTrackChannels.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
#include "LmmsCommonMacros.h"
#include "MidiEvent.h"
#include "PluginInstantiationFailure.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT auinstrument_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Audio Unit",
	QT_TRANSLATE_NOOP("PluginBrowser", "Audio Unit (AUv2) instrument host"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	new AuSubPluginFeatures(Plugin::Type::Instrument)
};

// A key-less instantiation (the browser's) still gives an instrument; a component that does not
// instantiate answers nullptr with its typed reason noted, which plugin.load reports.
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	try
	{
		const auto* key = static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data);
		auto instrument = std::make_unique<AuInstrument>(static_cast<InstrumentTrack*>(parent), key);
		if (key == nullptr || instrument->unit()->isLoaded()) { return instrument.release(); }
		pluginload::noteInstantiationFailure(instrument->unavailableReason());
		return nullptr;
	}
	catch (const std::exception& error)
	{
		pluginload::noteInstantiationFailure(QStringLiteral("the Audio Unit instrument could not be created: %1")
			.arg(QString::fromUtf8(error.what())));
		return nullptr;
	}
}

}

AuInstrument::AuInstrument(InstrumentTrack* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPluginExt<Instrument, AuInstrumentAudioPortsSettings>(&auinstrument_plugin_descriptor, parent, key,
		Instrument::Flag::IsSingleStreamed | Instrument::Flag::IsMidiBased),
	m_params(&m_unit, this)
{
	au::ComponentId id;
	const QString component = key != nullptr ? key->attributes.value(QStringLiteral("component")) : QString();
	if (!au::parseComponentKey(component, &id))
	{
		m_loadError = au::instantiationError(id, component, QStringLiteral("its key names no component"), 0);
	}
	else if (!m_unit.load(id, &m_loadError)) { qWarning() << "AU:" << m_loadError; }
	else
	{
		audioPorts().setAllChannelCounts(hostedTrackChannels(0, 2), 0, 2);
		if (AudioEngine* engine = Engine::audioEngine())
		{
			QString error;
			if (!m_unit.prepare(engine->outputSampleRate(), engine->framesPerPeriod(), 2, &error)) { qWarning() << "AU:" << error; }
			connect(engine, &AudioEngine::sampleRateChanged, this, [this] { reprepare(); });
		}
		reportLatency(m_unit.isPrepared() ? m_unit.latencyFrames() : 0);
	}
	m_params.build();

	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(100);
	connect(m_pollTimer, &QTimer::timeout, this, &AuInstrument::poll);
	m_pollTimer->start();

	// A single-streamed instrument renders through one InstrumentPlayHandle per period.
	if (AudioEngine* engine = Engine::audioEngine()) { engine->addPlayHandle(new InstrumentPlayHandle(this, parent)); }
}

AuInstrument::~AuInstrument()
{
	m_editor.close();
	if (AudioEngine* engine = Engine::audioEngine())
	{
		engine->removePlayHandlesOfTypes(instrumentTrack(),
			PlayHandle::Type::NotePlayHandle | PlayHandle::Type::InstrumentPlayHandle);
	}
}

bool AuInstrument::openNativeEditor(QString* error)
{
	return m_editor.open(gui::PluginNativeEditor::Hooks{
		[this](void* parent, QString* why) { return m_unit.openEditor(parent, why); },
		[this] { m_unit.closeEditor(); },
		[this](int* width, int* height) { m_unit.editorSize(width, height); }},
		displayName(), error, gui::PluginNativeEditor::Owner{nullptr, this});
}

void AuInstrument::closeNativeEditor() { m_editor.close(); }

bool AuInstrument::nativeEditorOpen() const { return m_editor.isOpen(); }

void AuInstrument::reprepare()
{
	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr || !m_unit.isLoaded()) { return; }
	engine->requestChangeInModel();
	QString error;
	if (!m_unit.prepare(engine->outputSampleRate(), engine->framesPerPeriod(), 2, &error)) { qWarning() << "AU:" << error; }
	engine->doneChangeInModel();
	m_needsReprepare.store(false, std::memory_order_relaxed);
	reportLatency(m_unit.isPrepared() ? m_unit.latencyFrames() : 0);
}

void AuInstrument::poll()
{
	if (!m_unit.isLoaded()) { return; }
	if (m_needsReprepare.load(std::memory_order_relaxed)) { reprepare(); }
	m_params.pullValues(1e-4f);
}

bool AuInstrument::handleMidiEvent(const MidiEvent& event, const TimePos&, f_cnt_t offset)
{
	// The audio thread for a clip's notes, the MIDI thread for live input: one bounded push, nothing more.
	const auto channel = static_cast<std::uint8_t>(std::clamp<int>(event.channel(), 0, 15));
	const auto frame = static_cast<int>(offset);
	switch (event.type())
	{
	case MidiNoteOn:
	case MidiNoteOff:
	case MidiKeyPressure:
		m_unit.queueMidi(static_cast<std::uint8_t>(event.type() | channel), static_cast<std::uint8_t>(event.key() & 0x7f),
			static_cast<std::uint8_t>(std::min<int>(event.velocity(), 127)), frame);
		break;
	case MidiControlChange:
		m_unit.queueMidi(static_cast<std::uint8_t>(MidiControlChange | channel),
			static_cast<std::uint8_t>(event.controllerNumber() & 0x7f), static_cast<std::uint8_t>(event.controllerValue() & 0x7f), frame);
		break;
	case MidiChannelPressure:
		m_unit.queueMidi(static_cast<std::uint8_t>(MidiChannelPressure | channel),
			static_cast<std::uint8_t>(event.channelPressure() & 0x7f), 0, frame);
		break;
	case MidiPitchBend:
		m_unit.queueMidi(static_cast<std::uint8_t>(MidiPitchBend | channel), static_cast<std::uint8_t>(event.pitchBend() & 0x7f),
			static_cast<std::uint8_t>((event.pitchBend() >> 7) & 0x7f), frame);
		break;
	case MidiProgramChange:
		m_unit.queueMidi(static_cast<std::uint8_t>(MidiProgramChange | channel), static_cast<std::uint8_t>(event.program() & 0x7f),
			0, frame);
		break;
	default:
		break;  // SysEx and system messages are not carried
	}
	return true;
}

auto AuInstrument::processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
	PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus
{
	if (!m_unit.isPrepared() || !m_unit.process(in.data(), out.data(), static_cast<int>(in.channels()),
			static_cast<int>(out.channels()), static_cast<int>(out.frames())))
	{
		if (!m_unit.isPrepared()) { m_needsReprepare.store(true, std::memory_order_relaxed); }
		for (ch_cnt_t c = 0; c < out.channels(); ++c) { std::fill_n(out.bufferPtr(c), out.frames(), 0.f); }
	}
	return lmms::ProcessStatus::Continue;
}

void AuInstrument::saveSettings(QDomDocument& doc, QDomElement& element)
{
	saveAuState(&m_unit, doc, element);
	m_params.saveValues(doc, element);
}

void AuInstrument::loadSettings(const QDomElement& element)
{
	loadAuState(&m_unit, element);
	m_params.pullValues(0.f);
	m_params.restoreValues(element);
}

auto AuInstrument::instantiateView(QWidget* parent) -> gui::PluginView*
{
	return new gui::AuInstrumentView(this, parent);
}

} // namespace lmms
