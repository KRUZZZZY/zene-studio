/*
 * Vst3Instrument.cpp - native VST3 instrument host
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

#include "Vst3Instrument.h"
#include "InstrumentTrack.h"
#include "MidiPort.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>

#include <QDebug>
#include <QTimer>

#include "AudioEngine.h"
#include "DeviceOptions.h"
#include "Engine.h"
#include "HostedTrackChannels.h"
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
		applyChannelCounts();
		// MX-114/MX-120: the output buses a route can send to a channel, and the note expressions.
		m_outputBuses.setLayout(vst3::outputBusesOf(m_plugin.outputBusDescriptors()));
		m_outputBuses.setEnableHook([this](int bus) { return enableOutputBus(bus); });
		m_plugin.refreshNoteExpressions();
		m_noteIds.store(m_plugin.hasNoteExpressions(), std::memory_order_relaxed);

		if (auto* engine = Engine::audioEngine())
		{
			m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(),
				&error);
		}
		cacheLatency();  // MX-157
		m_plugin.setLatencyChangedCallback([this] { m_latencyChanged.store(true, std::memory_order_relaxed); });
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

bool Vst3Instrument::openNativeEditor(QString* error)
{
	return m_editor.open(gui::PluginNativeEditor::Hooks{
		[this](void* parent, QString* why) { return m_plugin.openEditor(parent, why); },
		[this] { m_plugin.closeEditor(); },
		[this](int* width, int* height) { m_plugin.editorSize(width, height); }}, displayName(), error, gui::PluginNativeEditor::Owner{nullptr, this});
}

void Vst3Instrument::closeNativeEditor()
{
	m_editor.close();
}

bool Vst3Instrument::nativeEditorOpen() const
{
	return m_editor.isOpen();
}

Vst3Instrument::~Vst3Instrument()
{
	m_plugin.setLatencyChangedCallback(nullptr);  // MX-157: the flag it raises goes before the plug-in
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

void Vst3Instrument::applyChannelCounts()
{
	const auto& layout = m_plugin.busLayout();
	// A VST3 instrument generates its audio: no track channels are routed
	// into it. Any audio input bus the plug-in declares is fed silence by
	// the host, which is what AudioPlugin's single-bus instrument
	// transport can express.
	const auto mainInputs = static_cast<ch_cnt_t>(std::max(0, layout.inputs));
	const auto mainOutputs = std::max<ch_cnt_t>(ch_cnt_t{2},
		static_cast<ch_cnt_t>(std::max(0, layout.outputs)));
	audioPorts().setAllChannelCounts(hostedTrackChannels(mainInputs, mainOutputs), mainInputs, mainOutputs);
}

bool Vst3Instrument::enableOutputBus(int bus)
{
	auto* engine = Engine::audioEngine();
	if (engine == nullptr || !m_plugin.isInstrument()) { return false; }
	// GUI thread, with the render held off: a VST3 bus is activated on an inactive component, and the
	// transport's channel count follows the new layout before the next block.
	engine->requestChangeInModel();
	m_processGate.beginReconfigure();
	m_plugin.release();
	const bool activated = m_plugin.activateOutputBuses(bus);
	QString error;
	if (!m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(), &error))
	{
		qWarning() << "VST3: re-prepare after activating output bus" << bus << "failed:" << error;
	}
	applyChannelCounts();
	m_outputBuses.setLayout(vst3::outputBusesOf(m_plugin.outputBusDescriptors()));
	engine->doneChangeInModel();
	cacheLatency();  // MX-157
	m_processGate.endReconfigure();
	return activated;
}

void Vst3Instrument::cacheLatency()
{
	// The plug-in's unsigned report, bounded as the effect host bounds it (Vst3Effect::cacheLatency), so a
	// garbage value can never wrap into a negative delay; the PDC delay lines clamp what they cannot apply.
	const auto latency = std::min<std::uint32_t>(m_plugin.latency(), DeviceOptions::MaxLatencyOverride);
	reportLatency(m_plugin.isPrepared() ? static_cast<int>(latency) : 0);
}

auto Vst3Instrument::perNoteTarget(const AutomatableModel* parameter) const -> std::int64_t
{
	if (!m_plugin.hasNoteExpressions()) { return -1; }
	for (const auto* model : m_paramModels)
	{
		if (model == parameter) { return m_plugin.noteExpressionFor(model->paramId(), model->displayName()); }
	}
	return -1;
}

void Vst3Instrument::modulateNote(std::int64_t target, int channel, int key, float, float normalized)
{
	// Audio thread: one bounded push into the host's MIDI queue - a NoteExpressionValueEvent for the note.
	if (target < 0 || !m_plugin.receivesMidi()) { return; }
	vst3::MidiEventIn midi;
	midi.type = vst3::kNoteExpressionMidiType;
	midi.channel = static_cast<std::uint8_t>(std::clamp(channel, 0, 15));
	midi.data0 = static_cast<std::uint8_t>(std::clamp(key, 0, 127));
	midi.expressionType = static_cast<std::uint32_t>(target);
	midi.expressionValue = normalized;
	midi.withNoteId = true;
	m_plugin.pushMidiEvent(midi);
}

auto Vst3Instrument::mayPrepareAgain() const -> bool
{
	auto* engine = Engine::audioEngine();
	if (engine == nullptr) { return true; }
	const PrepareConfig current{engine->outputSampleRate(), engine->framesPerPeriod()};
	// A configuration the plug-in has not been asked for yet is tried at once:
	// that is the sample-rate change the re-prepare exists for.
	if (current != m_attemptedConfig) { return true; }
	return m_attemptCount < MaxPrepareAttempts;
}

void Vst3Instrument::reprepare()
{
	m_processGate.beginReconfigure();
	if (auto* engine = Engine::audioEngine())
	{
		const PrepareConfig config{engine->outputSampleRate(), engine->framesPerPeriod()};
		if (config != m_attemptedConfig)
		{
			// A new configuration: its retry budget starts here.
			m_attemptedConfig = config;
			m_attemptCount = 0;
		}
		++m_attemptCount;
		QString error;
		if (!m_plugin.prepare(config.sampleRate, static_cast<int>(config.framesPerPeriod), &error))
		{
			// A plug-in that refuses this configuration is not asked again and
			// again for it: the request stays pending, but the next attempt
			// waits for a different sample rate or block size
			// (mayPrepareAgain()), so the failure is not repeated on every
			// poll tick.
			qWarning() << "VST3: re-prepare failed:" << error;
		}
		else
		{
			// The configuration works again: the budget is refreshed.
			m_attemptCount = 0;
		}
	}
	m_needsReprepare.store(false, std::memory_order_relaxed);
	cacheLatency();  // MX-157
	m_processGate.endReconfigure();
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
	midi.withNoteId = m_noteIds.load(std::memory_order_relaxed);  // MX-120

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

		// E10: channel pressure and pitch bend are queued too; the host maps them (and CCs) to
		// the plug-in's parameters through its IMidiMapping - how MPE pressure and pitch arrive.
		case MidiChannelPressure:
			midi.type = static_cast<std::uint8_t>(MidiChannelPressure);
			midi.data0 = event.channelPressure();
			break;

		case MidiPitchBend:
			midi.type = static_cast<std::uint8_t>(MidiPitchBend);
			midi.data0 = static_cast<std::uint8_t>(event.pitchBend() & 0x7f);
			midi.data1 = static_cast<std::uint8_t>((event.pitchBend() >> 7) & 0x7f);
			break;

		default:
			// Not carried: SysEx and program change. Reported as handled so the track does not
			// warn on every message it does not understand (docs/VST3-INSTRUMENT-HOSTING.md).
			return true;
	}

	m_plugin.pushMidiEvent(midi);
	return true;
}

auto Vst3Instrument::processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
		PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus
{
	if (!m_processGate.tryEnterAudioProcess())
	{
		for (ch_cnt_t c = 0; c < out.channels(); ++c)
		{
			std::fill_n(out.bufferPtr(c), out.frames(), 0.f);
		}
		return lmms::ProcessStatus::Continue;
	}

	if (!m_plugin.isPrepared())
	{
		if (!out.empty())
		{
			for (ch_cnt_t channel = 0; channel < out.channels(); ++channel)
			{
				const auto samples = out.buffer(channel);
				std::fill(samples.begin(), samples.end(), 0.0f);
			}
		}
		// Either the plug-in failed to load or the sample rate changed. The
		// actual re-configuration happens on the GUI thread; this path only
		// records the request.
		for (ch_cnt_t channel = 0; channel < out.channels(); ++channel)
		{
			std::fill_n(out.bufferPtr(channel), out.frames(), 0.0f);
		}
		m_needsReprepare.store(true, std::memory_order_relaxed);
		m_processGate.leaveAudioProcess();
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
	forwardOutputEvents();
	// MX-114: the output buses past the main one, into the mixer channels they are routed to.
	m_outputBuses.deliver(out.data(), static_cast<int>(out.channels()), out.frames(),
		instrumentTrack() != nullptr && instrumentTrack()->isMuted());
	m_processGate.leaveAudioProcess();

	return lmms::ProcessStatus::Continue;
}

void Vst3Instrument::forwardOutputEvents()
{
	// Engine audit E7: the notes the plug-in generates (an arpeggiator, a sequencer) leave on the
	// track's MIDI OUTPUT port - straight to the port, not through InstrumentTrack::processOutEvent,
	// which would feed them back into the plug-in. Audio thread; MidiPort::processOutEvent is the
	// call a NotePlayHandle makes from here too.
	InstrumentTrack* track = instrumentTrack();
	if (track == nullptr || m_plugin.outputEventCount() == 0) { return; }
	for (int i = 0; i < m_plugin.outputEventCount(); ++i)
	{
		const vst3::MidiEventIn& e = m_plugin.outputEvent(i);
		// Its frame offset in the block travels as the event's time, as a NotePlayHandle's does
		// (review L01-1 #4: it was computed and then dropped, so a period's notes left at once).
		track->midiPort()->processOutEvent(MidiEvent(static_cast<MidiEventTypes>(e.type), e.channel, e.data0, e.data1),
			TimePos::fromFrames(static_cast<f_cnt_t>(std::max(0, e.frameOffset)), Engine::framesPerTick()));
	}
}

void Vst3Instrument::saveSettings(QDomDocument& doc, QDomElement& element)
{
	QByteArray componentState;
	QByteArray controllerState;
	const bool stateSaved = m_plugin.saveState(&componentState, &controllerState);
	if (!stateSaved)
	{
		qWarning() << "VST3: instrument state chunks could not be saved; writing parameter fallback";
	}

	auto appendState = [&doc, &element](const QString& name, const QByteArray& data) {
		if (data.isEmpty()) { return; }
		auto state = doc.createElement(name);
		state.appendChild(doc.createTextNode(QString::fromLatin1(data.toBase64())));
		element.appendChild(state);
	};
	appendState(QStringLiteral("componentstate"), componentState);
	appendState(QStringLiteral("controllerstate"), controllerState);

	auto parameters = doc.createElement(QStringLiteral("parameters"));
	for (const auto& descriptor : m_plugin.parameters())
	{
		if (descriptor.readOnly) { continue; }
		auto parameter = doc.createElement(QStringLiteral("parameter"));
		parameter.setAttribute(QStringLiteral("id"), QString::number(descriptor.id));
		parameter.setAttribute(QStringLiteral("value"),
			QString::number(m_plugin.paramNormalized(descriptor.id), 'g', 9));
		parameters.appendChild(parameter);
	}
	if (parameters.hasChildNodes()) { element.appendChild(parameters); }

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
	const bool stateRestored = m_plugin.loadState(componentState, controllerState);
	if (!stateRestored)
	{
		qWarning() << "VST3: instrument state chunks could not be restored; applying parameter fallback";
	}

	// The host's saved values of the writable parameters: all of them when the chunks could not be applied
	// (or there were none), and after a successful load each one the plug-in did not bring back (stress-4).
	restoreSavedParameters(m_plugin, element);

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

	if (needsReprepare() && mayPrepareAgain())
	{
		reprepare();
	}
	if (m_latencyChanged.exchange(false, std::memory_order_relaxed)) { cacheLatency(); }  // MX-157

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
