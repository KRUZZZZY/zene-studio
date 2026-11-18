/*
 * SfzPlayer.cpp - the built-in SFZ instrument: loads .sfz files and plays them with sfizz
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "SfzPlayer.h"

#include <algorithm>
#include <cmath>

#include <QDomElement>
#include <QFileInfo>
#include <initializer_list>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "PathUtil.h"
#include "SfzPlayerView.h"
#include "Song.h"

#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT sfzplayer_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"SFZ Player",
	QT_TRANSLATE_NOOP("PluginBrowser",
		"Player for SFZ instruments (sfizz): samples with velocity layers, round robins and key switches"),
	"Zene Studio; SFZ engine: sfizz (BSD-2-Clause)",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	"sfz",
	nullptr,
};

}

namespace
{

constexpr int PitchBendCenter = 8192;

std::uint8_t clampData(int value) noexcept
{
	return static_cast<std::uint8_t>(std::clamp(value, 0, 127));
}

//! The SFZ engine's form of a MIDI event; false for the kinds an SFZ never reacts to.
bool translate(const MidiEvent& event, SfzEvent& out) noexcept
{
	switch (event.type())
	{
		case MidiNoteOn:
			out.kind = event.velocity() == 0 ? SfzEvent::Kind::NoteOff : SfzEvent::Kind::NoteOn;
			out.number = clampData(event.key());
			out.value = clampData(event.velocity());
			return true;
		case MidiNoteOff:
			out.kind = SfzEvent::Kind::NoteOff;
			out.number = clampData(event.key());
			out.value = clampData(event.velocity());
			return true;
		case MidiControlChange:
			out.kind = SfzEvent::Kind::Controller;
			out.number = clampData(event.controllerNumber());
			out.value = clampData(event.controllerValue());
			return true;
		case MidiPitchBend:
			out.kind = SfzEvent::Kind::PitchBend;
			out.bend = static_cast<std::int16_t>(std::clamp(event.pitchBend() - PitchBendCenter, -PitchBendCenter, PitchBendCenter - 1));
			return true;
		case MidiChannelPressure:
			out.kind = SfzEvent::Kind::ChannelPressure;
			out.value = clampData(event.channelPressure());
			return true;
		case MidiKeyPressure:
			out.kind = SfzEvent::Kind::KeyPressure;
			out.number = clampData(event.key());
			out.value = clampData(event.velocity());
			return true;
		case MidiProgramChange:
			out.kind = SfzEvent::Kind::ProgramChange;
			out.number = clampData(event.program());
			return true;
		default:
			return false;
	}
}

} // namespace


SfzPlayer::SfzPlayer(InstrumentTrack* track)
	: Instrument(track, &sfzplayer_plugin_descriptor, nullptr, Flag::IsSingleStreamed | Flag::IsMidiBased)
	, m_volumeModel(0.f, -60.f, 12.f, 0.1f, this, tr("Volume"))
	, m_qualityModel(2, 1, 10, this, tr("Sample quality"))
	, m_polyphonyModel(64, 1, 256, this, tr("Polyphony"))
	, m_oversamplingModel(this, tr("Oversampling"))
{
	for (const char* factor : {"1x", "2x", "4x", "8x"}) { m_oversamplingModel.addItem(QString::fromLatin1(factor)); }
	m_oversamplingModel.setValue(0);

	// Polyphony and oversampling are fixed when a synth is built, so a change builds the loaded file again. An automation
	// tick changes a model on the render thread, where nothing may emit a signal: all four models hold their change
	// signal back there (AutomatableModel::setDeferChangeSignalOnRenderThread) and a timer on this object's own thread
	// emits it, which is what reaches scheduleReload. A change from the socket or the window emits at once.
	for (AutomatableModel* model : std::initializer_list<AutomatableModel*>{&m_volumeModel, &m_qualityModel, &m_polyphonyModel, &m_oversamplingModel})
	{
		model->setDeferChangeSignalOnRenderThread();
	}
	connect(&m_polyphonyModel, &Model::dataChanged, this, &SfzPlayer::scheduleReload);
	connect(&m_oversamplingModel, &Model::dataChanged, this, &SfzPlayer::scheduleReload);
	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, &SfzPlayer::scheduleReload);
	connect(&m_deferredSignalTimer, &QTimer::timeout, this, &SfzPlayer::flushDeferredSignals);
	m_deferredSignalTimer.start(DeferredSignalPollMs);
	// An export loads every sample whole before it starts and goes back to streaming when it is done.
	if (Song* song = Engine::getSong())
	{
		connect(song, &Song::exportPreparing, this, &SfzPlayer::exportPreparing, Qt::DirectConnection);
		connect(song, &Song::exportFinished, this, &SfzPlayer::exportFinished);
	}
}

quint32 SfzPlayer::preloadFrames() const
{
	const auto guard = m_synth.acquire();
	return guard ? guard->preloadFrames() : 0;
}

void SfzPlayer::flushDeferredSignals()
{
	for (AutomatableModel* model : std::initializer_list<AutomatableModel*>{&m_volumeModel, &m_qualityModel, &m_polyphonyModel, &m_oversamplingModel})
	{
		model->flushDeferredChange();
	}
}

void SfzPlayer::exportPreparing()
{
	m_exportPreload = true;
	if (!m_file.isEmpty()) { installSynth(m_file); }
}

void SfzPlayer::exportFinished()
{
	m_exportPreload = false;
	if (!m_file.isEmpty()) { installSynth(m_file); }
}

SfzPlayer::~SfzPlayer() = default;

QString SfzPlayer::nodeName() const
{
	return sfzplayer_plugin_descriptor.name;
}

gui::PluginView* SfzPlayer::instantiateView(QWidget* parent)
{
	return new gui::SfzPlayerView(this, parent);
}

SfzSynth::Settings SfzPlayer::currentSettings() const
{
	SfzSynth::Settings settings;
	settings.sampleRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	settings.polyphony = m_polyphonyModel.value();
	settings.oversampling = 1 << std::clamp(m_oversamplingModel.value(), 0, 3);
	settings.quality = m_qualityModel.value();
	if (m_exportPreload) { settings.preloadFrames = SfzSynth::WholeSamples; }
	return settings;
}

bool SfzPlayer::installSynth(const QString& absolutePath)
{
	auto synth = std::make_unique<SfzSynth>(currentSettings());
	const bool loaded = QFileInfo::exists(absolutePath) && synth->load(absolutePath.toUtf8().toStdString());
	if (!loaded)
	{
		// What was loaded before is no longer what the file name says: go silent rather than play the old one.
		m_synth.replace(nullptr);
		m_regions.store(0, std::memory_order_relaxed);
		m_status = QFileInfo::exists(absolutePath)
			? tr("Not a playable SFZ (no regions): %1").arg(QFileInfo(absolutePath).fileName())
			: tr("File not found: %1").arg(absolutePath);
		return false;
	}

	const int regions = synth->regionCount();
	m_regions.store(regions, std::memory_order_relaxed);
	m_status = tr("%n region(s)", nullptr, regions);
	m_synth.replace(std::move(synth));
	return true;
}

void SfzPlayer::renameTrackAfter(const QString& file)
{
	instrumentTrack()->setName(PathUtil::cleanName(file));
}

bool SfzPlayer::openFile(const QString& file, bool renameTrack)
{
	// Remembered even when it cannot be loaded, so a project saved meanwhile keeps the reference.
	m_file = PathUtil::toAbsolute(file);
	const bool loaded = installSynth(m_file);
	if (loaded && renameTrack) { renameTrackAfter(m_file); }
	emit fileChanged();
	return loaded;
}

void SfzPlayer::reportOpenFailure(const QString& message)
{
	m_status = message;
	emit fileChanged();
}

void SfzPlayer::loadFile(const QString& file)
{
	if (!openFile(file)) { Engine::getSong()->collectError(m_status); }
}

void SfzPlayer::scheduleReload()
{
	if (m_loadingSettings || m_file.isEmpty() || m_reloadQueued.exchange(true)) { return; }
	QMetaObject::invokeMethod(this, [this] {
		m_reloadQueued.store(false);
		if (m_file.isEmpty()) { return; }
		installSynth(m_file);
		emit fileChanged();
	}, Qt::QueuedConnection);
}

void SfzPlayer::saveSettings(QDomDocument& doc, QDomElement& element)
{
	if (!m_file.isEmpty()) { element.setAttribute(QStringLiteral("src"), PathUtil::toShortestRelative(m_file)); }
	m_volumeModel.saveSettings(doc, element, "volume");
	m_qualityModel.saveSettings(doc, element, "quality");
	m_polyphonyModel.saveSettings(doc, element, "polyphony");
	m_oversamplingModel.saveSettings(doc, element, "oversampling");
}

void SfzPlayer::loadSettings(const QDomElement& element)
{
	// The synth is built once, below, with these values - not once for each of them.
	m_loadingSettings = true;
	m_volumeModel.loadSettings(element, "volume");
	m_qualityModel.loadSettings(element, "quality");
	m_polyphonyModel.loadSettings(element, "polyphony");
	m_oversamplingModel.loadSettings(element, "oversampling");

	m_loadingSettings = false;

	const QString src = element.attribute(QStringLiteral("src"));
	if (src.isEmpty()) { return; }
	if (!openFile(src, false)) { Engine::getSong()->collectError(m_status); }
}

bool SfzPlayer::handleMidiEvent(const MidiEvent& event, const TimePos&, f_cnt_t offset)
{
	// Any thread: the render thread for a clip's notes, the MIDI or GUI thread for live input. The queue is
	// lock-free; a full one drops the event rather than wait.
	SfzEvent sfzEvent;
	sfzEvent.offset = static_cast<std::uint32_t>(offset);
	if (translate(event, sfzEvent)) { m_events.push(sfzEvent); }
	return true;
}

std::size_t SfzPlayer::drainEvents() noexcept
{
	// Insertion into a fixed array, ordered by frame offset and stable for equal offsets: no allocation.
	std::size_t count = 0;
	SfzEvent event;
	while (count < m_batch.size() && m_events.pop(&event))
	{
		std::size_t at = count++;
		for (; at > 0 && m_batch[at - 1].offset > event.offset; --at) { m_batch[at] = m_batch[at - 1]; }
		m_batch[at] = event;
	}
	return count;
}

void SfzPlayer::configureSynth(SfzSynth& synth) noexcept
{
	const bool fresh = synth.id() != m_configuredSynth;
	if (fresh)
	{
		m_configuredSynth = synth.id();
		m_appliedVolume = std::nanf("");
		m_appliedQuality = 0;
	}

	const float volume = m_volumeModel.value();
	if (fresh || volume != m_appliedVolume)
	{
		synth.setVolume(volume);
		m_appliedVolume = volume;
	}

	const int quality = m_qualityModel.value();
	if (quality != m_appliedQuality)
	{
		synth.setSampleQuality(quality);
		m_appliedQuality = quality;
	}
}

void SfzPlayer::renderPeriod(SfzSynth& synth, std::span<SampleFrame> out, std::size_t eventCount) noexcept
{
	const std::size_t total = out.size();
	std::size_t done = 0;
	std::size_t next = 0;
	while (done < total)
	{
		const std::size_t chunk = std::min(SfzSynth::ChunkFrames, total - done);
		const std::size_t end = done + chunk;
		// The last chunk also takes events at or past the period's end, so none is lost.
		while (next < eventCount && (m_batch[next].offset < end || end == total))
		{
			const std::size_t offset = m_batch[next].offset;
			synth.apply(m_batch[next], static_cast<int>(std::min(offset > done ? offset - done : 0, chunk - 1)));
			++next;
		}
		synth.render(out.data() + done, chunk);
		done = end;
	}
}

void SfzPlayer::playImpl(std::span<SampleFrame> out)
{
	// Drained every period, loaded or not, so the queue cannot fill while no file is loaded.
	const std::size_t events = drainEvents();
	const auto guard = m_synth.acquire();
	SfzSynth* const synth = guard.get();
	if (synth == nullptr)
	{
		zeroSampleFrames(out.data(), out.size());
		return;
	}
	configureSynth(*synth);
	renderPeriod(*synth, out, events);
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* model, void*)
{
	return new SfzPlayer(static_cast<InstrumentTrack*>(model));
}

}

} // namespace lmms
