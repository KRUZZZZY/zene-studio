/*
 * SessionAudioRecorderTest.cpp - the launcher cell's audio take, as a unit (0.5.0 L02, WF-055)
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

/*! include/SessionAudioRecorder.h without an engine: the round trip skipped from the head, the take running on
 *  past the stop to exactly its span, a fixed length and an earlier stop, events for another cell ignored, a
 *  period before the capture started counted against the head - and the audio thread's three entry points
 *  allocating NOTHING in any state (AllocationProbe.h, the SessionSchedulerTest pattern). SessionAudioRecordTest
 *  proves the same take on the real engine, through the agent surface. */

#include <QtTest>

#include <cstdint>
#include <vector>

#include "AllocationProbe.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "SessionAudioRecorder.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t Period = 256;
constexpr float FramesPerTick = 10.0f;

SampleFrame frameAt(std::uint64_t n)
{
	return SampleFrame(static_cast<float>(n % 1000) / 1000.0f, static_cast<float>(n / 1000) / 1000.0f);
}

//! Feeds periods of the known signal, counting the input stream's frames.
class Feeder
{
public:
	void push(SessionAudioRecorder& recorder, int periods = 1)
	{
		for (int p = 0; p < periods; ++p)
		{
			for (f_cnt_t f = 0; f < Period; ++f) { m_block[static_cast<std::size_t>(f)] = frameAt(m_fed + static_cast<std::uint64_t>(f)); }
			recorder.push(m_block.data(), Period, true, Period);
			m_fed += Period;
		}
	}
	std::uint64_t fed() const { return m_fed; }

private:
	std::vector<SampleFrame> m_block = std::vector<SampleFrame>(Period);
	std::uint64_t m_fed = 0;
};

using State = SessionAudioRecorder::State;

} // namespace

class SessionAudioRecorderTest : public QObject
{
	Q_OBJECT

private slots:
	void theTakeSkipsTheRoundTripAndRunsOnToItsSpan()
	{
		SessionAudioRecorder recorder;
		QVERIFY(recorder.arm(3, 1, 0, 48000, 300));
		QVERIFY(!recorder.arm(3, 2, 0, 48000, 300));  // one take at a time
		Feeder feeder;
		feeder.push(recorder, 2);  // armed, not started: nothing is taken
		QCOMPARE(recorder.framesRecorded(), std::uint64_t{0});
		QCOMPARE(recorder.slotStarted(3, 1, 192, FramesPerTick), tick_t{0});
		QVERIFY(recorder.state() == State::Recording);
		const std::uint64_t startFrame = feeder.fed();
		feeder.push(recorder, 3);  // 768 input frames, 300 of them the round trip
		recorder.slotStopped(3, 1, 192 + 96);  // a 96-tick span: 960 frames
		QVERIFY(recorder.state() == State::Finishing);
		QCOMPARE(recorder.targetFrames(), std::uint64_t{960});
		feeder.push(recorder, 3);
		QVERIFY(recorder.state() == State::Done);
		QCOMPARE(recorder.framesRecorded(), std::uint64_t{960});
		const auto take = recorder.takeFinished();
		QVERIFY(take != nullptr);
		QCOMPARE(take->size(), std::size_t{960});
		for (std::size_t i = 0; i < take->size(); ++i)
		{
			QCOMPARE(take->data()[i].left(), frameAt(startFrame + 300 + i).left());
			QCOMPARE(take->data()[i].right(), frameAt(startFrame + 300 + i).right());
		}
		QVERIFY(recorder.state() == State::Idle);
	}

	void aFixedLengthIsGivenItsStopAndAnEarlierStopShortensIt()
	{
		SessionAudioRecorder recorder;
		QVERIFY(recorder.arm(0, 0, 192, 48000, 0));
		QCOMPARE(recorder.slotStarted(0, 0, 0, FramesPerTick), tick_t{192});  // the cell must stop after a bar
		QCOMPARE(recorder.targetFrames(), std::uint64_t{1920});
		Feeder feeder;
		feeder.push(recorder, 2);
		recorder.slotStopped(0, 0, 96);  // stopped half way
		QCOMPARE(recorder.targetFrames(), std::uint64_t{960});
		feeder.push(recorder, 2);
		QVERIFY(recorder.state() == State::Done);
		QCOMPARE(recorder.takeFinished()->size(), std::size_t{960});

		QVERIFY(recorder.arm(0, 0, 192, 48000, 0));
		recorder.slotStarted(0, 0, 0, FramesPerTick);
		feeder.push(recorder, 10);  // past the length while the cell still plays: the take stops at it
		QCOMPARE(recorder.framesRecorded(), std::uint64_t{1920});
		recorder.slotStopped(0, 0, 192);
		QVERIFY(recorder.state() == State::Done);
	}

	void eventsForAnotherCellAreIgnoredAndCancelDropsTheTake()
	{
		SessionAudioRecorder recorder;
		QVERIFY(recorder.arm(2, 2, 0, 44100, 0));
		QCOMPARE(recorder.slotStarted(2, 3, 0, FramesPerTick), tick_t{0});
		QVERIFY(recorder.state() == State::Armed);
		recorder.slotStarted(2, 2, 0, FramesPerTick);
		recorder.slotStopped(1, 2, 96);
		QVERIFY(recorder.state() == State::Recording);
		recorder.cancel();
		QVERIFY(recorder.state() == State::Idle);
		QVERIFY(recorder.takeFinished() == nullptr);
		QVERIFY(recorder.arm(2, 2, 0, 44100, 0));  // free again
	}

	//! BUGS_FOUND 11.29's rule, as SampleRecordHandle keeps it: a period with no capture yet is a staging block
	//! the take never sees, so it counts against the round trip skipped from the head.
	void aPeriodBeforeTheCaptureStartedCountsAgainstTheHead()
	{
		SessionAudioRecorder recorder;
		QVERIFY(recorder.arm(0, 0, 0, 48000, 2 * Period));
		recorder.slotStarted(0, 0, 0, FramesPerTick);
		recorder.push(nullptr, 0, false, Period);  // the device has delivered nothing yet
		Feeder feeder;
		feeder.push(recorder);
		QCOMPARE(recorder.framesRecorded(), std::uint64_t{0});  // one period of head left, skipped here
		feeder.push(recorder);
		QCOMPARE(recorder.framesRecorded(), std::uint64_t{Period});
	}

	//! The realtime rule: push(), slotStarted() and slotStopped() allocate nothing, in every state.
	void theAudioThreadPathDoesNotAllocate()
	{
		SessionAudioRecorder recorder;
		Feeder feeder;
		feeder.push(recorder);  // the feeder's own block exists before counting
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		feeder.push(recorder, 100);  // idle
		lmms::test::tlCountAllocations = false;
		QVERIFY(recorder.arm(0, 0, 0, 48000, 512));  // the ring and its thread: the control thread's cost
		lmms::test::tlCountAllocations = true;
		feeder.push(recorder, 100);  // armed
		recorder.slotStarted(0, 0, 0, FramesPerTick);
		feeder.push(recorder, 400);  // recording
		recorder.slotStopped(0, 0, 192 * 8);
		feeder.push(recorder, 200);  // finishing, then done
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2(allocations == 0, qPrintable(QStringLiteral("the audio-thread path allocated %1 times").arg(allocations)));
		QVERIFY(recorder.state() == State::Done);
		QCOMPARE(recorder.framesDropped(), std::uint64_t{0});
		// Recording ran past the line the stop named (a span counted in whole ticks can): the take is cut to it.
		QCOMPARE(recorder.takeFinished()->size(), std::size_t{192 * 8 * 10});
	}
};

QTEST_GUILESS_MAIN(SessionAudioRecorderTest)
#include "SessionAudioRecorderTest.moc"
