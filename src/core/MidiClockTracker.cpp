/*
 * MidiClockTracker.cpp - the MIDI clock vocabulary and the timing core of a
 *                        clock SLAVE: timestamped pulses in, a measured tempo,
 *                        a lock state and a drift measurement out.
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

#include <cmath>

namespace lmms
{

namespace
{

//! The index a message's counter lives at. Clock messages are counted too: a
//! pulse count is the one number that says a slave is being clocked at all.
int messageIndex(MidiClockMessage message)
{
	return static_cast<int>(message);
}

//! BPM of a pulse interval, from the grid: MidiClockPulsesPerQuarter pulses
//! make a quarter note, so 60 / (24 * intervalSeconds).
double bpmOfIntervalNs(double intervalNs)
{
	if (intervalNs <= 0.0) { return 0.0; }
	return 60.0 * 1.0e9 / (static_cast<double>(MidiClockPulsesPerQuarter) * intervalNs);
}

} // namespace

QString midiClockMessageName(MidiClockMessage message)
{
	switch (message)
	{
		case MidiClockMessage::Clock: { return QStringLiteral("clock"); }
		case MidiClockMessage::Start: { return QStringLiteral("start"); }
		case MidiClockMessage::Continue: { return QStringLiteral("continue"); }
		case MidiClockMessage::Stop: { return QStringLiteral("stop"); }
		case MidiClockMessage::SongPosition: { return QStringLiteral("song_position"); }
		case MidiClockMessage::TimeCode: { return QStringLiteral("time_code"); }
		case MidiClockMessage::None: { break; }
	}
	return QString();
}

MidiClockMessage midiClockMessageFromName(const QString& name)
{
	static const MidiClockMessage kAll[] = {MidiClockMessage::Clock, MidiClockMessage::Start,
		MidiClockMessage::Continue, MidiClockMessage::Stop, MidiClockMessage::SongPosition,
		MidiClockMessage::TimeCode};
	for (MidiClockMessage message : kAll)
	{
		if (midiClockMessageName(message) == name) { return message; }
	}
	return MidiClockMessage::None;
}

MidiEventTypes midiClockEventType(MidiClockMessage message)
{
	switch (message)
	{
		case MidiClockMessage::Clock: { return MidiSync; }
		case MidiClockMessage::Start: { return MidiStart; }
		case MidiClockMessage::Continue: { return MidiContinue; }
		case MidiClockMessage::Stop: { return MidiStop; }
		case MidiClockMessage::SongPosition: { return MidiSongPosition; }
		case MidiClockMessage::TimeCode: { return MidiTimeCode; }
		case MidiClockMessage::None: { break; }
	}
	return MidiActiveSensing;
}

MidiClockMessage midiClockMessageOfByte(quint8 status)
{
	switch (status)
	{
		case MidiSync: { return MidiClockMessage::Clock; }
		case MidiStart: { return MidiClockMessage::Start; }
		case MidiContinue: { return MidiClockMessage::Continue; }
		case MidiStop: { return MidiClockMessage::Stop; }
		case MidiSongPosition: { return MidiClockMessage::SongPosition; }
		case MidiTimeCode: { return MidiClockMessage::TimeCode; }
		default: { break; }
	}
	return MidiClockMessage::None;
}

void MidiClockTracker::reset() noexcept
{
	m_windowStartNs.store(0);
	m_lastPulseNs.store(0);
	m_intervalNs.store(0);
	m_meanIntervalNs.store(0.0);
	m_windowSpanMs.store(0.0);
	m_pulseCount.store(0);
	m_windowPulses.store(0);
	m_locked.store(false);
}

void MidiClockTracker::accumulate(quint64 timestampNs) noexcept
{
	const quint64 previous = m_lastPulseNs.load();
	m_lastPulseNs.store(timestampNs);
	m_intervalNs.store(previous == 0 ? 0 : timestampNs - previous);
	m_windowPulses.fetch_add(1);
}

void MidiClockTracker::measure(quint64 timestampNs) noexcept
{
	// The window is the span between the pulse that OPENED it and this one, and
	// it holds exactly `pulses` intervals - so the mean interval is the span
	// divided by the count, never by count - 1.
	const quint64 span = timestampNs - m_windowStartNs.load();
	const int pulses = m_windowPulses.load();
	const double mean = static_cast<double>(span) / static_cast<double>(pulses);
	m_meanIntervalNs.store(mean);
	m_windowSpanMs.store(static_cast<double>(span) / 1.0e6);
	m_windowStartNs.store(timestampNs);
	m_windowPulses.store(0);

	const double strayNs = std::fabs(static_cast<double>(m_intervalNs.load()) - mean);
	const bool within = strayNs <= m_driftBoundMs.load() * 1.0e6;
	m_locked.store(within);
}

void MidiClockTracker::pulse(quint64 timestampNs) noexcept
{
	if (m_pulseCount.load() == 0 || m_windowStartNs.load() == 0)
	{
		m_windowStartNs.store(timestampNs);
		m_lastPulseNs.store(timestampNs);
		m_pulseCount.store(1);
		m_windowPulses.store(0);
		// The first pulse cannot measure anything: one point is not a rate.
		m_locked.store(false);
		return;
	}
	accumulate(timestampNs);
	m_pulseCount.fetch_add(1);
	if (m_windowPulses.load() >= WindowPulses) { measure(timestampNs); }
}

void MidiClockTracker::received(MidiClockMessage message, quint32 value,
	quint64 timestampNs) noexcept
{
	if (message == MidiClockMessage::Clock)
	{
		pulse(timestampNs);
		return;
	}
	m_messageCounts[static_cast<std::size_t>(messageIndex(message))].fetch_add(1);
	switch (message)
	{
		// START and CONTINUE say nothing about the rate: a slave's tempo comes
		// from the pulses and only from the pulses, so neither touches the lock.
		case MidiClockMessage::Start:
		case MidiClockMessage::Continue: { return; }
		// STOP means the pulse stream is about to end. The lock is dropped, so
		// a caller never reads a tempo from a clock that has stopped.
		case MidiClockMessage::Stop: { unlock(); return; }
		case MidiClockMessage::SongPosition: { m_songPosition.store(static_cast<int>(value)); return; }
		case MidiClockMessage::TimeCode: { m_timeCode.store(static_cast<int>(value)); return; }
		case MidiClockMessage::Clock:
		case MidiClockMessage::None: { return; }
	}
}

void MidiClockTracker::unlock() noexcept
{
	m_locked.store(false);
	m_windowStartNs.store(0);
	m_windowPulses.store(0);
}

void MidiClockTracker::update(quint64 timestampNs) noexcept
{
	if (!m_locked.load()) { return; }
	const quint64 last = m_lastPulseNs.load();
	const quint64 ageMs = timestampNs > last ? (timestampNs - last) / 1000000 : 0;
	if (ageMs > LockTimeoutMs) { unlock(); }
}

double MidiClockTracker::tempoBpm() const noexcept
{
	if (!m_locked.load()) { return 0.0; }
	return bpmOfIntervalNs(m_meanIntervalNs.load());
}

double MidiClockTracker::driftMs() const noexcept
{
	const double mean = m_meanIntervalNs.load();
	if (mean <= 0.0) { return 0.0; }
	return (static_cast<double>(m_intervalNs.load()) - mean) / 1.0e6;
}

double MidiClockTracker::windowMs() const noexcept
{
	return m_windowSpanMs.load();
}

double MidiClockTracker::tempoErrorBoundBpm() const noexcept
{
	// The derivation, not a guessed constant: jitter on the two timestamps that
	// bound the window moves the span by at most 2 * jitter, so the measured
	// tempo moves by at most tempo * (2 * jitter) / window for a window whose
	// own span is `window`.
	const double tempo = tempoBpm();
	const double window = windowMs();
	if (tempo <= 0.0 || window <= 0.0) { return 0.0; }
	return tempo * (2.0 * PulseJitterBoundMs) / window;
}

void MidiClockTracker::setDriftBoundMs(double ms) noexcept
{
	m_driftBoundMs.store(ms < 0.0 ? 0.0 : (ms > MaxDriftBoundMs ? MaxDriftBoundMs : ms));
}

quint32 MidiClockTracker::messageCount(MidiClockMessage message) const noexcept
{
	if (message == MidiClockMessage::None) { return 0; }
	return m_messageCounts[static_cast<std::size_t>(messageIndex(message))].load();
}

} // namespace lmms
