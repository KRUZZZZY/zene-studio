/*
 * MidiClockCaptureOutputTest.cpp - the output contract of the MIDI clock's capture span (Q050-277, review round 2)
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
 */

/*! Two contracts the capture span's output must keep, each driven with this test's own thread as the AUDIO thread (it
 *  calls captureStarted/processAudioPeriod/captureStopped exactly where the session recorder does):
 *   - ONE PRODUCER PER QUEUE. Ordinary track MIDI goes through MidiClient::processOutEvent() on the audio thread, which
 *     on ALSA feeds a single-producer queue. The capture's sender must therefore never call processOutEvent(): it uses
 *     MidiClient::sendFromNonAudioThread(). A watching client records which thread called which path while track MIDI
 *     and capture clock overlap (MidiOutQueueTest drives the ALSA client's two queues under the same overlap).
 *   - EVERY ACCEPTED START HAS ITS STOP. With the sender held, earlier spans fill the capture ring; successive captures
 *     are then started. A span is either admitted with START and STOP both delivered, or refused before START. */

#include <QtTest>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "MidiClient.h"
#include "MidiClock.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

//! A MIDI client that remembers which thread used which output path.
class WatchingClient : public MidiClient
{
public:
	explicit WatchingClient(std::thread::id audioThread) : m_audioThread(audioThread) {}

	//! The audio path (track MIDI). Its producer must only ever be the audio thread.
	void processOutEvent(const MidiEvent& event, const TimePos&, const MidiPort*) override
	{
		if (std::this_thread::get_id() != m_audioThread) { offThreadAudioPathCalls.fetch_add(1); }
		if (event.type() == MidiNoteOn) { notes.fetch_add(1); }
		else { clockOnAudioPath.fetch_add(1); }
	}

	std::vector<int> offAudioLog() const
	{
		std::lock_guard<std::mutex> lock(m_logMutex);
		return m_log;
	}

	std::atomic<int> offThreadAudioPathCalls{0};
	std::atomic<int> audioThreadOffAudioCalls{0};
	std::atomic<int> notes{0};
	std::atomic<int> clockOnAudioPath{0};

protected:
	bool processNonAudioOutEvent(const MidiEvent& event, const MidiPort*) override
	{
		if (std::this_thread::get_id() == m_audioThread) { audioThreadOffAudioCalls.fetch_add(1); }
		std::lock_guard<std::mutex> lock(m_logMutex);
		m_log.push_back(static_cast<int>(event.type()));
		return true;
	}

private:
	const std::thread::id m_audioThread;
	mutable std::mutex m_logMutex;
	std::vector<int> m_log;
};

//! True once the sender has delivered everything queued.
bool delivered()
{
	for (int i = 0; i < 3000 && MidiClock::instance()->captureOutputPending(); ++i) { QTest::qWait(1); }
	return !MidiClock::instance()->captureOutputPending();
}

} // namespace

class MidiClockCaptureOutputTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::getSong() != nullptr);
		// This test is the audio thread: the Dummy device's own periods must not run beside it.
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void init()
	{
		QString error;
		QVERIFY2(MidiClock::instance()->setMasterEnabled(true, QString(), &error), qPrintable(error));
		QVERIFY(delivered());
	}

	void cleanup()
	{
		MidiClock* clock = MidiClock::instance();
		clock->holdCaptureOutput(false);
		clock->captureAbort();
		QVERIFY(delivered());
		clock->setMasterEnabled(false, QString(), nullptr);
	}

	//! Track MIDI on the audio thread and a four-step capture span overlap on one client. Every processOutEvent() call
	//! comes from the audio thread (the queue it feeds keeps its single producer), the span reaches the client only
	//! through the off-audio path - START, 24 pulses, STOP, in order, each once - and every note arrives once.
	void captureClockLeavesThroughTheOffAudioPathBesideTrackMidi()
	{
		MidiClock* clock = MidiClock::instance();
		WatchingClient watcher(std::this_thread::get_id());
		MidiClient* previous = clock->swapClientForTesting(&watcher);
		const auto frames = static_cast<int>(Engine::audioEngine()->framesPerPeriod());
		const qint64 length = 4 * TicksPerMidiBeat;
		const auto owed = static_cast<quint32>(length / TicksPerMidiClockPulse);

		clock->captureArm();
		clock->captureStarted(0, length);
		int sentNotes = 0;
		for (int i = 0; i < 20000 && clock->captureReport().pulses < owed; ++i)
		{
			watcher.processOutEvent(MidiEvent(MidiNoteOn, 0, 60, 100), TimePos(), nullptr);  // the track's MIDI
			++sentNotes;
			clock->processAudioPeriod(frames, false, 0, MidiClock::nowNs());
			if (i % 4 == 0) { std::this_thread::sleep_for(std::chrono::microseconds(200)); }  // let the sender run between
		}
		clock->captureStopped(length);
		for (int i = 0; i < 200; ++i)  // the sender drains while track MIDI goes on
		{
			watcher.processOutEvent(MidiEvent(MidiNoteOn, 0, 61, 100), TimePos(), nullptr);
			++sentNotes;
			std::this_thread::sleep_for(std::chrono::microseconds(50));
		}
		const bool drained = delivered();
		clock->swapClientForTesting(previous);
		QVERIFY(drained);

		QCOMPARE(watcher.offThreadAudioPathCalls.load(), 0);   // no second producer on the audio path
		QCOMPARE(watcher.audioThreadOffAudioCalls.load(), 0);  // and the audio thread never takes the off-audio path
		QCOMPARE(watcher.clockOnAudioPath.load(), 0);
		QCOMPARE(watcher.notes.load(), sentNotes);
		std::vector<int> expected{MidiStart};
		for (quint32 i = 0; i < owed; ++i) { expected.push_back(MidiSync); }
		expected.push_back(MidiStop);
		const std::vector<int> log = watcher.offAudioLog();
		QCOMPARE(static_cast<int>(log.size()), static_cast<int>(expected.size()));
		QVERIFY(log == expected);
	}

	//! The ring is filled by earlier spans the held sender has not delivered - one with a pulse (3 records) then empty
	//! ones (2 each) until a single slot is left - and more captures are started. A START that fits only without its
	//! STOP must be refused before START; once the sender delivers, STARTs and STOPs match one for one, and a span
	//! started after the ring drained is admitted again.
	void everyAcceptedStartHasItsStopWhenEarlierOutputFillsTheRing()
	{
		MidiClock* clock = MidiClock::instance();
		const MidiClock::CaptureReport before = clock->captureReport();
		const quint32 startsSent = clock->emittedCount(MidiClockMessage::Start);
		const quint32 stopsSent = clock->emittedCount(MidiClockMessage::Stop);
		clock->captureArm();
		clock->holdCaptureOutput(true);

		qint64 tick = 0;
		clock->captureStarted(tick, TicksPerMidiClockPulse);  // one pulse long: START, its pulse, STOP
		clock->captureStopped(tick + TicksPerMidiClockPulse);
		const int attempts = 300;  // more than the ring holds: (512 - 3) / 2 = 254 fit, a 255th START would take the last slot
		for (int i = 0; i < attempts; ++i)
		{
			tick += 4 * TicksPerMidiBeat;
			clock->captureStarted(tick, 0);
			clock->captureStopped(tick);
		}
		const MidiClock::CaptureReport held = clock->captureReport();
		QVERIFY(!held.active);
		QCOMPARE(held.starts - before.starts, held.stops - before.stops);  // every span opened was closed with a queued STOP
		QVERIFY2(held.refused - before.refused > 0, "the full ring refused no span");
		QCOMPARE((held.starts - before.starts) + (held.refused - before.refused), static_cast<quint32>(attempts + 1));

		clock->holdCaptureOutput(false);
		QVERIFY(delivered());
		const quint32 startsDelivered = clock->emittedCount(MidiClockMessage::Start) - startsSent;
		const quint32 stopsDelivered = clock->emittedCount(MidiClockMessage::Stop) - stopsSent;
		QCOMPARE(startsDelivered, held.starts - before.starts);
		QCOMPARE(stopsDelivered, startsDelivered);  // every START the device saw has its STOP

		// Drained: a new span is admitted again, START and STOP both.
		clock->captureArm();
		clock->captureStarted(tick + 4 * TicksPerMidiBeat, 0);
		clock->captureStopped(tick + 4 * TicksPerMidiBeat);
		QVERIFY(delivered());
		QCOMPARE(clock->captureReport().refused, held.refused);
		QCOMPARE(clock->emittedCount(MidiClockMessage::Start) - startsSent, startsDelivered + 1);
		QCOMPARE(clock->emittedCount(MidiClockMessage::Stop) - stopsSent, stopsDelivered + 1);
	}
};

QTEST_GUILESS_MAIN(MidiClockCaptureOutputTest)
#include "MidiClockCaptureOutputTest.moc"
