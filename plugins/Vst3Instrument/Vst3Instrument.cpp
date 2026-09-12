/*
 * Vst3Instrument.cpp - native VST3 instrument host
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

#include "Vst3Instrument.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include <QDebug>
#include <QTimer>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "LmmsCommonMacros.h"
#include "MidiEvent.h"
#include "Song.h"
#include "Vst3InstrumentView.h"
#include "Vst3SubPluginFeatures.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3instrument_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST3",
	QT_TRANSLATE_NOOP("PluginBrowser", "native VST3 instrument host"),
	"LMMS contributors",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	new Vst3SubPluginFeatures(Plugin::Type::Instrument)
};

// The module entry point. PluginFactory requires this symbol in every plug-in
// library (src/core/PluginFactory.cpp:177) and calls it with the track and the
// sub-plugin Key; `keyFromDnd` style instantiation (no key) is what the
// descriptor browser uses, and it must still produce an instrument.
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	try
	{
		return new Vst3Instrument(
			static_cast<InstrumentTrack*>(parent),
			static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
	}
	catch (const std::exception& error)
	{
		// A throw must not cross the extern "C" boundary: the track then falls
		// back to a dummy instrument, which is visible rather than fatal.
		qCritical() << "VST3 instrument could not be created:" << error.what();
		return nullptr;
	}
}

}

Vst3Instrument::Vst3Instrument(InstrumentTrack* parent,
	const Descriptor::SubPluginFeatures::Key* key) :
	AudioPluginExt<Instrument, Vst3InstrumentAudioPortsSettings>(
		&vst3instrument_plugin_descriptor, parent, key,
		Instrument::Flag::IsSingleStreamed | Instrument::Flag::IsMidiBased)
{
	const auto modulePath = key != nullptr ? key->attributes.value("file") : QString{};
	const auto classId = key != nullptr ? key->attributes.value("class") : QString{};

	QString error;
	if (!m_plugin.load(modulePath, classId, &error))
	{
		qWarning() << "VST3: could not load" << modulePath << classId << ":" << error;
	}
	else if (!m_plugin.isInstrument())
	{
		// The instrument browser filters by type, so this only happens for a
		// hand-edited project or a plug-in that lies about its category. The
		// plug-in still loads (its parameters and state are real) but no MIDI
		// reaches it, because it declares no instrument event input.
		qWarning() << "VST3:" << modulePath << classId
			<< "is not an instrument; it will not receive MIDI";
	}
	else
	{
		const auto& layout = m_plugin.busLayout();
		// A VST3 instrument generates its audio: no track channels are routed
		// into it. Any audio input bus the plug-in declares is fed silence by
		// the host, which is what AudioPlugin's single-bus instrument
		// transport can express.
		const auto mainInputs = static_cast<ch_cnt_t>(std::max(0, layout.inputs));
		const auto mainOutputs = std::max<ch_cnt_t>(ch_cnt_t{2},
			static_cast<ch_cnt_t>(std::max(0, layout.outputs)));
		audioPorts().setAllChannelCounts(std::max(mainInputs, mainOutputs),
			mainInputs, mainOutputs);

		if (auto* engine = Engine::audioEngine())
		{
			m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(),
				&error);
		}
	}

	// Every VST3 parameter becomes an LMMS model, exactly as for the effect
	// host, so automation and the .mmp both work without new plumbing.
	for (const auto& descriptor : m_plugin.parameters())
	{
		auto* model = new Vst3ParamModel(descriptor, this);
		m_paramModels.push_back(model);
		// host -> plug-in: mirror the model value into the lock free snapshot
		// the audio thread reads. Safe from any thread that changes a model.
		QObject::connect(model, &Model::dataChanged, model,
			[plugin = &m_plugin, id = descriptor.id, model] {
				plugin->setParamNormalized(id, model->value());
			});
		// plug-in -> host: render the value with the plug-in's own formatter
		model->setDisplayFormatter([plugin = &m_plugin, id = descriptor.id](float value) {
			return plugin->paramDisplayValue(id, value);
		});
	}

	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(100);
	connect(m_pollTimer, &QTimer::timeout, this, &Vst3Instrument::poll);
	m_pollTimer->start();

	// An InstrumentPlayHandle is what gets play() called once per period for a
	// single-streamed instrument; NotePlayHandle emission happens inside
	// InstrumentPlayHandle::play before the instrument renders.
	if (auto* engine = Engine::audioEngine())
	{
		engine->addPlayHandle(new InstrumentPlayHandle(this, parent));
	}
}

Vst3Instrument::~Vst3Instrument()
{
	if (auto* engine = Engine::audioEngine())
	{
		engine->removePlayHandlesOfTypes(instrumentTrack(),
			PlayHandle::Type::NotePlayHandle | PlayHandle::Type::InstrumentPlayHandle);
	}
}

auto Vst3Instrument::modelForParam(std::uint32_t id) -> Vst3ParamModel*
{
	for (auto* model : m_paramModels)
	{
		if (model->paramId() == id) { return model; }
	}
	return nullptr;
}

void Vst3Instrument::reprepare()
{
	if (auto* engine = Engine::audioEngine())
	{
		QString error;
		if (!m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(),
				&error))
		{
			qWarning() << "VST3: re-prepare failed:" << error;
		}
	}
	m_needsReprepare.store(false, std::memory_order_relaxed);
}

bool Vst3Instrument::handleMidiEvent(const MidiEvent& event, const TimePos&,
	f_cnt_t offset)
{
	// Called from the track's MIDI path: the audio thread for a clip's notes,
	// the MIDI/GUI thread for live input. Nothing here allocates or locks.
	if (!m_plugin.receivesMidi()) { return true; }

	vst3::MidiEventIn midi;
	midi.channel = static_cast<std::uint8_t>(
		std::clamp<int>(event.channel(), 0, 15));
	// LMMS' offset is in frames from the start of the current period
	// (NotePlayHandle.cpp:235-237, :396-400), which is exactly what VST3
	// calls Event::sampleOffset (SDK ivstevents.h:145).
	midi.frameOffset = static_cast<std::int32_t>(offset);

	switch (event.type())
	{
		case MidiNoteOn:
			midi.type = static_cast<std::uint8_t>(MidiNoteOn);
			midi.data0 = static_cast<std::uint8_t>(std::clamp<int>(event.key(), 0, 127));
			midi.data1 = static_cast<std::uint8_t>(
				std::clamp<int>(event.velocity(), 0, 127));
			break;

		case MidiNoteOff:
			midi.type = static_cast<std::uint8_t>(MidiNoteOff);
			midi.data0 = static_cast<std::uint8_t>(std::clamp<int>(event.key(), 0, 127));
			midi.data1 = 0;
			break;

		case MidiKeyPressure:
			midi.type = static_cast<std::uint8_t>(MidiKeyPressure);
			midi.data0 = static_cast<std::uint8_t>(std::clamp<int>(event.key(), 0, 127));
			midi.data1 = static_cast<std::uint8_t>(
				std::clamp<int>(event.velocity(), 0, 127));
			break;

		case MidiControlChange:
			midi.type = static_cast<std::uint8_t>(MidiControlChange);
			midi.data0 = event.controllerNumber();
			midi.data1 = event.controllerValue();
			break;

		default:
			// Not carried by this slice: SysEx, program change, channel
			// pressure and pitch bend. Reported as handled so the track does
			// not warn on every message it does not understand. The list is
			// in docs/VST3-INSTRUMENT-HOSTING.md.
			return true;
	}

	m_plugin.pushMidiEvent(midi);
	return true;
}

auto Vst3Instrument::processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
	PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus
{
	if (!m_plugin.isPrepared())
	{
		// Either the plug-in failed to load or the sample rate changed. The
		// actual re-configuration happens on the GUI thread; this path only
		// records the request.
		m_needsReprepare.store(true, std::memory_order_relaxed);
		return lmms::ProcessStatus::Continue;
	}

	if (auto* song = Engine::getSong())
	{
		m_plugin.setTempo(song->getTempo());
		m_plugin.setTransportPlaying(song->isPlaying());
	}

	// An instrument with no audio input has nothing to mix with a dry signal,
	// so the plug-in's output is the output - unlike the effect host, there is
	// no wet/dry blend here.
	const auto frames = static_cast<int>(out.frames());
	m_plugin.process(in.data(), out.data(),
		static_cast<int>(in.channels()), static_cast<int>(out.channels()), frames);

	return lmms::ProcessStatus::Continue;
}

void Vst3Instrument::saveSettings(QDomDocument& doc, QDomElement& element)
{
	QByteArray componentState;
	QByteArray controllerState;
	m_plugin.saveState(&componentState, &controllerState);

	auto appendState = [&doc, &element](const QString& name, const QByteArray& data) {
		if (data.isEmpty()) { return; }
		auto state = doc.createElement(name);
		state.appendChild(doc.createTextNode(QString::fromLatin1(data.toBase64())));
		element.appendChild(state);
	};
	appendState(QStringLiteral("componentstate"), componentState);
	appendState(QStringLiteral("controllerstate"), controllerState);
}

void Vst3Instrument::loadSettings(const QDomElement& element)
{
	auto readState = [&element](const QString& name) -> QByteArray {
		const auto node = element.firstChildElement(name);
		if (node.isNull()) { return {}; }
		return QByteArray::fromBase64(node.text().toLatin1());
	};

	const auto componentState = readState(QStringLiteral("componentstate"));
	const auto controllerState = readState(QStringLiteral("controllerstate"));
	m_plugin.loadState(componentState, controllerState);

	// reflect the restored state in the models
	m_syncing = true;
	for (auto* model : m_paramModels)
	{
		model->setValue(m_plugin.paramNormalized(model->paramId()));
	}
	m_syncing = false;
}

void Vst3Instrument::poll()
{
	if (!m_plugin.isLoaded()) { return; }

	if (needsReprepare())
	{
		reprepare();
	}

	if (m_syncing) { return; }
	m_syncing = true;
	for (auto* model : m_paramModels)
	{
		const auto id = model->paramId();
		const auto value = m_plugin.paramNormalized(id);
		if (std::abs(model->value() - value) > 1e-4f)
		{
			model->setValue(value);
		}
		// keep the edit controller's GUI-side mirror in sync (GUI thread only)
		m_plugin.notifyController(id, model->value());
	}
	m_syncing = false;
}

auto Vst3Instrument::instantiateView(QWidget* parent) -> gui::PluginView*
{
	return new gui::Vst3InstrumentView(this, parent);
}

} // namespace lmms
