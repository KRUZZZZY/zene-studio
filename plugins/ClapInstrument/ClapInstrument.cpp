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
#include <cstdint>
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

namespace
{

struct ChannelCounts
{
	ch_cnt_t total;
	ch_cnt_t inputs;
	ch_cnt_t outputs;
};

//! The transport's channel counts for a CLAP instrument's @a layout. A CLAP instrument generates
//! its audio: no track channels are routed into it. The output side is the plug-in's own channel
//! count, with a floor of two so a mono generator still gets a stereo bus. This is the
//! AUDIO-OUTPUT CONFIGURATION half of feature row 79.
ChannelCounts channelCountsOf(const clap::PortLayout& layout)
{
	const auto inputs = static_cast<ch_cnt_t>(std::max(0, layout.inputs));
	const auto outputs = std::max<ch_cnt_t>(ch_cnt_t{2}, static_cast<ch_cnt_t>(std::max(0, layout.outputs)));
	return {std::max(inputs, outputs), inputs, outputs};
}

} // namespace

ClapInstrument::ClapInstrument(InstrumentTrack* parent,
	const Descriptor::SubPluginFeatures::Key* key) :
	AudioPluginExt<Instrument, ClapInstrumentAudioPortsSettings>(
		&clapinstrument_plugin_descriptor, parent, key,
		Instrument::Flag::IsSingleStreamed | Instrument::Flag::IsMidiBased),
	m_params(&m_plugin, this)
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
		const auto counts = channelCountsOf(m_plugin.busLayout());
		audioPorts().setAllChannelCounts(counts.total, counts.inputs, counts.outputs);
		m_outputBuses.setLayout(clap::outputBusesOf(m_plugin.ports()));  // MX-114

		if (auto* engine = Engine::audioEngine())
		{
			m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(),
				&error);
		}
		cacheLatency();  // MX-157
	}

	// Every CLAP parameter becomes an LMMS model, exactly as for the effect
	// host (the same list, ClapParamModelList), so automation and the .mmp both
	// work without new plumbing. W6: in plain values, as the model's own range
	// is - the normalised value this used to write and read was right only for
	// a parameter whose range is 0..1.
	m_params.sync();

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


bool ClapInstrument::openNativeEditor(QString* error)
{
	return m_editor.open(gui::PluginNativeEditor::Hooks{
		[this](void* parent, QString* why) { return m_plugin.openEditor(parent, why); },
		[this] { m_plugin.closeEditor(); },
		[this](int* width, int* height) { m_plugin.editorSize(width, height); }}, displayName(), error, gui::PluginNativeEditor::Owner{nullptr, this});
}

void ClapInstrument::closeNativeEditor()
{
	m_editor.close();
}

bool ClapInstrument::nativeEditorOpen() const
{
	return m_editor.isOpen();
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
	return m_params.modelFor(id);
}

void ClapInstrument::reprepare()
{
	if (auto* engine = Engine::audioEngine())
	{
		// Held off the audio thread: the plug-in can ask for this while it is processing
		// (review L01-1 #3).
		engine->requestChangeInModel();
		QString error;
		const auto revision = m_plugin.layoutRevision();
		if (!m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(),
				&error))
		{
			qWarning() << "CLAP: re-prepare failed:" << error;
		}
		// prepare() re-read a changed port layout: the transport follows it, as the effect host's does.
		bool layoutChanged = false;
		if (m_plugin.layoutRevision() != revision)
		{
			const auto counts = channelCountsOf(m_plugin.busLayout());
			audioPorts().setAllChannelCounts(counts.total, counts.inputs, counts.outputs);
			m_outputBuses.setLayout(clap::outputBusesOf(m_plugin.ports()));  // MX-114
			layoutChanged = true;
		}
		engine->doneChangeInModel();
		// BUG 6: the audio-output configuration on the surface follows the new layout instead of going stale.
		if (layoutChanged) { emit outputLayoutChanged(); }
	}
	m_needsReprepare.store(false, std::memory_order_relaxed);
	cacheLatency();  // MX-157
}

void ClapInstrument::cacheLatency()
{
	// clap_plugin_latency reports a uint32_t: past INT32_MAX it would turn negative as an int (the effect
	// host's bound, ClapEffect::cacheLatency). The PDC delay lines clamp what they cannot apply.
	const auto latency = std::min<std::uint32_t>(m_plugin.latency(), static_cast<std::uint32_t>(INT32_MAX));
	reportLatency(m_plugin.isPrepared() ? static_cast<int>(latency) : 0);
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

		// E11: MPE's per-note axes as CLAP note expressions - pressure from channel or key
		// pressure, timbre (CC74) as brightness; key -1 is every note on that channel, which is
		// what an MPE member channel means.
		case MidiChannelPressure:
			m_plugin.setNoteExpression(channel, -1, CLAP_NOTE_EXPRESSION_PRESSURE, event.channelPressure() / 127.0, frameOffset);
			break;

		case MidiKeyPressure:
			m_plugin.setNoteExpression(channel, static_cast<std::int16_t>(event.key()), CLAP_NOTE_EXPRESSION_PRESSURE,
				event.velocity() / 127.0, frameOffset);
			break;

		case MidiControlChange:
			if (event.controllerNumber() == 74)
			{
				m_plugin.setNoteExpression(channel, -1, CLAP_NOTE_EXPRESSION_BRIGHTNESS, event.controllerValue() / 127.0, frameOffset);
			}
			break;

		default:
			// Not carried: pitch bend, program change, SysEx (docs/KNOWN-LIMITATIONS.md). Reported
			// as handled so the track does not warn on every message.
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
	// MX-114: the output ports past the main one, into the mixer channels they are routed to.
	m_outputBuses.deliver(out.data(), static_cast<int>(out.channels()), out.frames(),
		instrumentTrack() != nullptr && instrumentTrack()->isMuted());

	return lmms::ProcessStatus::Continue;
}

auto ClapInstrument::perNoteTarget(const AutomatableModel* parameter) const -> std::int64_t
{
	for (const ClapParamModel* model : m_params.models())
	{
		if (model != parameter) { continue; }
		const int index = m_plugin.paramIndex(model->paramId());
		const auto& params = m_plugin.parameters();
		const bool perNote = index >= 0 && static_cast<std::size_t>(index) < params.size()
			&& params[static_cast<std::size_t>(index)].modulatablePerNote;
		return perNote ? static_cast<std::int64_t>(model->paramId()) : -1;
	}
	return -1;
}

void ClapInstrument::modulateNote(std::int64_t target, int channel, int key, float offset, float)
{
	// Audio thread: one bounded push into the host's note queue, delivered with the next block.
	if (target < 0 || !m_plugin.acceptsNotes()) { return; }
	m_plugin.setNoteParamMod(static_cast<std::uint8_t>(std::clamp(channel, 0, 15)),
		static_cast<std::int16_t>(std::clamp(key, 0, 127)), static_cast<std::uint32_t>(target), offset);
}

void ClapInstrument::saveSettings(QDomDocument& doc, QDomElement& element)
{
	QByteArray state;
	if (m_plugin.saveState(&state))
	{
		auto stateElement = doc.createElement(QStringLiteral("state"));
		stateElement.appendChild(doc.createTextNode(QString::fromLatin1(state.toBase64())));
		element.appendChild(stateElement);
	}
	m_params.saveValues(doc, element);
}

void ClapInstrument::loadSettings(const QDomElement& element)
{
	const auto node = element.firstChildElement(QStringLiteral("state"));
	if (!node.isNull()) { m_plugin.loadState(QByteArray::fromBase64(node.text().toLatin1())); }
	// stress-4: the host's saved values of what the plug-in's state did not bring back (none in a document
	// written before they were saved).
	const bool restored = m_params.restoreValues(element);
	if (node.isNull() && !restored) { return; }

	// reflect the restored state in the models
	m_params.pullValues(0.f);
}

void ClapInstrument::poll()
{
	if (!m_plugin.isLoaded()) { return; }

	if (m_plugin.serviceMainThread() && Engine::getSong() != nullptr) { Engine::getSong()->setModified(); }
	if (needsReprepare() || m_plugin.needsReprepare())
	{
		m_plugin.clearNeedsReprepare();
		reprepare();
	}

	if (m_params.sync()) { emit parametersChanged(); }
	m_params.pullValues(1e-4f);
}

auto ClapInstrument::instantiateView(QWidget* parent) -> gui::PluginView*
{
	return new gui::ClapInstrumentView(this, parent);
}

} // namespace lmms
