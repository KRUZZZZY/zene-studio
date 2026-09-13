/*
 * MidiClock.cpp - the DAW as a MIDI clock master and as a MIDI clock slave:
 *                 the object the `clock.*` command group drives.
 *
 * WHAT THIS FILE IS, AND WHERE IT SITS. The engine already had the vocabulary
 * (include/Midi.h declares MidiSync 0xF8, MidiStart 0xFA, MidiContinue 0xFB,
 * MidiStop 0xFC, MidiSongPosition 0xF2, MidiTimeCode 0xF1) and one MIDI output
 * path (MidiClient::processOutEvent, reached through a MidiPort). It had no
 * generator, no follower and no reader: MidiClientRaw::parseData discarded
 * every byte >= 0xF8 that was not a system reset, and both processOutEvent
 * switches warned "unhandled" for the clock family. This is the generator, the
 * follower, and the two switches widened to carry the messages.
 *
 * DEPENDENCIES ON UPSTREAM-FORKED FILES, stated because they are the contract:
 *   * src/core/Song.cpp - processNextBuffer() calls processAudioPeriod() before
 *     its own transport gate (the Session View precedent: STOP is an edge, and
 *     a stopped transport is when it must be sent).
 *   * src/core/midi/MidiClient.cpp - parseData() routes the clock family to
 *     handleInputMessage() instead of dropping it, and processOutEvent() sends
 *     the one- and three-byte forms through sendByte().
 *   * src/core/midi/MidiAlsaSeq.cpp - processOutEvent() sends the same family
 *     as ALSA-sequencer events, DIRECT (not on the tick queue the note path
 *     uses), because a clock must not be scheduled behind a queue.
 *
 * MTC, stated rather than implied: this file sends and receives the MIDI CLOCK
 * family (0xF8/0xFA/0xFB/0xFC/0xF2) and counts MTC quarter-frames (0xF1) on the
 * input side. It does NOT generate MTC: a full-frame MTC master needs a frame
 * rate, a drop-frame flag and a SMPTE start offset, and this engine's time
 * model is ticks-per-bar with no frame rate and no start offset to convert
 * from. Inventing that configuration is how a claim gets shipped instead of a
 * feature, so the absence is declared in docs/KNOWN-LIMITATIONS.md and the
 * `clock.get_state` result reports it as `mtc: "absent"`.
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
#include <chrono>
#include <cmath>

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiClient.h"
#include "MidiEvent.h"
#include "MidiEventProcessor.h"
#include "MidiPort.h"
#include "Song.h"

namespace lmms
{

namespace
{

/*! The MidiEventProcessor a MidiPort insists on being given. The clock's port is
 *  OUTPUT-only, so nothing should ever arrive on it; an incoming event is
 *  dropped rather than routed anywhere, which is the honest behaviour for a port
 *  that exists to send. */
class ClockPortProcessor : public MidiEventProcessor
{
public:
	void processInEvent(const MidiEvent&, const TimePos&, f_cnt_t) override {}
	void processOutEvent(const MidiEvent&, const TimePos&, f_cnt_t) override {}
};

//! How often the follower offers the measured tempo to the Song, in ms. A tempo
//! write is project state (it goes on the GUI's model, the undo display and the
//! project file), so it is a control-thread act and not an audio-period one.
const int FollowPollMs = 250;

//! The name of the clock's own MIDI port, as a user sees it in a port menu.
const char* const ClockPortName = "zene-clock";

} // namespace

MidiClock* MidiClock::instance()
{
	// Deliberately never destroyed: this object is reached from the audio
	// thread, the MIDI reader thread and the control thread, and a singleton
	// torn down at static-destruction time can be reached by one of them after
	// its client is gone. The one owned MidiPort survives its client by design
	// (MidiClient::~MidiClient calls MidiPort::invalidateCilent() on every port
	// it knows, which replaces the client with the process-wide dummy), so a
	// late emission is a no-op instead of a dangling call.
	static MidiClock* s_instance = new MidiClock();
	return s_instance;
}

MidiClock::MidiClock() = default;

MidiClock::~MidiClock() = default;

quint64 MidiClock::nowNs() noexcept
{
	using Clock = std::chrono::steady_clock;
	return static_cast<quint64>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}

quint32 MidiClock::songPositionOf(qint64 ticks) noexcept
{
	// One MIDI BEAT - the unit of a Song Position Pointer - is six clock pulses,
	// i.e. a sixteenth note, which is TicksPerMidiBeat of this engine's ticks.
	// A position before the start is 0, and the wire form is 14-bit.
	if (ticks <= 0) { return 0; }
	return static_cast<quint32>(ticks / TicksPerMidiBeat) & 0x3FFF;
}

MidiClient* MidiClock::client() const
{
	AudioEngine* audio = Engine::audioEngine();
	return audio != nullptr ? audio->midiClient() : nullptr;
}

MidiPort* MidiClock::outputPort()
{
	if (MidiPort* existing = m_outputPort.load()) { return existing; }
	MidiClient* midi = client();
	if (midi == nullptr) { return nullptr; }
	// MidiPort's constructor is what registers the port on the client and
	// applies its mode - the same call InstrumentTrack's own port makes when a
	// track is added from the GUI thread, so this is the existing output path
	// and not a second one.
	auto* processor = new ClockPortProcessor();
	auto* port = new MidiPort(QString::fromLatin1(ClockPortName), midi, processor, nullptr,
		MidiPort::Mode::Output);
	m_client.store(midi);
	m_processor.store(processor);
	m_outputPort.store(port);
	return port;
}

bool MidiClock::setMasterEnabled(bool enabled, const QString& port, QString* error)
{
	if (!enabled)
	{
		m_masterEnabled.store(false);
		return true;
	}
	MidiClient* midi = client();
	if (midi == nullptr)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("this instance has no MIDI client, so there is nothing to "
				"send a clock through: the engine's MIDI client is the dummy one (see "
				"midi.device_list for the client that is running).");
		}
		return false;
	}
	MidiPort* output = outputPort();
	if (output == nullptr)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("the engine's MIDI client could not take an output port, so "
				"the clock has no port to send on.");
		}
		return false;
	}
	if (!port.isEmpty())
	{
		output->subscribeWritablePort(port);
		m_masterPort = port;
	}
	m_client.store(midi);
	m_masterEnabled.store(true);
	return true;
}

QString MidiClock::masterPort() const
{
	return m_masterPort;
}

void MidiClock::restoreMaster(bool enabled, const QString& port)
{
	MidiPort* output = m_outputPort.load();
	if (output != nullptr && port.isEmpty() && !m_masterPort.isEmpty())
	{
		output->subscribeWritablePort(m_masterPort, false);
		m_masterPort.clear();
	}
	QString ignored;
	setMasterEnabled(enabled, port, &ignored);
}

bool MidiClock::masterPortReady() const
{
	MidiPort* port = m_outputPort.load();
	if (port == nullptr || !port->isOutputEnabled()) { return false; }
	// A raw client (ALSA Raw-MIDI, OSS, the dummy) writes to its one device and
	// has no destination ports to subscribe to, so an enabled port IS a ready
	// one there. A sequencer client is only ready once a destination is
	// subscribed, and saying otherwise would claim a clock nobody receives.
	if (MidiClient* midi = m_client.load())
	{
		if (midi->isRaw()) { return true; }
	}
	const MidiPort::Map& writable = port->writablePorts();
	for (auto it = writable.begin(); it != writable.end(); ++it)
	{
		if (it.value()) { return true; }
	}
	return false;
}

double MidiClock::periodMs() const
{
	AudioEngine* audio = Engine::audioEngine();
	if (audio == nullptr) { return 0.0; }
	const sample_rate_t rate = audio->baseSampleRate();
	if (rate <= 0) { return 0.0; }
	return 1000.0 * static_cast<double>(audio->framesPerPeriod()) / static_cast<double>(rate);
}

void MidiClock::setSlaveEnabled(bool enabled) noexcept
{
	m_slaveEnabled.store(enabled);
	if (!enabled)
	{
		m_slaveFollowTempo.store(false);
		// Leaving slave mode forgets the measurement: a caller that re-enables
		// following must not be handed a tempo measured from a clock it stopped
		// listening to (the same reason the lock drops when pulses stop).
		m_tracker.reset();
	}
}

void MidiClock::setSlaveFollowTempo(bool follow) noexcept
{
	m_slaveFollowTempo.store(follow);
}

QString MidiClock::slaveSourcePort() const
{
	return m_slavePort;
}

void MidiClock::setSlaveSourcePort(const QString& port)
{
	m_slavePort = port;
}

void MidiClock::setSlaveDriftBoundMs(double ms) noexcept
{
	m_tracker.setDriftBoundMs(ms);
}

void MidiClock::startFollowTimer()
{
	if (m_followTimer != nullptr) { return; }
	m_followTimer = new QTimer();
	// A functor connection with the timer itself as the context: no receiver
	// object is needed, and the timer lives on the thread that creates it here
	// (the control thread - `clock.*` handlers always run on the UI thread).
	QObject::connect(m_followTimer, &QTimer::timeout, [this]() { applyMeasuredTempo(); });
	m_followTimer->start(FollowPollMs);
}

void MidiClock::stopFollowTimer()
{
	if (m_followTimer == nullptr) { return; }
	m_followTimer->stop();
}

int MidiClock::applyMeasuredTempo()
{
	if (!m_slaveEnabled.load() || !m_slaveFollowTempo.load()) { return 0; }
	if (!m_tracker.locked()) { return 0; }
	Song* song = Engine::getSong();
	if (song == nullptr) { return 0; }
	const double measured = m_tracker.tempoBpm();
	if (measured <= 0.0) { return 0; }
	const double current = static_cast<double>(song->getTempo());
	const double clamped = std::max(static_cast<double>(MinTempo),
		std::min(static_cast<double>(MaxTempo), measured));
	if (std::fabs(clamped - current) < FollowDeadbandBpm) { return 0; }
	const int bpm = static_cast<int>(std::lround(clamped));
	song->setTempo(bpm);
	return bpm;
}

void MidiClock::record(MidiClockMessage message, qint64 positionTicks) noexcept
{
	const std::size_t index = static_cast<std::size_t>(message);
	if (index < m_emittedCounts.size()) { m_emittedCounts[index].fetch_add(1); }
	m_emittedTotal.fetch_add(1);
	const quint32 slot = m_monitorWrite.fetch_add(1) % static_cast<quint32>(MonitorCapacity);
	m_monitor[slot].message.store(static_cast<quint32>(message));
	m_monitor[slot].ticks.store(positionTicks);
}

void MidiClock::emitMessage(MidiClockMessage message, qint64 positionTicks, quint32 payload) noexcept
{
	record(message, positionTicks);
	MidiClient* midi = m_client.load();
	MidiPort* port = m_outputPort.load();
	if (midi == nullptr || port == nullptr) { return; }

	const MidiEventTypes type = midiClockEventType(message);
	MidiEvent event;
	switch (message)
	{
		// The two messages with a payload: a Song Position Pointer carries the
		// position as two 7-bit bytes (LSB first), and a quarter-frame carries
		// one 7-bit nibble.
		case MidiClockMessage::SongPosition:
			event = MidiEvent(type, 0, static_cast<int16_t>(payload & 0x7F),
				static_cast<int16_t>((payload >> 7) & 0x7F));
			break;
		case MidiClockMessage::TimeCode:
			event = MidiEvent(type, 0, static_cast<int16_t>(payload & 0x7F));
			break;
		default:
			event = MidiEvent(type);
			break;
	}
	midi->processOutEvent(event, TimePos(), port);
}

void MidiClock::trackTransport(bool transportRunning, qint64 playPosTicks, qint64 expectedAdvance) noexcept
{
	const bool wasRunning = m_transportWasRunning.load();
	const bool known = m_positionKnown.load();
	const qint64 previous = m_lastPlayPosTicks.load();
	m_transportWasRunning.store(transportRunning);
	m_positionKnown.store(true);
	m_lastPlayPosTicks.store(playPosTicks);
	if (transportRunning == wasRunning && !known) { return; }
	if (transportRunning && !wasRunning)
	{
		// The rising edge: a START begins at the top of the song and a CONTINUE
		// resumes where the pointer says. The pointer is sent FIRST, and only
		// when the transport is not at the top, so a master never claims a
		// position the slave would not have arrived at anyway.
		m_tickRemainder.store(0.0);
		if (playPosTicks != 0)
		{
			emitMessage(MidiClockMessage::SongPosition, playPosTicks, songPositionOf(playPosTicks));
			emitMessage(MidiClockMessage::Continue, playPosTicks);
			return;
		}
		emitMessage(MidiClockMessage::Start, playPosTicks);
		return;
	}
	if (!transportRunning && wasRunning)
	{
		m_tickRemainder.store(0.0);
		emitMessage(MidiClockMessage::Stop, playPosTicks);
		return;
	}
	// Running on both periods: a position that did not move by the amount the
	// period advances is a SEEK, and a slave cannot know about it. The engine's
	// own advance is not exactly `expectedAdvance` every period (it carries a
	// fractional tick), so one tick either side is the same advance and only a
	// bigger difference is reported. A sub-pulse seek is not reportable at all:
	// an SPP has twelve-tick resolution.
	const qint64 delta = playPosTicks - previous;
	if (delta == 0) { return; }
	if (delta >= expectedAdvance - 1 && delta <= expectedAdvance + 1) { return; }
	emitMessage(MidiClockMessage::SongPosition, playPosTicks, songPositionOf(playPosTicks));
}

int MidiClock::pulsesForPeriod(double carryTicks, double advancedTicks,
	double* carryAfter) noexcept
{
	const double pulseTicks = static_cast<double>(TicksPerMidiClockPulse);
	double remainder = carryTicks + advancedTicks;
	int pulses = 0;
	// Bounded in practice: one audio period advances a few ticks, so this cannot
	// spin - and nothing here allocates or locks.
	while (remainder >= pulseTicks)
	{
		remainder -= pulseTicks;
		++pulses;
	}
	if (carryAfter != nullptr) { *carryAfter = remainder; }
	return pulses;
}

void MidiClock::emitPulses(int frames) noexcept
{
	const float framesPerTick = Engine::framesPerTick();
	if (framesPerTick <= 0.0f || frames <= 0) { return; }
	const double advance = static_cast<double>(frames) / static_cast<double>(framesPerTick);
	double carry = 0.0;
	const int pulses = pulsesForPeriod(m_tickRemainder.load(), advance, &carry);
	for (int i = 0; i < pulses; ++i)
	{
		emitMessage(MidiClockMessage::Clock, m_lastPlayPosTicks.load());
	}
	m_tickRemainder.store(carry);
}

void MidiClock::processAudioPeriod(int frames, bool transportRunning, qint64 playPosTicks,
	quint64 timestampNs) noexcept
{
	// The tracker ages on every period, whether or not the master is on, so a
	// clock that simply STOPS arriving loses its lock and a caller never reads a
	// tempo from a stream that has ended.
	m_tracker.update(timestampNs);
	if (!m_masterEnabled.load()) { return; }
	const float framesPerTick = Engine::framesPerTick();
	const qint64 expected = framesPerTick > 0.0f
		? static_cast<qint64>(static_cast<double>(frames) / static_cast<double>(framesPerTick))
		: 0;
	trackTransport(transportRunning, playPosTicks, expected);
	if (transportRunning) { emitPulses(frames); }
}

void MidiClock::handleInputMessage(MidiClockMessage message, quint32 value,
	quint64 timestampNs) noexcept
{
	m_tracker.received(message, value, timestampNs);
}

quint32 MidiClock::emittedCount(MidiClockMessage message) const noexcept
{
	const std::size_t index = static_cast<std::size_t>(message);
	if (index >= m_emittedCounts.size()) { return 0; }
	return m_emittedCounts[index].load();
}

MidiClockMessage MidiClock::emittedAt(int indexFromLatest) const noexcept
{
	const quint32 total = m_emittedTotal.load();
	if (indexFromLatest < 0) { return MidiClockMessage::None; }
	const quint32 index = static_cast<quint32>(indexFromLatest);
	if (index >= static_cast<quint32>(MonitorCapacity) || index >= total)
	{
		return MidiClockMessage::None;
	}
	const quint32 slot = (m_monitorWrite.load() - 1 - index) % static_cast<quint32>(MonitorCapacity);
	return static_cast<MidiClockMessage>(m_monitor[slot].message.load());
}

qint64 MidiClock::emittedTickAt(int indexFromLatest) const noexcept
{
	const quint32 total = m_emittedTotal.load();
	if (indexFromLatest < 0) { return 0; }
	const quint32 index = static_cast<quint32>(indexFromLatest);
	if (index >= static_cast<quint32>(MonitorCapacity) || index >= total) { return 0; }
	const quint32 slot = (m_monitorWrite.load() - 1 - index) % static_cast<quint32>(MonitorCapacity);
	return m_monitor[slot].ticks.load();
}

} // namespace lmms
