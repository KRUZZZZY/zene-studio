/*
 * MidiClockSourceTest.cpp - WHICH clock drives the follower (Q050-026)
 *
 * THE DEFECT THIS PROVES. `clock.slave_set` stored a `source_port`, reported it
 * back, and never read it: every MIDI client handed the clock follower bytes
 * with no sender at all (MidiAlsaSeq::run threw `ev->source` away, MidiClientRaw
 * had no port to name), so ANY connected device could lock the tracker and
 * rewrite the project's tempo while the UI showed a source it was supposedly
 * following. This test configures TWO ports and holds both halves: the port that
 * was NOT chosen cannot move the follower's state, and the one that was chosen
 * updates it.
 *
 * WHY IT IS A SEPARATE EXECUTABLE. MidiClockTest.cpp is the group's engine test
 * and sits at this fork's file-length ratchet already (Gate 7 counts CODE lines);
 * the source selection is its own contract with its own vocabulary and its own
 * regressions, so it lives beside it rather than inside it.
 *
 * WHAT IS PROVED, IN ORDER:
 *   1. the unselected device is ignored - no pulses, no lock, no tempo, and no
 *      Song tempo written - while the selected one locks, measures and writes;
 *   2. two devices clocking at once: only the selected one's pulses are counted;
 *   3. the match is the WHOLE name, not a prefix;
 *   4. choosing a different source forgets the measurement, and re-naming the
 *      current one keeps the lock (a repeat is not a change);
 *   5. no source chosen means "any source", which is what this release did
 *      before the source became load-bearing - and a client with one unnamed
 *      input (ALSA Raw-MIDI, JACK, OSS, sndio) arrives exactly that way, through
 *      the real byte parser;
 *   6. a name the follower cannot hold is refused, typed, and changes nothing;
 *   7. `clock.get_state` reports the configured source and the ignored count, so
 *      "nothing is arriving" is distinguishable from "everything arriving is the
 *      wrong device";
 *   8. one control.undo puts the previous source back;
 *   9. a real writer thread rewriting the source while a real reader is reading
 *      it (with a handshake that proves the two overlapped) never admits the
 *      port the messages arrive on, even when the two published names are chosen
 *      so a torn read would spell that port exactly;
 *  10. the state reply's session counters (`emitted_total`, the per-message counts
 *      and `slave.ignored`) stay non-negative above INT_MAX (BUG 9, new-p14-173).
 *      No public path reaches that count in a test, so the counters are written
 *      through the LMMS_TESTING friend seam and read back as the reply's text.
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

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <thread>

#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiClock.h"
#include "MidiClient.h"
#include "MidiEvent.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! The two ports this test plays against, spelled the way ALSA spells them and
//! the way `midi.device_list` lists them under `readable`.
QString portA() { return QStringLiteral("128:0 Sequencer:Ableton Push"); }
QString portB() { return QStringLiteral("129:0 Sequencer:Launchkey"); }
/*! The two names the concurrency slot's writer alternates between, chosen so a
 *  TORN read of the first -> second write would spell portB() itself - the
 *  sender every message in that slot arrives on. `old` is exactly as long as
 *  portB() but a different name, and `new` is longer and STARTS WITH portB(), so
 *  the old length (25) over the new name's first 25 units is "129:0
 *  Sequencer:Launchkey" = portB() exactly. Neither name equals portB(), so no
 *  message there is ever legitimately admitted; a protocol that could tear would
 *  admit one and break the exact pulse count. */
QString portTearOld() { return QStringLiteral("131:0 Sequencer:Launchkey"); }
QString portTearNew() { return QStringLiteral("129:0 Sequencer:Launchkey MK3"); }

//! The interval between two clock pulses at \a bpm, in nanoseconds: 24 pulses to
//! the quarter note, from the grid alone.
quint64 pulseIntervalNs(int bpm)
{
	return 60000000000ULL / (static_cast<quint64>(bpm) * MidiClockPulsesPerQuarter);
}

/*! Feed \a pulses clock pulses at \a bpm into the ENGINE's own input entry point
 *  - the one a MIDI reader thread calls - naming \a source, and return the
 *  timestamp after the last one. Synthetic time, exactly the shape the reader
 *  thread's real timestamps have. */
quint64 feedFrom(MidiClock* clock, const QString& source, int bpm, int pulses, quint64 timestampNs)
{
	const quint64 interval = pulseIntervalNs(bpm);
	for (int i = 0; i < pulses; ++i)
	{
		timestampNs += interval;
		clock->handleInputMessage(MidiClockMessage::Clock, 0, timestampNs, source);
	}
	return timestampNs;
}

//! A raw MIDI client with no device behind it, which can drive parseData() - the
//! byte-at-a-time assembler the raw reader threads (ALSA Raw-MIDI, JACK, OSS,
//! sndio) feed. This client has ONE input and cannot name it, which is why its
//! clock messages must arrive with an empty source.
class NamingRawClient : public MidiClientRaw
{
public:
	void feed(unsigned char byte) { parseData(byte); }

protected:
	void sendByte(const unsigned char) override {}
};

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject slaveJson()
{
	return MidiClock::instance()->stateJson().value(QStringLiteral("slave")).toObject();
}

} // namespace

class MidiClockSourceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::getSong() != nullptr);
		// The test feeds the clock itself; the Dummy device's own periods would
		// interleave (the same reason MidiClockTest stops it).
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void cleanup()
	{
		MidiClock* clock = MidiClock::instance();
		QString error;
		clock->setSlaveSourcePort(QString(), &error);
		clock->setSlaveEnabled(false);
		clock->setSlaveFollowTempo(false);
		Engine::getSong()->setTempo(120);
	}

	//! THE REGRESSION. Two MIDI ports, one chosen: the other device sends a whole
	//! quarter note of pulses at 140 BPM and moves nothing at all - not the pulse
	//! count, not the lock, not the measured tempo, not the Song. The chosen
	//! device then does all four, which is what makes the first half a filter
	//! rather than a clock that never arrived.
	void onlyTheConfiguredSourceCanDriveTheFollower()
	{
		MidiClock* clock = MidiClock::instance();
		Song* song = Engine::getSong();
		song->setTempo(100);
		QString error;
		QVERIFY2(clock->setSlaveSourcePort(portA(), &error), qPrintable(error));
		clock->setSlaveEnabled(true);
		clock->setSlaveFollowTempo(true);
		const quint32 ignoredBefore = clock->slaveIgnoredCount();

		// The UNSELECTED device, at a rate that would be unmistakable if it were
		// followed: a whole quarter note of pulses at 140 BPM.
		feedFrom(clock, portB(), 140, 48, 1000000ULL);

		QVERIFY2(clock->tracker().pulseCount() == 0,
			qPrintable(QStringLiteral("the unselected port fed %1 pulses").arg(clock->tracker().pulseCount())));
		QVERIFY(!clock->tracker().locked());
		QCOMPARE(clock->tracker().tempoBpm(), 0.0);
		QCOMPARE(clock->applyMeasuredTempo(), 0);
		QCOMPARE(song->getTempo(), 100);
		QCOMPARE(clock->slaveIgnoredCount(), ignoredBefore + 48u);

		// The SELECTED device: the same stream updates the follower's state.
		feedFrom(clock, portA(), 140, 48, 4000000000ULL);
		QVERIFY2(clock->tracker().locked(), "48 pulses from the chosen port must lock");
		QCOMPARE(qRound(clock->tracker().tempoBpm()), 140);
		QCOMPARE(clock->tracker().pulseCount(), 48);
		QCOMPARE(clock->applyMeasuredTempo(), 140);
		QCOMPARE(song->getTempo(), 140);
		// ... and the ignored count did not move for the chosen device: it counts
		// what was DROPPED, not what was chosen.
		QCOMPARE(clock->slaveIgnoredCount(), ignoredBefore + 48u);
	}

	//! TWO DEVICES AT ONCE: the arrangement that made the defect visible. Both
	//! clock at the same rate in the same milliseconds - two devices plugged into
	//! one sequencer - and only the chosen device's pulses are counted, so the
	//! measurement is the chosen device's and neither device doubles the other's
	//! rate or halves it.
	void aSecondClockingDeviceDoesNotReachTheFollower()
	{
		MidiClock* clock = MidiClock::instance();
		QString error;
		QVERIFY2(clock->setSlaveSourcePort(portA(), &error), qPrintable(error));
		clock->setSlaveEnabled(true);
		const quint32 ignoredBefore = clock->slaveIgnoredCount();

		const quint64 interval = pulseIntervalNs(120);
		quint64 now = 1000000ULL;
		for (int i = 0; i < 60; ++i)
		{
			now += interval;
			clock->handleInputMessage(MidiClockMessage::Clock, 0, now, portB());
			clock->handleInputMessage(MidiClockMessage::Clock, 0, now, portA());
		}
		QCOMPARE(clock->tracker().pulseCount(), 60);
		QCOMPARE(qRound(clock->tracker().tempoBpm()), 120);
		QCOMPARE(clock->slaveIgnoredCount(), ignoredBefore + 60u);
	}

	//! THE WHOLE NAME, not a prefix: a second device whose name merely STARTS
	//! with the chosen one's is a different device, and following it would be the
	//! same defect with a shorter distance between the two names.
	void theSourceMatchIsTheWholeName()
	{
		MidiClock* clock = MidiClock::instance();
		QString error;
		QVERIFY2(clock->setSlaveSourcePort(portA(), &error), qPrintable(error));
		feedFrom(clock, portA() + QStringLiteral(" 2"), 140, 48, 1000000ULL);
		QCOMPARE(clock->tracker().pulseCount(), 0);
		QVERIFY(!clock->tracker().locked());
	}

	//! A SWITCH forgets what the device moved OFF measured, and a repeat is not a
	//! change: re-naming the configured port must not drop the lock it holds.
	void switchingTheSourceForgetsTheMeasurement()
	{
		MidiClock* clock = MidiClock::instance();
		Song* song = Engine::getSong();
		song->setTempo(100);
		QString error;
		QVERIFY2(clock->setSlaveSourcePort(portA(), &error), qPrintable(error));
		clock->setSlaveEnabled(true);
		feedFrom(clock, portA(), 140, 48, 1000000ULL);
		QVERIFY(clock->tracker().locked());

		QVERIFY2(clock->setSlaveSourcePort(portB(), &error), qPrintable(error));
		QCOMPARE(clock->slaveSourcePort(), portB());
		QVERIFY2(clock->tracker().pulseCount() == 0 && !clock->tracker().locked(),
			"the device moved off must not leave a tempo the follower can still write");
		QCOMPARE(clock->tracker().tempoBpm(), 0.0);

		// The old device is now simply another device.
		feedFrom(clock, portA(), 140, 48, 4000000000ULL);
		QCOMPARE(clock->tracker().pulseCount(), 0);

		// Re-naming the CURRENT source is not a change and keeps its lock.
		feedFrom(clock, portB(), 150, 48, 8000000000ULL);
		QVERIFY(clock->tracker().locked());
		QVERIFY2(clock->setSlaveSourcePort(portB(), &error), qPrintable(error));
		QVERIFY2(clock->tracker().locked(),
			"re-naming the configured source must not drop the lock it holds");
		QCOMPARE(qRound(clock->tracker().tempoBpm()), 150);
	}

	//! NO SOURCE CHOSEN IS "ANY SOURCE" - the behaviour this release had before
	//! the source became load-bearing, and the ONLY one a client with a single
	//! unnamed input can have. Those bytes arrive through the real parser here:
	//! ALSA Raw-MIDI, JACK, OSS and sndio all feed MidiClientRaw::parseData().
	void anySourceIsFollowedWhileNoneIsChosen()
	{
		MidiClock* clock = MidiClock::instance();
		QString error;
		QVERIFY2(clock->setSlaveSourcePort(QString(), &error), qPrintable(error));
		QCOMPARE(clock->slaveSourcePort(), QString());
		const quint32 ignoredBefore = clock->slaveIgnoredCount();
		clock->setSlaveEnabled(true);

		feedFrom(clock, QString(), 120, 48, 1000000ULL);
		QVERIFY2(clock->tracker().locked(), "with no source chosen any source may follow");
		QCOMPARE(qRound(clock->tracker().tempoBpm()), 120);

		// The same bytes through the RAW parser - 0xF8 pulses and a 0xF2 song
		// position - reach the follower, with no source name, because a raw client
		// has one input and cannot say which port a byte came from.
		NamingRawClient client;
		client.feed(0xF8);
		client.feed(0xF8);
		QCOMPARE(clock->tracker().pulseCount(), 50);
		client.feed(0xF2);
		client.feed(0x61);
		client.feed(0x00);
		QCOMPARE(clock->tracker().lastSongPosition(), 97);
		QCOMPARE(clock->slaveIgnoredCount(), ignoredBefore);
	}

	//! THE REFUSAL. A source name the follower cannot hold whole is invalid_args,
	/// not a truncated name: a source that silently became a different name would
	//! follow nothing while reporting that it was following something. The refusal
	//! changes nothing, which is what "the state is as it was" means here.
	void aNameTheFollowerCannotHoldIsRefused()
	{
		MidiClock* clock = MidiClock::instance();
		const QString tooLong(MidiClock::SourceNameCapacity + 1, QLatin1Char('x'));
		QVERIFY2(!MidiClock::sourcePortProblem(tooLong).isEmpty(),
			"an over-long source name must name a problem");
		QVERIFY(MidiClock::sourcePortProblem(
			QString(MidiClock::SourceNameCapacity, QLatin1Char('x'))).isEmpty());

		QString error;
		QVERIFY2(clock->setSlaveSourcePort(portA(), &error), qPrintable(error));
		const QJsonObject before = slaveJson();

		const ControlResult refused = run(QStringLiteral("clock.slave_set"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("source_port"), tooLong}});
		QVERIFY2(!refused.ok, "an unholdable source name must be refused, not truncated");
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(!refused.errorMessage.isEmpty(), "the refusal must say why");
		QCOMPARE(slaveJson(), before);
		QCOMPARE(clock->slaveSourcePort(), portA());
	}

	//! THE READ-BACK. `clock.get_state` reports the configured source and the
	//! ignored count, which is how a caller tells "no clock is arriving" (pulses
	//! 0, ignored 0) from "the clock is arriving on the port nobody chose" (pulses
	//! 0, ignored climbing).
	void theStateReportsTheSourceAndTheIgnoredCount()
	{
		MidiClock* clock = MidiClock::instance();
		QString error;
		QVERIFY2(clock->setSlaveSourcePort(portA(), &error), qPrintable(error));
		clock->setSlaveEnabled(true);

		// `ignored` counts for the session, so it is read as a DELTA: what this
		// slot proves is that the number moves with the messages the source
		// filter drops, not what any earlier slot left behind.
		const int ignoredBefore = slaveJson().value(QStringLiteral("ignored")).toInt();
		const QJsonObject before = slaveJson();
		QCOMPARE(before.value(QStringLiteral("source_port")).toString(), portA());

		feedFrom(clock, portB(), 120, 5, 1000000ULL);
		const QJsonObject after = slaveJson();
		QCOMPARE(after.value(QStringLiteral("ignored")).toInt(), ignoredBefore + 5);
		QCOMPARE(after.value(QStringLiteral("pulses")).toInt(), 0);
		QVERIFY(!after.value(QStringLiteral("locked")).toBool());
		QCOMPARE(after.value(QStringLiteral("source_port")).toString(), portA());

		// The command answers the same numbers, so an agent that just set a source
		// reads back what it set without a second round trip.
		const ControlResult set = run(QStringLiteral("clock.slave_set"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("source_port"), portB()}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("source_port")).toString(), portB());
		QCOMPARE(set.result.value(QStringLiteral("ignored")).toInt(), ignoredBefore + 5);
	}

	//! ONE UNDO STEP. Choosing a source is one recorded action step like every
	//! other configuration write, so control.undo puts the previous one back - and
	//! the measurement does NOT come back with it (a source switch forgets it).
	void oneUndoStepRestoresThePreviousSource()
	{
		MidiClock* clock = MidiClock::instance();
		QString error;
		QVERIFY2(clock->setSlaveSourcePort(portA(), &error), qPrintable(error));
		clock->setSlaveEnabled(true);
		feedFrom(clock, portA(), 140, 48, 1000000ULL);
		QVERIFY(clock->tracker().locked());

		QVERIFY2(run(QStringLiteral("clock.slave_set"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("source_port"), portB()}}).ok,
			"switching the source must be accepted");
		QCOMPARE(clock->slaveSourcePort(), portB());

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(undone.result.value(QStringLiteral("undone_command")).toString(),
			QStringLiteral("clock.slave_set"));
		QCOMPARE(clock->slaveSourcePort(), portA());
		QVERIFY2(!clock->tracker().locked(),
			"the undone switch restores the port, not the tempo that port had measured");
	}

	//! CONCURRENCY (rework r4). The source name is published to the MIDI reader
	//! thread while the control thread may rewrite it, so this slot runs a REAL
	//! writer thread against a REAL reader with a HANDSHAKE that proves the two
	//! overlapped: the writer counts every publishSource() it makes (odd while one
	//! is in flight), and the reader only counts a message once its own read of
	//! that counter straddled a publish - the counter moved during the read, or
	//! was odd at both ends of it. The writer alternates between portTearOld() and
	//! portTearNew(), the pair whose torn read spells portB() (see the two
	//! helpers): a protocol that could tear would admit a portB() pulse and break
	//! the exact count; the fenced sequence lock cannot tear, so the count stays
	//! exact. A data race on the buffer is what a hosted TSan build of this file
	//! reports (ThreadSanitizer cannot run on the authoring box, so this is
	//! registered in the hosted TSan job too).
	void concurrentReconfigurationNeverAdmitsAnotherPort()
	{
		MidiClock* clock = MidiClock::instance();
		QString error;
		QVERIFY2(clock->setSlaveSourcePort(portA(), &error), qPrintable(error));
		clock->setSlaveEnabled(true);

		// The writer's publish sequence: +1 before each publishSource(), +1 after,
		// so the counter is ODD exactly while a write is in flight.
		std::atomic<quint32> publishSeq{0};
		std::atomic<bool> stop{false};
		std::thread writer([clock, &stop, &publishSeq]() {
			bool flip = false;
			while (!stop.load(std::memory_order_relaxed))
			{
				publishSeq.fetch_add(1, std::memory_order_acq_rel);
				QString ignore;
				clock->setSlaveSourcePort(flip ? portTearNew() : portTearOld(), &ignore);
				publishSeq.fetch_add(1, std::memory_order_acq_rel);
				flip = !flip;
			}
		});

		const quint32 before = clock->slaveIgnoredCount();
		const int messages = 20000;
		// Wait (bounded) for the writer to be mid-publish before reading, so the
		// handshake measures the race and not the writer's own thread startup.
		for (int spin = 0; spin < 1000000
			&& (publishSeq.load(std::memory_order_acquire) & 1u) == 0u; ++spin)
		{
			std::this_thread::yield();
		}
		int overlappedReads = 0;
		int reads = 0;
		// A bound on the total feeds, so a reader that (impossibly) never straddled
		// a publish fails the assertion below instead of spinning forever.
		const int maxReads = messages * 100;
		while (overlappedReads < messages && reads < maxReads)
		{
			// The handshake: read the counter, take the message in, read it
			// again. The read overlapped a publish iff the counter moved during
			// it or was odd at both ends - a publish was in flight across the
			// whole read. A read that did not overlap is fed again, so the slot
			// keeps taking messages until `messages` of them have each provably
			// overlapped a write; a re-feed is harmless while nothing is
			// admitted, which is what is being proved.
			const quint32 first = publishSeq.load(std::memory_order_acquire);
			clock->handleInputMessage(MidiClockMessage::Clock, 0, 1000000ULL + reads, portB());
			const quint32 second = publishSeq.load(std::memory_order_acquire);
			++reads;
			if (first != second || (first & 1u) != 0u) { ++overlappedReads; }
		}
		stop.store(true, std::memory_order_relaxed);
		writer.join();

		// Every portB message was dropped, so none reached the tracker: the
		// writer's switches reset it and the reader adds nothing to it.
		QVERIFY2(overlappedReads == messages,
			"every counted read overlapped a publish, so the race was really exercised");
		QCOMPARE(clock->slaveIgnoredCount(), before + static_cast<quint32>(reads));
		QCOMPARE(clock->tracker().pulseCount(), 0);
		QVERIFY2(!clock->tracker().locked(), "no portB message may lock the follower");
	}

	//! BUG 9 (new-p14-173): the state reply cast every session counter to int, so a
	//! count above INT_MAX came out negative. This writes every slot of each counter
	//! array (the message-to-slot mapping does not matter when every slot holds the
	//! same value), reads the reply back as the text a client receives, and puts the
	//! singleton's counters back before any assertion can return early.
	void stateCountersAboveInt32MaxAreNotNegative()
	{
		constexpr quint32 Big = 3000000000u;
		static_assert(Big > static_cast<quint32>(std::numeric_limits<int>::max()),
			"the probe must sit above INT_MAX, where the old cast went negative");
		MidiClock* clock = MidiClock::instance();
		const quint32 emittedTotal = clock->m_emittedTotal.load();
		const quint32 ignoredTotal = clock->m_ignoredTotal.load();
		std::array<quint32, 7> emittedCounts{};
		std::array<quint32, 7> messageCounts{};
		for (std::size_t i = 0; i < emittedCounts.size(); ++i)
		{
			emittedCounts[i] = clock->m_emittedCounts[i].load();
			messageCounts[i] = clock->m_tracker.m_messageCounts[i].load();
			clock->m_emittedCounts[i].store(Big);
			clock->m_tracker.m_messageCounts[i].store(Big);
		}
		clock->m_emittedTotal.store(Big);
		clock->m_ignoredTotal.store(Big);

		const QJsonObject state = QJsonDocument::fromJson(
			QJsonDocument(clock->stateJson()).toJson(QJsonDocument::Compact)).object();

		for (std::size_t i = 0; i < emittedCounts.size(); ++i)
		{
			clock->m_emittedCounts[i].store(emittedCounts[i]);
			clock->m_tracker.m_messageCounts[i].store(messageCounts[i]);
		}
		clock->m_emittedTotal.store(emittedTotal);
		clock->m_ignoredTotal.store(ignoredTotal);

		const QJsonObject master = state.value(QStringLiteral("master")).toObject();
		QCOMPARE(master.value(QStringLiteral("emitted_total")).toDouble(), static_cast<double>(Big));
		const QJsonObject emitted = master.value(QStringLiteral("emitted")).toObject();
		QCOMPARE(emitted.size(), static_cast<qsizetype>(6));
		for (auto it = emitted.constBegin(); it != emitted.constEnd(); ++it)
		{
			QCOMPARE(it.value().toDouble(), static_cast<double>(Big));
		}

		const QJsonObject slave = state.value(QStringLiteral("slave")).toObject();
		QCOMPARE(slave.value(QStringLiteral("ignored")).toDouble(), static_cast<double>(Big));
		const QJsonObject messages = slave.value(QStringLiteral("messages")).toObject();
		QCOMPARE(messages.size(), static_cast<qsizetype>(6));
		for (auto it = messages.constBegin(); it != messages.constEnd(); ++it)
		{
			QCOMPARE(it.value().toDouble(), static_cast<double>(Big));
		}
	}
};

QTEST_GUILESS_MAIN(MidiClockSourceTest)
#include "MidiClockSourceTest.moc"