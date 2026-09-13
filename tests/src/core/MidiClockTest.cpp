/*
 * MidiClockTest.cpp - the ENGINE half of MIDI clock (0.3.0, the `clock.*`
 *                     group): the generator's message set, and the follower's
 *                     measurement of a clock it was never told the rate of.
 *
 * This is the half of the proof that does not need hardware, and it is the
 * reason the socket transcript beside it (tests/control-clock-commands.py,
 * ctest ControlClockCommands) can be honest about what it does NOT cover: an
 * incoming clock cannot be injected into a running instance from outside, so
 * the rate arithmetic is proved HERE, with synthetic timestamps, on the same
 * tracker the MIDI reader thread feeds.
 *
 * The four claims:
 *   1. a pulse stream at a known rate produces that tempo, from the grid alone;
 *   2. the measurement FOLLOWS a tempo change rather than averaging it away;
 *   3. no clock at all means no lock and no tempo - nothing is guessed;
 *   4. a locked stream that STOPS loses its lock (a stale tempo is worse than
 *      none: a follower would keep writing it);
 *   5. the master's message set for a known transport script is exactly
 *      START, the pulses the advanced ticks imply, a Song Position Pointer on a
 *      seek, and STOP - asserted on the emitted SEQUENCE, not on "the call
 *      succeeded";
 *   6. the group's three ids carry the A16 classes the contract table states,
 *      read out of the table itself.
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

#include <algorithm>
#include <cmath>

#include <QtTest>

#include <QStringList>
#include <QVector>

#include "ControlReversibility.h"
#include "Engine.h"
#include "MidiClock.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! The interval between two clock pulses at \a bpm, in nanoseconds, from the
//! grid: 24 pulses to a quarter note.
quint64 pulseIntervalNs(int bpm)
{
	return 60000000000ULL / (static_cast<quint64>(bpm) * MidiClockPulsesPerQuarter);
}

//! Feed \a pulses clock pulses at \a bpm, starting at \a timestampNs, and return
//! the timestamp AFTER the last one. Synthetic time, exactly the shape the MIDI
//! reader thread's real timestamps have.
quint64 feedPulses(MidiClockTracker& tracker, int bpm, int pulses, quint64 timestampNs)
{
	const quint64 interval = pulseIntervalNs(bpm);
	for (int i = 0; i < pulses; ++i)
	{
		timestampNs += interval;
		tracker.pulse(timestampNs);
	}
	return timestampNs;
}

//! Feed clock pulses through the ENGINE's own input entry point - the one the
//! MIDI client's reader thread calls - rather than into a tracker directly, so
//! the follower test measures the path a real incoming clock takes.
quint64 feedClockInput(MidiClock* clock, int bpm, int pulses, quint64 timestampNs)
{
	const quint64 interval = pulseIntervalNs(bpm);
	for (int i = 0; i < pulses; ++i)
	{
		timestampNs += interval;
		clock->handleInputMessage(MidiClockMessage::Clock, 0, timestampNs);
	}
	return timestampNs;
}

//! The pulse count the generator's own arithmetic implies for \a periods audio
//! periods of \a frames samples at the engine's current frame/tick scalar. The
//! SAME accumulation the engine runs (add, then subtract while the remainder is
//! a whole pulse), so this is not a second opinion - it is the arithmetic being
//! measured against itself, which is what catches a wrong divisor.
int impliedPulses(int frames, int periods)
{
	const double framesPerTick = static_cast<double>(Engine::framesPerTick());
	if (framesPerTick <= 0.0) { return 0; }
	double remainder = 0.0;
	int pulses = 0;
	const double pulseTicks = static_cast<double>(TicksPerMidiClockPulse);
	for (int i = 0; i < periods; ++i)
	{
		remainder += static_cast<double>(frames) / framesPerTick;
		while (remainder >= pulseTicks)
		{
			remainder -= pulseTicks;
			++pulses;
		}
	}
	return pulses;
}

//! The monitor's fresh messages, OLDEST first, with the pulses left in: the order
//! the master actually emitted since \a sinceTotal was read, which is what a
//! slave would have received.
QStringList emittedSequence(const MidiClock& clock, quint32 sinceTotal)
{
	QStringList out;
	const quint32 fresh = clock.emittedTotal() - sinceTotal;
	const quint32 available = std::min<quint32>(fresh,
		static_cast<quint32>(MidiClock::MonitorCapacity));
	for (quint32 i = available; i-- > 0; )
	{
		out.append(midiClockMessageName(clock.emittedAt(static_cast<int>(i))));
	}
	return out;
}

//! The sequence with the clock pulses removed: the transport messages in order.
QStringList transportMessages(const QStringList& sequence)
{
	QStringList out;
	for (const QString& name : sequence)
	{
		if (name != QStringLiteral("clock")) { out.append(name); }
	}
	return out;
}

} // namespace

class MidiClockTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::getSong() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void cleanup()
	{
		// Never leak a clock mode into the next slot: the singleton is one object
		// for the whole run, and a master left enabled would keep emitting.
		MidiClock* clock = MidiClock::instance();
		QString ignored;
		clock->setMasterEnabled(false, QString(), &ignored);
		clock->setSlaveEnabled(false);
	}

	// ---------------------------------------------------------------- tracker

	//! THE RATE. 48 pulses at 120 BPM - two quarter notes - and the measured
	//! tempo is 120, from the grid and nothing else.
	void trackerMeasuresTheRateOfTheStreamItWasGiven()
	{
		MidiClockTracker tracker;
		QVERIFY(!tracker.locked());
		QCOMPARE(tracker.tempoBpm(), 0.0);

		feedPulses(tracker, 120, 48, 1000000ULL);

		QVERIFY2(tracker.locked(), "48 pulses at 120 BPM must lock");
		QCOMPARE(qRound(tracker.tempoBpm()), 120);
		QCOMPARE(tracker.pulseCount(), 48);
		// One window is one quarter note, whose length at 120 BPM is 500 ms.
		QVERIFY2(std::fabs(tracker.windowMs() - 500.0) < 1.0,
			qPrintable(QStringLiteral("window=%1 ms").arg(tracker.windowMs())));
		// The drift of the last interval from the window's mean, and the error a
		// jittering reader can produce over that window: both are numbers the
		// surface reports, so both are asserted here rather than assumed.
		QVERIFY2(std::fabs(tracker.driftMs()) < 0.001,
			qPrintable(QStringLiteral("drift=%1 ms").arg(tracker.driftMs())));
		QVERIFY2(tracker.tempoErrorBoundBpm() > 0.0 && tracker.tempoErrorBoundBpm() < 5.0,
			qPrintable(QStringLiteral("bound=%1 BPM").arg(tracker.tempoErrorBoundBpm())));
	}

	//! THE TEMPO CHANGE, which is the whole point of following a clock: a stream
	//! that moves from 120 to 140 BPM is measured at 140, not averaged.
	void trackerFollowsATempoChange()
	{
		MidiClockTracker tracker;
		quint64 now = feedPulses(tracker, 120, 48, 1000000ULL);
		QCOMPARE(qRound(tracker.tempoBpm()), 120);

		now = feedPulses(tracker, 140, 48, now);
		QCOMPARE(qRound(tracker.tempoBpm()), 140);
		QVERIFY2(std::fabs(tracker.windowMs() - 428.57) < 2.0,
			qPrintable(QStringLiteral("window=%1 ms").arg(tracker.windowMs())));
	}

	//! NO CLOCK, NO TEMPO. An enabled slave with nothing arriving must report
	//! unlocked and must not invent a rate from one pulse or from nothing.
	void trackerReportsNothingWhenNoClockArrives()
	{
		MidiClockTracker tracker;
		tracker.update(1000000000ULL);
		QVERIFY(!tracker.locked());
		QCOMPARE(tracker.tempoBpm(), 0.0);

		// A single pulse is a position, not a rate: one point cannot measure one.
		tracker.pulse(5000000000ULL);
		tracker.pulse(5000000000ULL + pulseIntervalNs(120));
		QVERIFY2(!tracker.locked(), "two pulses must not lock: the window needs a quarter note");
		QCOMPARE(tracker.tempoBpm(), 0.0);
	}

	//! A STALE LOCK IS WORSE THAN NO LOCK: when the pulses stop, the tempo goes.
	void trackerDropsItsLockWhenTheStreamStops()
	{
		MidiClockTracker tracker;
		const quint64 last = feedPulses(tracker, 120, 48, 1000000ULL);
		QVERIFY(tracker.locked());

		tracker.update(last + 100000000ULL);        // 100 ms later: still live
		QVERIFY(tracker.locked());

		tracker.update(last + 600000000ULL);        // past LockTimeoutMs
		QVERIFY2(!tracker.locked(), "a clock that stopped arriving must unlock");
		QCOMPARE(tracker.tempoBpm(), 0.0);

		// ... and a STOP message unlocks immediately, without waiting it out.
		MidiClockTracker second;
		const quint64 stopAt = feedPulses(second, 120, 48, 1000000ULL);
		QVERIFY(second.locked());
		second.received(MidiClockMessage::Stop, 0, stopAt + 1000);
		QVERIFY2(!second.locked(), "a STOP message ends the lock at once");
	}

	//! The two payload-carrying inputs are decoded and reported: the position in
	//! MIDI beats and the time-code nibble, counted and exposed.
	void trackerReportsSongPositionAndTimeCode()
	{
		MidiClockTracker tracker;
		tracker.received(MidiClockMessage::SongPosition, 97, 1000);
		tracker.received(MidiClockMessage::TimeCode, 0x12, 1001);
		QCOMPARE(tracker.lastSongPosition(), 97);
		QCOMPARE(tracker.lastTimeCode(), 0x12);
		QCOMPARE(tracker.messageCount(MidiClockMessage::SongPosition), 1u);
		QCOMPARE(tracker.messageCount(MidiClockMessage::TimeCode), 1u);
		QCOMPARE(tracker.messageCount(MidiClockMessage::Clock), 0u);
	}

	// ----------------------------------------------------------------- master

	//! THE MASTER'S MESSAGE SET. Drive one transport script - stopped, start,
	//! two running periods, a seek, stop - and read the emission back out of the
	//! master's own monitor: START, the pulses the advanced ticks imply, a Song
	//! Position Pointer for the seek, STOP. The pulse COUNT is the arithmetic of
	//! TicksPerMidiClockPulse against the engine's own frame/tick scalar, so a
	//! wrong divisor fails here and not only on a hardware trace.
	void masterEmitsTheExpectedMessageSet()
	{
		MidiClock* clock = MidiClock::instance();
		const quint32 pulsesBefore = clock->emittedCount(MidiClockMessage::Clock);
		const quint32 startBefore = clock->emittedCount(MidiClockMessage::Start);
		const quint32 queriesBefore = clock->emittedCount(MidiClockMessage::SongPosition);
		const quint32 stopsBefore = clock->emittedCount(MidiClockMessage::Stop);
		const quint32 totalBefore = clock->emittedTotal();

		QString error;
		QVERIFY2(clock->setMasterEnabled(true, QString(), &error), qPrintable(error));
		QVERIFY(clock->masterEnabled());
		QVERIFY2(clock->periodMs() > 0.0, "the engine must report its own period");

		const int frames = 512;
		quint64 now = 1000000000ULL;
		const qint64 tick = 0;
		// Establish the baseline while stopped: no message, and no START.
		clock->processAudioPeriod(frames, false, tick, now);

		// The rising edge at the top of the song is a START.
		clock->processAudioPeriod(frames, true, tick, now += 1000000ULL);
		// Two running periods: the position advances by the engine's own
		// frame/tick scalar, and one pulse is emitted per two ticks crossed.
		const int advance = static_cast<int>(static_cast<double>(frames)
			/ static_cast<double>(Engine::framesPerTick()));
		qint64 position = tick + advance;
		clock->processAudioPeriod(frames, true, position, now += 1000000ULL);
		position += advance;
		clock->processAudioPeriod(frames, true, position, now += 1000000ULL);

		// A seek while running: the position jumps far past the period's own
		// advance, which a slave cannot know about unless it is told.
		position += 20000;
		clock->processAudioPeriod(frames, true, position, now += 1000000ULL);

		// The falling edge is a STOP.
		clock->processAudioPeriod(frames, false, position, now += 1000000ULL);

		const quint32 expectedPulses = static_cast<quint32>(impliedPulses(frames, 3));
		QVERIFY2(expectedPulses > 0, "the script must cross at least one pulse");
		QCOMPARE(clock->emittedCount(MidiClockMessage::Clock) - pulsesBefore, expectedPulses);
		QCOMPARE(clock->emittedCount(MidiClockMessage::Start) - startBefore, 1u);
		QCOMPARE(clock->emittedCount(MidiClockMessage::SongPosition) - queriesBefore, 1u);
		QCOMPARE(clock->emittedCount(MidiClockMessage::Stop) - stopsBefore, 1u);
		QCOMPARE(clock->emittedTotal() - totalBefore, expectedPulses + 3u);

		const QStringList transport = transportMessages(emittedSequence(*clock, totalBefore));
		QVERIFY2(transport == QStringList({QStringLiteral("start"),
				QStringLiteral("song_position"), QStringLiteral("stop")}),
			qPrintable(QStringLiteral("emitted %1").arg(transport.join(QLatin1Char(',')))));

		// A stopped transport emits nothing further: STOP is an edge, not a state.
		const quint32 afterStop = clock->emittedTotal();
		clock->processAudioPeriod(frames, false, position, now += 1000000ULL);
		clock->processAudioPeriod(frames, false, position, now += 1000000ULL);
		QCOMPARE(clock->emittedTotal(), afterStop);
	}

	//! A DISABLED MASTER EMITS NOTHING, and enabling one is refused typed when
	//! the engine has no MIDI client to send through. Both halves matter: the
	//! first is the "no clock mode on" no-op, the second is what stops the
	//! surface reporting a master that could not emit a byte.
	void masterIsOffUntilItIsEnabled()
	{
		MidiClock* clock = MidiClock::instance();
		QVERIFY(!clock->masterEnabled());
		const quint32 before = clock->emittedTotal();
		for (int i = 0; i < 4; ++i)
		{
			clock->processAudioPeriod(512, true, i * 48, 1000000000ULL + i * 1000000ULL);
		}
		QCOMPARE(clock->emittedTotal(), before);

		QString error;
		QVERIFY(clock->setMasterEnabled(true, QString(), &error));
		QVERIFY(clock->masterEnabled());
		QVERIFY2(clock->setMasterEnabled(false, QString(), &error), qPrintable(error));
		QVERIFY(!clock->masterEnabled());
	}

	// ------------------------------------------------------------------ slave

	//! THE FOLLOWER WRITES THE SONG'S TEMPO, and only when it has a lock. This is
	//! the same call the control thread's poll makes (MidiClock::applyMeasuredTempo),
	//! driven here with synthetic pulses because an incoming clock cannot be
	//! injected into a running instance from outside the process.
	void slaveAppliesTheMeasuredTempoToTheSong()
	{
		MidiClock* clock = MidiClock::instance();
		Song* song = Engine::getSong();
		song->setTempo(100);

		// Enabled but UNLOCKED: with no clock arriving the transport may not move
		// and the tempo may not be written - the engine half of the contract the
		// socket transcript proves end to end.
		clock->setSlaveEnabled(true);
		QVERIFY(clock->slaveEnabled());
		QVERIFY(!clock->tracker().locked());
		QCOMPARE(clock->applyMeasuredTempo(), 0);
		QCOMPARE(song->getTempo(), 100);
		QVERIFY(!song->isPlaying());

		// Enabled and FOLLOWING: 48 pulses at 140 BPM over synthetic time.
		clock->setSlaveFollowTempo(true);
		const quint64 last = feedClockInput(clock, 140, 48, 1000000ULL);
		QVERIFY(clock->tracker().locked());
		QCOMPARE(qRound(clock->tracker().tempoBpm()), 140);
		QCOMPARE(clock->applyMeasuredTempo(), 140);
		QCOMPARE(song->getTempo(), 140);

		// Inside the dead band nothing is written: a follower that chased the
		// last digit would rewrite the project's tempo on every window.
		QCOMPARE(clock->applyMeasuredTempo(), 0);

		// And with following OFF the measurement is reported but never written.
		song->setTempo(100);
		clock->setSlaveFollowTempo(false);
		QCOMPARE(clock->applyMeasuredTempo(), 0);
		QCOMPARE(song->getTempo(), 100);

		// Leaving slave mode forgets the measurement.
		clock->setSlaveEnabled(false);
		QVERIFY(!clock->tracker().locked());
		QCOMPARE(clock->tracker().pulseCount(), 0);
		QVERIFY(last > 0);
	}

	//! THE POINTER'S UNIT. A Song Position Pointer is expressed in MIDI BEATS -
	//! six clock pulses, i.e. a sixteenth note - and this engine resolves a
	//! sixteenth as twelve ticks. It is the one grid conversion that would
	//! otherwise only be checked by a hardware trace, so it is checked here.
	void theSongPositionPointerIsInMidiBeats()
	{
		QCOMPARE(MidiClock::songPositionOf(0), 0u);
		QCOMPARE(MidiClock::songPositionOf(TicksPerMidiBeat), 1u);
		QCOMPARE(MidiClock::songPositionOf(DefaultTicksPerBar), 16u);   // one 4/4 bar
		QCOMPARE(MidiClock::songPositionOf(DefaultTicksPerBar - 1), 15u); // truncated
		QCOMPARE(MidiClock::songPositionOf(-5), 0u);
		QVERIFY(MidiClock::songPositionOf(9999 * DefaultTicksPerBar) <= 0x3FFFu);
	}

	//! The state a caller reads back reports the grid, the mode and - the part
	//! that would otherwise be a claim - that this release has no timecode.
	void stateReportsTheGridAndTheAbsentTimecode()
	{
		const QJsonObject state = MidiClock::instance()->stateJson();
		QCOMPARE(state.value(QStringLiteral("ticks_per_pulse")).toInt(), TicksPerMidiClockPulse);
		QCOMPARE(state.value(QStringLiteral("ticks_per_beat")).toInt(), TicksPerMidiBeat);
		QCOMPARE(state.value(QStringLiteral("pulses_per_quarter")).toInt(),
			MidiClockPulsesPerQuarter);
		QCOMPARE(state.value(QStringLiteral("mtc")).toString(), QStringLiteral("absent"));
		QVERIFY(state.contains(QStringLiteral("master")));
		QVERIFY(state.contains(QStringLiteral("slave")));
		const QJsonObject slave = state.value(QStringLiteral("slave")).toObject();
		QVERIFY(slave.contains(QStringLiteral("locked")));
		QVERIFY(slave.contains(QStringLiteral("tempo_error_bound_bpm")));
		QCOMPARE(slave.value(QStringLiteral("locked")).toBool(), false);
	}

	// ------------------------------------------------------------------- A16

	//! THE CONTRACT ROWS. Not "a row exists" - the CLASS each id declares, read
	//! out of the table, because the whole point of clock.slave_set being
	//! `snapshot` is that a tempo-following slave has no trivially invertible
	//! answer and the table says so instead of the handler asserting otherwise.
	void theGroupCarriesTheDocumentedA16Classes()
	{
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		auto cls = [&table](const QString& id) {
			const control::ReversibilityEntry* entry = table.lookup(id);
			return entry != nullptr ? entry->cls : control::ReversibilityClass::NotMutating;
		};
		const control::ReversibilityEntry* state = table.lookup(QStringLiteral("clock.get_state"));
		QVERIFY(state != nullptr);
		QCOMPARE(cls(QStringLiteral("clock.get_state")), control::ReversibilityClass::NotMutating);
		QCOMPARE(cls(QStringLiteral("clock.master_set")), control::ReversibilityClass::TrueInverse);
		QCOMPARE(cls(QStringLiteral("clock.slave_set")), control::ReversibilityClass::Snapshot);
		QVERIFY(!state->reversible);

		// The slave row must NAME the part of the command its inverse cannot
		// restore, and must offer the fallback that does. A row that said only
		// "the configuration is restored" would be the overclaim this class
		// exists to prevent.
		const control::ReversibilityEntry* slave = table.lookup(QStringLiteral("clock.slave_set"));
		QVERIFY(slave != nullptr);
		QVERIFY2(slave->reason.contains(QStringLiteral("trajectory")),
			qPrintable(slave->reason));
		QVERIFY2(slave->fallback.contains(QStringLiteral("transport.set_tempo")),
			qPrintable(slave->fallback));
	}
};

QTEST_GUILESS_MAIN(MidiClockTest)
#include "MidiClockTest.moc"
