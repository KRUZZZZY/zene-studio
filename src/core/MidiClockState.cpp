/*
 * MidiClockState.cpp - the OBSERVABLE half of the MIDI clock: the one result a
 *                      caller reads the engine's clock state from.
 *
 * Split out of MidiClock.cpp because that file's generator + follower half had
 * grown past this fork's 500-line file ratchet (Gate 7), and this is the seam
 * the file already had: everything here READS state, nothing here is on a
 * timing path, and none of it runs on the audio thread.
 *
 * `clock.get_state` answers this object verbatim, and the unit test reads it
 * back through the same function, so a test cannot accidentally assert on a
 * different view of the clock than the control surface serves.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "MidiClock.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>

#include "Engine.h"
#include "Song.h"

namespace lmms
{

namespace
{

//! One counter per clock-family message, built from a counting callable so the
//! emission side and the receive side cannot disagree about which names exist.
template <typename Counter>
QJsonObject messageCountsJson(Counter counter)
{
	QJsonObject out;
	for (MidiClockMessage message : {MidiClockMessage::Clock, MidiClockMessage::Start,
			MidiClockMessage::Continue, MidiClockMessage::Stop, MidiClockMessage::SongPosition,
			MidiClockMessage::TimeCode})
	{
		out.insert(midiClockMessageName(message), static_cast<int>(counter(message)));
	}
	return out;
}

} // namespace

QJsonObject MidiClock::stateJson() const
{
	QJsonObject master;
	master.insert(QStringLiteral("enabled"), masterEnabled());
	master.insert(QStringLiteral("port"), masterPort());
	master.insert(QStringLiteral("port_ready"), masterPortReady());
	master.insert(QStringLiteral("period_ms"), periodMs());
	master.insert(QStringLiteral("emitted_total"), static_cast<int>(emittedTotal()));
	master.insert(QStringLiteral("emitted"),
		messageCountsJson([this](MidiClockMessage m) { return emittedCount(m); }));
	master.insert(QStringLiteral("last_emitted"), midiClockMessageName(emittedAt(0)));
	master.insert(QStringLiteral("last_emitted_ticks"), emittedTickAt(0));
	const quint32 history = std::min<quint32>(emittedTotal(), MonitorCapacity);
	QJsonArray monitor;
	for (quint32 i = 0; i < history; ++i)
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("message"), midiClockMessageName(emittedAt(static_cast<int>(i))));
		entry.insert(QStringLiteral("ticks"), emittedTickAt(static_cast<int>(i)));
		monitor.append(entry);
	}
	master.insert(QStringLiteral("monitor"), monitor);

	const MidiClockTracker& tracker = m_tracker;
	QJsonObject slave;
	slave.insert(QStringLiteral("enabled"), slaveEnabled());
	slave.insert(QStringLiteral("source_port"), slaveSourcePort());
	slave.insert(QStringLiteral("follow_tempo"), slaveFollowTempo());
	slave.insert(QStringLiteral("locked"), tracker.locked());
	slave.insert(QStringLiteral("tempo_bpm"), tracker.tempoBpm());
	slave.insert(QStringLiteral("drift_ms"), tracker.driftMs());
	slave.insert(QStringLiteral("drift_bound_ms"), tracker.driftBoundMs());
	slave.insert(QStringLiteral("tempo_error_bound_bpm"), tracker.tempoErrorBoundBpm());
	slave.insert(QStringLiteral("window_ms"), tracker.windowMs());
	slave.insert(QStringLiteral("pulses"), tracker.pulseCount());
	slave.insert(QStringLiteral("interval_ms"),
		static_cast<double>(tracker.pulseIntervalNs()) / 1.0e6);
	slave.insert(QStringLiteral("song_position"), tracker.lastSongPosition());
	slave.insert(QStringLiteral("time_code"), tracker.lastTimeCode());
	slave.insert(QStringLiteral("messages"),
		messageCountsJson([&tracker](MidiClockMessage m) { return tracker.messageCount(m); }));

	QJsonObject out;
	out.insert(QStringLiteral("master"), master);
	out.insert(QStringLiteral("slave"), slave);
	out.insert(QStringLiteral("ticks_per_pulse"), TicksPerMidiClockPulse);
	out.insert(QStringLiteral("ticks_per_beat"), TicksPerMidiBeat);
	out.insert(QStringLiteral("pulses_per_quarter"), MidiClockPulsesPerQuarter);
	out.insert(QStringLiteral("lock_timeout_ms"), static_cast<int>(MidiClockTracker::LockTimeoutMs));
	// MTC is declared, not guessed: see this file's header comment. A reader
	// asking for a timecode master gets an answer that says there is none
	// rather than an empty object it has to interpret.
	out.insert(QStringLiteral("mtc"), QStringLiteral("absent"));
	Song* song = Engine::getSong();
	out.insert(QStringLiteral("tempo"), song != nullptr ? song->getTempo() : 0);
	out.insert(QStringLiteral("last_pulse_age_ms"), lastPulseAgeMs());
	return out;
}

qint64 MidiClock::lastPulseAgeMs() const
{
	const quint64 last = m_tracker.lastPulseNs();
	if (last == 0) { return -1; }
	const quint64 now = nowNs();
	return now > last ? static_cast<qint64>((now - last) / 1000000) : 0;
}

} // namespace lmms
} // namespace lmms
