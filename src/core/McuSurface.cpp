/*
 * McuSurface.cpp - a Mackie Control surface driving the song's tracks and transport
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

#include "McuSurface.h"

#include <algorithm>

#include <QJsonArray>
#include <QMetaObject>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClient.h"
#include "MidiPort.h"
#include "Song.h"
#include "Track.h"
#include "TrackMixModels.h"

namespace lmms
{

namespace
{

constexpr int FeedbackMs = 40;
constexpr float PanStep = 2.f;
std::unique_ptr<McuSurface> s_surface;

//! The tracks a strip can drive: those with a volume of their own, in song order.
QList<Track*> bankable()
{
	QList<Track*> out;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (trackVolumeModel(track) != nullptr) { out.append(track); }
	}
	return out;
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

McuSurface::McuSurface()
{
	m_sentFader.fill(-1);
	QObject::connect(&m_feedback, &QTimer::timeout, this, &McuSurface::sendFeedback);
}

McuSurface::~McuSurface() = default;

McuSurface* McuSurface::instance()
{
	return s_surface.get();
}

McuSurface* McuSurface::connect(const QString& input, const QString& output)
{
	auto surface = std::make_unique<McuSurface>();
	surface->m_input = input;
	surface->m_output = output;
	surface->m_port = std::make_unique<MidiPort>(QStringLiteral("Mackie Control"), Engine::audioEngine()->midiClient(),
		surface.get(), nullptr, MidiPort::Mode::Duplex);
	if (!input.isEmpty()) { surface->m_port->subscribeReadablePort(input); }
	if (!output.isEmpty()) { surface->m_port->subscribeWritablePort(output); }
	surface->m_feedback.start(FeedbackMs);
	s_surface = std::move(surface);
	return s_surface.get();
}

void McuSurface::disconnect()
{
	s_surface.reset();
}

void McuSurface::setBank(int first)
{
	m_bank = std::clamp(first, 0, std::max(0, static_cast<int>(bankable().size()) - 1));
	m_sentFader.fill(-1);  // every strip shows a different track: send them all again
	m_sentLeds.clear();
}

Track* McuSurface::stripTrack(int strip) const
{
	const QList<Track*> tracks = bankable();
	const int index = m_bank + strip;
	return strip >= 0 && strip < mcu::Strips && index < tracks.size() ? tracks.at(index) : nullptr;
}

void McuSurface::processInEvent(const MidiEvent& event, const TimePos&, f_cnt_t)
{
	// MIDI client thread: decode here, apply on the UI thread where the registry and the models live.
	const mcu::Input input = mcu::decode(event);
	if (input.kind == mcu::Kind::None) { return; }
	m_eventsIn.fetch_add(1, std::memory_order_relaxed);
	QMetaObject::invokeMethod(this, [this, input] { apply(input); }, Qt::QueuedConnection);
}

void McuSurface::processOutEvent(const MidiEvent&, const TimePos&, f_cnt_t)
{
	// The surface's port only sends what sendFeedback() hands it directly.
}

void McuSurface::apply(const mcu::Input& input)
{
	if (applyMix(input) || applyBank(input.kind)) { return; }
	if (Track* track = stripTrack(input.strip)) { applyStripButton(input, track); }
	else { applyTransport(input.kind); }
}

bool McuSurface::applyMix(const mcu::Input& input)
{
	Track* track = stripTrack(input.strip);
	switch (input.kind)
	{
	case mcu::Kind::Fader:
		if (FloatModel* volume = trackVolumeModel(track))
		{
			volume->setValue(volume->minValue() + mcu::faderLevel(input.value) * (volume->maxValue() - volume->minValue()));
		}
		else if (input.strip == mcu::MasterStrip)
		{
			IntModel& master = Engine::getSong()->masterVolumeModel();
			master.setValue(master.minValue() + mcu::faderLevel(input.value) * (master.maxValue() - master.minValue()));
		}
		return true;
	case mcu::Kind::FaderTouch: m_touched[static_cast<std::size_t>(input.strip)] = input.pressed; return true;
	case mcu::Kind::VPot:
		if (FloatModel* pan = trackPanningModel(track)) { pan->setValue(pan->value() + input.value * PanStep); }
		return true;
	default: return false;
	}
}

bool McuSurface::applyBank(mcu::Kind kind)
{
	switch (kind)
	{
	case mcu::Kind::BankLeft: setBank(m_bank - mcu::Strips); return true;
	case mcu::Kind::BankRight: setBank(m_bank + mcu::Strips); return true;
	case mcu::Kind::ChannelLeft: setBank(m_bank - 1); return true;
	case mcu::Kind::ChannelRight: setBank(m_bank + 1); return true;
	default: return false;
	}
}

void McuSurface::applyStripButton(const mcu::Input& input, Track* track)
{
	const QString id = control::trackIdOf(track);
	switch (input.kind)
	{
	case mcu::Kind::Mute: run(QStringLiteral("track.set_mute"), {{QStringLiteral("track"), id}, {QStringLiteral("muted"), !track->isMuted()}}); break;
	case mcu::Kind::Solo: run(QStringLiteral("track.set_solo"), {{QStringLiteral("track"), id}, {QStringLiteral("solo"), !track->isSolo()}}); break;
	case mcu::Kind::Select: run(QStringLiteral("window.select"), {{QStringLiteral("track"), id}}); break;
	case mcu::Kind::Rec:
	{
		const bool arm = !m_armed.contains(id);
		if (run(QStringLiteral("track.set_arm"), {{QStringLiteral("track"), id}, {QStringLiteral("armed"), arm}}).ok)
		{
			if (arm) { m_armed.insert(id); } else { m_armed.remove(id); }
		}
		break;
	}
	default: break;
	}
}

void McuSurface::applyTransport(mcu::Kind kind)
{
	Song* song = Engine::getSong();
	switch (kind)
	{
	case mcu::Kind::Play: run(QStringLiteral("transport.play")); break;
	case mcu::Kind::Stop: run(QStringLiteral("transport.stop")); break;
	case mcu::Kind::Record: run(QStringLiteral("transport.record")); break;
	case mcu::Kind::Rewind:
	case mcu::Kind::Forward:
	{
		const int bar = song->ticksPerBar();
		const int now = song->getPlayPos().getTicks();
		run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), std::max(0, now + (kind == mcu::Kind::Forward ? bar : -bar))}});
		break;
	}
	default: break;
	}
}

void McuSurface::send(const MidiEvent& event)
{
	m_port->processOutEvent(event);
	++m_eventsOut;
}

void McuSurface::sendLed(mcu::Kind kind, int strip, bool lit)
{
	const MidiEvent event = mcu::ledEvent(kind, strip, lit);
	if (m_sentLeds.contains(event.key()) && m_sentLeds.value(event.key()) == lit) { return; }
	m_sentLeds.insert(event.key(), lit);
	send(event);
}

void McuSurface::sendStrip(int strip)
{
	Track* track = stripTrack(strip);
	FloatModel* volume = trackVolumeModel(track);
	const int position = volume == nullptr ? 0
		: mcu::faderPosition((volume->value() - volume->minValue()) / std::max(1e-6f, volume->maxValue() - volume->minValue()));
	auto& sent = m_sentFader[static_cast<std::size_t>(strip)];
	if (!m_touched[static_cast<std::size_t>(strip)] && position != sent)
	{
		sent = position;
		send(mcu::faderEvent(strip, position));
	}
	sendLed(mcu::Kind::Mute, strip, track != nullptr && track->isMuted());
	sendLed(mcu::Kind::Solo, strip, track != nullptr && track->isSolo());
	sendLed(mcu::Kind::Rec, strip, track != nullptr && m_armed.contains(control::trackIdOf(track)));
}

void McuSurface::sendFeedback()
{
	for (int strip = 0; strip < mcu::Strips; ++strip) { sendStrip(strip); }
	const Song* song = Engine::getSong();
	sendLed(mcu::Kind::Play, -1, song->isPlaying());
	sendLed(mcu::Kind::Record, -1, song->isPlaying() && song->isRecording());
}

QJsonObject McuSurface::state() const
{
	QJsonArray strips;
	for (int strip = 0; strip < mcu::Strips; ++strip)
	{
		strips.append(control::trackIdOf(stripTrack(strip)));
	}
	return {{QStringLiteral("connected"), true}, {QStringLiteral("input"), m_input}, {QStringLiteral("output"), m_output},
		{QStringLiteral("bank"), m_bank}, {QStringLiteral("strips"), strips},
		{QStringLiteral("events_in"), m_eventsIn.load(std::memory_order_relaxed)}, {QStringLiteral("events_out"), m_eventsOut}};
}

} // namespace lmms
