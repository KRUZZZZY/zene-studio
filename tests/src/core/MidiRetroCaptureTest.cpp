/*
 * MidiRetroCaptureTest.cpp - the MIDI-side contract of retrospective capture:
 *                            off by default, allocation-free and lock-free on
 *                            the MIDI input thread, and lossless mapping of a
 *                            MidiEvent into the ring's snapshot element
 *                            (owner item 14)
 *
 * Copyright (c) 2026 LMMS developers
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

#include "RetroMidiCapture.h"

#include <QtTest>

#include <cstdint>
#include <thread>
#include <vector>

#include "AllocationProbe.h"
#include "Midi.h"
#include "MidiEvent.h"

using lmms::MidiEvent;
using lmms::RetroMidiCapture;
using lmms::RetroMidiEvent;

namespace
{

qulonglong asNumber(std::size_t value) { return static_cast<qulonglong>(value); }

//! A synthetic hardware control-change, the event the input paths deliver.
MidiEvent makeCc(int channel, int controller, int value)
{
	return MidiEvent(lmms::MidiControlChange, channel, controller, value);
}

} // namespace


class MidiRetroCaptureTest : public QObject
{
	Q_OBJECT

private slots:
	//! OFF BY DEFAULT: a fresh capture records nothing, and the disarmed path
	//! allocates nothing on the MIDI input thread.
	void OffByDefault_RecordsNothingAndDoesNotAllocate()
	{
		RetroMidiCapture capture;
		QVERIFY(!capture.isArmed());

		std::uint64_t allocations = 0;
		std::thread inputThread([&capture, &allocations] {
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			for (int i = 0; i < 1000; ++i)
			{
				capture.capture(makeCc(0, 1, i % 128), static_cast<std::uint32_t>(i));
			}
			lmms::test::tlCountAllocations = false;
			allocations = lmms::test::tlAllocationCount;
		});
		inputThread.join();

		QCOMPARE(asNumber(static_cast<std::size_t>(allocations)), asNumber(0));
		QCOMPARE(asNumber(capture.ring().bufferedCount()), asNumber(0));
		QCOMPARE(asNumber(capture.ring().overwrittenCount()), asNumber(0));
		QCOMPARE(asNumber(capture.ring().pausedDropCount()), asNumber(0));
	}

	//! Armed: events delivered from a non-GUI thread land in the ring, in
	//! order, without one allocation on that thread - the programme's gold
	//! standard for a realtime path (AGENTS.md rule 4; tests/src/core/AllocationProbe.h).
	void Armed_RecordsFromTheMidiInputThreadWithoutAllocating()
	{
		constexpr int events = 1000;
		RetroMidiCapture capture;
		capture.arm(true);

		std::uint64_t allocations = 0;
		Qt::HANDLE inputThreadId = nullptr;
		std::thread inputThread([&capture, &allocations, &inputThreadId] {
			inputThreadId = QThread::currentThreadId();
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			for (int i = 0; i < events; ++i)
			{
				capture.capture(makeCc(3, 74, i % 128), static_cast<std::uint32_t>(i), 21);
			}
			lmms::test::tlCountAllocations = false;
			allocations = lmms::test::tlAllocationCount;
		});
		inputThread.join();

		// The write happened on that thread, not on this one.
		QVERIFY(inputThreadId != nullptr);
		QVERIFY(inputThreadId != QThread::currentThreadId());
		QCOMPARE(asNumber(static_cast<std::size_t>(allocations)), asNumber(0));

		QCOMPARE(asNumber(capture.ring().bufferedCount()), asNumber(events));
		QCOMPARE(asNumber(capture.ring().overwrittenCount()), asNumber(0));

		std::vector<RetroMidiEvent> window(events);
		QCOMPARE(asNumber(capture.ring().copyOut(window.data(), window.size())), asNumber(events));
		for (int i = 0; i < events; ++i)
		{
			const auto& recorded = window[static_cast<std::size_t>(i)];
			QCOMPARE(static_cast<unsigned>(recorded.tick), static_cast<unsigned>(i));
			QCOMPARE(static_cast<unsigned>(recorded.type), static_cast<unsigned>(lmms::MidiControlChange));
			QCOMPARE(static_cast<unsigned>(recorded.channel), 3u);
			QCOMPARE(static_cast<unsigned>(recorded.param1), 74u);
			QCOMPARE(static_cast<unsigned>(recorded.param2), static_cast<unsigned>(i % 128));
			QCOMPARE(static_cast<unsigned>(recorded.source), 21u);
			QVERIFY((recorded.flags & lmms::RetroMidiFlagExternal) != 0);
		}
	}

	//! Disarming stops the recording and leaves the window intact.
	void Disarm_StopsRecordingAndKeepsTheWindow()
	{
		RetroMidiCapture capture;
		capture.arm(true);
		for (int i = 0; i < 4; ++i) { capture.capture(makeCc(0, 1, i), 10); }
		QCOMPARE(asNumber(capture.ring().bufferedCount()), asNumber(4));

		QVERIFY(!capture.arm(false));
		QVERIFY(!capture.isArmed());
		for (int i = 0; i < 16; ++i) { capture.capture(makeCc(0, 1, i), 11); }

		QCOMPARE(asNumber(capture.ring().bufferedCount()), asNumber(4));
		std::vector<RetroMidiEvent> window(4);
		QCOMPARE(asNumber(capture.ring().copyOut(window.data(), window.size())), asNumber(4));
		QCOMPARE(static_cast<unsigned>(window[3].param2), 3u);
	}

	//! Every channel message keeps the parameters MidiEvent's accessors expose,
	//! and pitch bend keeps all 14 bits.
	void Mapping_IsLosslessForChannelMessages()
	{
		RetroMidiCapture capture;
		capture.arm(true);

		capture.capture(MidiEvent(lmms::MidiNoteOn, 2, 60, 100), 100, 7);
		capture.capture(MidiEvent(lmms::MidiNoteOff, 2, 60, 64), 101, 7);
		capture.capture(MidiEvent(lmms::MidiKeyPressure, 2, 60, 90), 102, 7);
		capture.capture(MidiEvent(lmms::MidiProgramChange, 2, 12, 0), 103, 7);
		capture.capture(MidiEvent(lmms::MidiChannelPressure, 2, 55, 0), 104, 7);
		capture.capture(MidiEvent(lmms::MidiPitchBend, 2, 8192, 0), 105, 7);   // centre
		capture.capture(MidiEvent(lmms::MidiPitchBend, 2, 16383, 0), 106, 7);  // full up

		std::vector<RetroMidiEvent> window(16);
		QCOMPARE(asNumber(capture.ring().copyOut(window.data(), window.size())), asNumber(7));

		QCOMPARE(static_cast<unsigned>(window[0].type), static_cast<unsigned>(lmms::MidiNoteOn));
		QCOMPARE(static_cast<unsigned>(window[0].param1), 60u);
		QCOMPARE(static_cast<unsigned>(window[0].param2), 100u);

		QCOMPARE(static_cast<unsigned>(window[1].type), static_cast<unsigned>(lmms::MidiNoteOff));
		QCOMPARE(static_cast<unsigned>(window[1].param2), 64u);

		QCOMPARE(static_cast<unsigned>(window[2].type), static_cast<unsigned>(lmms::MidiKeyPressure));
		QCOMPARE(static_cast<unsigned>(window[2].param1), 60u);
		QCOMPARE(static_cast<unsigned>(window[2].param2), 90u);

		QCOMPARE(static_cast<unsigned>(window[3].type), static_cast<unsigned>(lmms::MidiProgramChange));
		QCOMPARE(static_cast<unsigned>(window[3].param1), 12u);

		QCOMPARE(static_cast<unsigned>(window[4].type), static_cast<unsigned>(lmms::MidiChannelPressure));
		QCOMPARE(static_cast<unsigned>(window[4].param1), 55u);

		// 8192 = 0x2000 -> low seven bits 0, high seven bits 64.
		QCOMPARE(static_cast<unsigned>(window[5].type), static_cast<unsigned>(lmms::MidiPitchBend));
		QCOMPARE(static_cast<unsigned>(window[5].param1), 0u);
		QCOMPARE(static_cast<unsigned>(window[5].param2), 64u);

		// 16383 = 0x3FFF -> 127 / 127.
		QCOMPARE(static_cast<unsigned>(window[6].param1), 127u);
		QCOMPARE(static_cast<unsigned>(window[6].param2), 127u);
	}

	//! A SysEx is recorded as one flagged placeholder rather than lost silently;
	//! transport bytes are not recorded at all.
	void SysEx_IsFlaggedAndTransportBytesAreNotRecorded()
	{
		RetroMidiCapture capture;
		capture.arm(true);

		capture.capture(MidiEvent(lmms::MidiSysEx, "abc", 3), 200, 0);
		QCOMPARE(asNumber(capture.ring().bufferedCount()), asNumber(1));

		std::vector<RetroMidiEvent> window(4);
		QCOMPARE(asNumber(capture.ring().copyOut(window.data(), window.size())), asNumber(1));
		QVERIFY((window[0].flags & lmms::RetroMidiFlagSysExDropped) != 0);
		QCOMPARE(static_cast<unsigned>(window[0].type), static_cast<unsigned>(lmms::MidiSysEx));

		capture.capture(MidiEvent(lmms::MidiActiveSensing), 201, 0);
		capture.capture(MidiEvent(lmms::MidiSync), 202, 0);
		capture.capture(MidiEvent(lmms::MidiStop), 203, 0);
		QCOMPARE(asNumber(capture.ring().bufferedCount()), asNumber(1));
	}

	//! The published tick is what the raw clients stamp their events with; it
	//! round-trips from another thread and reaches the element unchanged.
	void TickPublisher_IsReadableFromTheMidiInputThreadAndStampsTheElement()
	{
		retroCapturePublishTickFromAnotherThread(4242);
		QCOMPARE(RetroMidiCapture::publishedTick(), 4242u);

		RetroMidiCapture capture;
		capture.arm(true);
		capture.capture(makeCc(0, 1, 5), RetroMidiCapture::publishedTick());

		std::vector<RetroMidiEvent> window(1);
		QCOMPARE(asNumber(capture.ring().copyOut(window.data(), window.size())), asNumber(1));
		QCOMPARE(window[0].tick, 4242u);
	}

	//! The consumer's snapshot handshake, through the capture object.
	void Snapshot_FreezesTheWindowAndIsReleasedAfterwards()
	{
		RetroMidiCapture capture;
		capture.arm(true);
		for (int i = 0; i < 8; ++i) { capture.capture(makeCc(0, 1, i), static_cast<std::uint32_t>(i)); }

		capture.ring().beginSnapshot();
		QVERIFY(!capture.ring().writerIdle());
		QVERIFY(!capture.ring().push(RetroMidiEvent{}));  // the producer's acknowledgement
		QVERIFY(capture.ring().writerIdle());

		std::vector<RetroMidiEvent> window(8);
		QCOMPARE(asNumber(capture.ring().copyOut(window.data(), window.size())), asNumber(8));
		QCOMPARE(static_cast<unsigned>(window[0].param2), 0u);
		QCOMPARE(static_cast<unsigned>(window[7].param2), 7u);
		QCOMPARE(asNumber(capture.ring().refusedSnapshots()), asNumber(0));

		capture.ring().endSnapshot();
		capture.capture(makeCc(0, 1, 9), 9);
		QCOMPARE(asNumber(capture.ring().bufferedCount()), asNumber(9));
	}

private:
	//! Publish the tick from a thread that is not this one, the way the audio
	//! thread does, and prove it allocated nothing while doing it.
	static void retroCapturePublishTickFromAnotherThread(std::uint32_t tick)
	{
		std::uint64_t allocations = 0;
		std::thread audioThread([tick, &allocations] {
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			RetroMidiCapture::publishTick(tick);
			lmms::test::tlCountAllocations = false;
			allocations = lmms::test::tlAllocationCount;
		});
		audioThread.join();
		QCOMPARE(asNumber(static_cast<std::size_t>(allocations)), asNumber(0));
	}
};


QTEST_APPLESS_MAIN(MidiRetroCaptureTest)

#include "MidiRetroCaptureTest.moc"
