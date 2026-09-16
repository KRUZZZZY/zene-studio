/*
 * ClapInstrument.cpp - native CLAP instrument host
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

#include "ClapInstrument.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include <QDebug>
#include <QTimer>

#include "AudioEngine.h"
#include "ClapInstrumentView.h"
#include "ClapSubPluginFeatures.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "LmmsCommonMacros.h"
#include "MidiEvent.h"
#include "Song.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT clapinstrument_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"CLAP",
	QT_TRANSLATE_NOOP("PluginBrowser", "native CLAP instrument host"),
	"LMMS contributors",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	new ClapSubPluginFeatures(Plugin::Type::Instrument)
};

// The module entry point. PluginFactory requires this symbol in every plug-in
// library and calls it with the track and the sub-plugin Key; a key-less
// instantiation is what the descriptor browser does, and it must still produce
// an instrument (with nothing loaded and an empty parameter list).
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	try
	{
		return new ClapInstrument(
			static_cast<InstrumentTrack*>(parent),
			static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
	}
	catch (const std::exception& error)
	{
		// A throw must not cross the extern "C" boundary: the track then falls
		// back to a dummy instrument, which is visible rather than fatal.
		qCritical() << "CLAP instrument could not be created:" << error.what();
		return nullptr;
	}
}

}

ClapInstrument::ClapInstrument(InstrumentTrack* parent,
	const Descriptor::SubPluginFeatures::Key* key) :
	AudioPluginExt<Instrument, ClapInstrumentAudioPortsSettings>(
		&clapinstrument_plugin_descriptor, parent, key,
		Instrument::Flag::IsSingleStreamed | Instrument::Flag::IsMidiBased)
{
	const auto modulePath = key != nullptr ? key->attributes.value("file") : QString{};
	const auto pluginId = key != nullptr ? key->attributes.value("id") : QString{};

	QString error;
	if (!m_plugin.load(modulePath, pluginId, &error))
	{
		qWarning() << "CLAP: could not load" << modulePath << pluginId << ":" << error;
	}
	else if (!m_plugin.isInstrument())
	{
		// The instrument browser filters by type, so this only happens for a
		// hand-edited project or a plug-in that lies about its category. The
		// plug-in still loads (its parameters and state are real) but no note
		// reaches it, because it declares no note input port and the host
		// refuses the event rather than queueing it.
		qWarning() << "CLAP:" << modulePath << pluginId
			<< "is not an instrument; it will not receive notes";
	}
	else
	{
		const auto& layout = m_plugin.busLayout();
		// A CLAP instrument generates its audio: no track channels are routed
		// into it. The output side is the plug-in's own channel count, with a
		// floor of two so a mono generator still gets a stereo bus. This is
		// the AUDIO-OUTPUT CONFIGURATION half of feature row 79.
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

	// Every CLAP parameter becomes an LMMS model, exactly as for the effect
	// host, so automation and the .mmp both work without new plumbing.
	for (const auto& descriptor : m_plugin.parameters())
	{
		auto* model = new ClapParamModel(descriptor, this);
		m_paramModels.push_back(model);
		// host -> plug-in: mirror the model value into the lock free snapshot
		// the audio thread reads. Safe from any thread that changes a model.
		QObject::connect(model, &Model::dataChanged, model,
			[plugin = &m_plugin, id = descriptor.id, model] {
				plugin->setParamNormalized(id, model->value());
			});
		// plug-in -> host: render the value with the plug-in's own formatter
		model->setDisplayFormatter([plugin = &m_plugin, id = descriptor.id](double value) {
			return plugin->paramDisplayValue(id, value);
		});
	}

	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(100);
	connect(m_pollTimer, &QTimer::timeout, this, &ClapInstrument::poll);
	m_pollTimer->start();

	// An InstrumentPlayHandle is what gets play() called once per period for a
	// single-streamed instrument; NotePlayHandle emission happens inside
	// InstrumentPlayHandle::play before the instrument renders.
	if (auto* engine = Engine::audioEngine())
	{
		engine->addPlayHandle(new InstrumentPlayHandle(this, parent));
	}
}

ClapInstrument::~ClapInstrument()
{
	if (auto* engine = Engine::audioEngine())
	{
		engine->removePlayHandlesOfTypes(instrumentTrack(),
			PlayHandle::Type::NotePlayHandle | PlayHandle::Type::InstrumentPlayHandle);
	}
}

auto ClapInstrument::modelForParam(std::uint32_t id) -> ClapParamModel*
{
	for (auto* model : m_paramModels)
	{
		if (model->paramId() == id) { return model; }
	}
	return nullptr;
}

void ClapInstrument::reprepare()
{
	if (auto* engine = Engine::audioEngine())
	{
		QString error;
		if (!m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(),
				&error))
		{
			qWarning() << "CLAP: re-prepare failed:" << error;
		}
	}
	m_needsReprepare.store(false, std::memory_order_relaxed);
}

bool ClapInstrument::handleMidiEvent(const MidiEvent& event, const TimePos&,
	f_cnt_t offset)
{
	// Called from the track's MIDI path: the audio thread for a clip's notes,
	// the MIDI/GUI thread for live input. Nothing here allocates or locks.
	if (!m_plugin.acceptsNotes()) { return true; }

	const auto channel = static_cast<std::uint8_t>(std::clamp<int>(event.channel(), 0, 15));
	// LMMS' offset is in frames from the start of the current period, which is
	// exactly what clap_event_note's header.time means inside the block the
	// event is delivered with.
	const auto frameOffset = static_cast<std::int32_t>(offset);

	switch (event.type())
	{
		case MidiNoteOn:
			// A note-on with velocity 0 is a note-off in MIDI's own convention
			// (Midi.h: the status byte is the promise); sending it as a note-on
			// with velocity 0 would leave a voice hanging in a plug-in that
			// follows the CLAP contract.
			if (event.velocity() == 0)
			{
				m_plugin.setNoteOff(channel, static_cast<std::int16_t>(event.key()),
					frameOffset);
			}
			else
			{
				m_plugin.setNoteOn(channel, static_cast<std::int16_t>(event.key()),
					static_cast<double>(event.velocity()) / 127.0, frameOffset);
			}
			break;

		case MidiNoteOff:
			m_plugin.setNoteOff(channel, static_cast<std::int16_t>(event.key()),
				frameOffset);
			break;

		case MidiKeyPressure:
		default:
			// Not carried by this slice: key pressure, control change, pitch
			// bend, program change, channel pressure and SysEx.
			// clap.note-ports describes notes; a MIDI-dialect port would need
			// clap_event_midi, which this host does not build yet. Reported as
			// handled so the track does not warn on every message it does not
			// understand; the list is in docs/KNOWN-LIMITATIONS.md.
			return true;
	}

	return true;
}

auto ClapInstrument::processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
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
	// no wet/dry blend here. The host writes every frame of every channel the
	// caller supplies (ClapHost::process's over-run and tail rule).
	const auto frames = static_cast<int>(out.frames());
	m_plugin.process(in.data(), out.data(),
		static_cast<int>(in.channels()), static_cast<int>(out.channels()), frames);

	return lmms::ProcessStatus::Continue;
}

void ClapInstrument::saveSettings(QDomDocument& doc, QDomElement& element)
{
	QByteArray state;
	if (!m_plugin.saveState(&state)) { return; }

	auto stateElement = doc.createElement(QStringLiteral("state"));
	stateElement.appendChild(doc.createTextNode(QString::fromLatin1(state.toBase64())));
	element.appendChild(stateElement);
}

void ClapInstrument::loadSettings(const QDomElement& element)
{
	const auto node = element.firstChildElement(QStringLiteral("state"));
	if (node.isNull()) { return; }
	m_plugin.loadState(QByteArray::fromBase64(node.text().toLatin1()));

	// reflect the restored state in the models
	m_syncing = true;
	for (auto* model : m_paramModels)
	{
		model->setValue(m_plugin.paramNormalized(model->paramId()));
	}
	m_syncing = false;
}

void ClapInstrument::poll()
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
	}
	m_syncing = false;
}

auto ClapInstrument::instantiateView(QWidget* parent) -> gui::PluginView*
{
	return new gui::ClapInstrumentView(this, parent);
}

} // namespace lmms
