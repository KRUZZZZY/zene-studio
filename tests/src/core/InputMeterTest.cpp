/*
 * InputMeterTest.cpp - 0.5.0 L08 (MX-134): the input meter's live reading beside meter.input's take
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

/*! MX-134: a live input meter needs a reading it can poll without emptying the peaks meter.input takes, so
 *  inputmeter::live() answers the peaks since its own last read and leaves take() alone. measure() runs on the audio
 *  thread: it must not allocate (the allocation probe counts every operator new on this thread). Headless:
 *  no Engine, no audio device. */

#include <QtTest>

#include <array>
#include <atomic>

#include "AllocationProbe.h"
#include "InputMeter.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t Frames = 256;

//! A period at @a left / @a right on every frame, with one negative sample (the meter reads magnitudes).
std::array<SampleFrame, Frames> period(float left, float right)
{
	std::array<SampleFrame, Frames> frames{};
	for (auto& frame : frames) { frame = SampleFrame(left * 0.5f, right * 0.5f); }
	frames[Frames / 2] = SampleFrame(-left, -right);
	return frames;
}

//! The reader the seam plays: it clears the cell the raiser has just observed - the one instant a
//!  reader can land in, between the raise's observation and its commit.
void clearTheObservedCell(std::atomic<float>& held, float)
{
	held.exchange(0.f, std::memory_order_relaxed);
}

} // namespace

class InputMeterTest : public QObject
{
	Q_OBJECT

private slots:
	void init()
	{
		float left = 0.f;
		float right = 0.f;
		inputmeter::take(&left, &right);  // a clean start for both readers
		inputmeter::live(&left, &right);
		inputmeter::watch(&left, &right);
	}

	//! Every period counts: a transient between two polls is still read, once.
	void theLiveReadingIsTheMostSinceTheLastRead()
	{
		const auto loud = period(0.5f, 0.25f);
		const auto quiet = period(0.125f, 0.f);
		inputmeter::measure(loud.data(), Frames);
		inputmeter::measure(quiet.data(), Frames);
		float left = -1.f;
		float right = -1.f;
		inputmeter::live(&left, &right);
		QCOMPARE(left, 0.5f);  // the louder period, though the quiet one came last
		QCOMPARE(right, 0.25f);
		inputmeter::live(&left, &right);
		QCOMPARE(left, 0.f);  // nothing since
		inputmeter::measure(quiet.data(), Frames);
		inputmeter::live(&left, &right);
		QCOMPARE(left, 0.125f);
		QCOMPARE(right, 0.f);
	}

	//! The live read and meter.input's take are two readers of their own: neither empties the other.
	void theLiveReadAndTheTakeAreIndependent()
	{
		const auto loud = period(0.5f, 0.25f);
		inputmeter::measure(loud.data(), Frames);
		float left = 0.f;
		float right = 0.f;
		inputmeter::live(&left, &right);
		QCOMPARE(left, 0.5f);
		inputmeter::take(&left, &right);  // meter.input's read, after the live one
		QCOMPARE(left, 0.5f);
		QCOMPARE(right, 0.25f);
		const auto quiet = period(0.125f, 0.f);
		inputmeter::measure(quiet.data(), Frames);
		inputmeter::take(&left, &right);
		QCOMPARE(left, 0.125f);
		inputmeter::live(&left, &right);  // and the take did not empty the live reading
		QCOMPARE(left, 0.125f);
	}

	//! Q050-100: the peak watch has its own read; it neither steals meter.input nor the live meter's peaks.
	void theWatcherHasItsOwnIndependentPeakRead()
	{
		const auto loud = period(0.5f, 0.25f);
		inputmeter::measure(loud.data(), Frames);
		float left = 0.f;
		float right = 0.f;
		inputmeter::watch(&left, &right);
		QCOMPARE(left, 0.5f);
		QCOMPARE(right, 0.25f);
		inputmeter::watch(&left, &right);
		QCOMPARE(left, 0.f);
		QCOMPARE(right, 0.f);
		inputmeter::take(&left, &right);
		QCOMPARE(left, 0.5f);
		QCOMPARE(right, 0.25f);
		inputmeter::live(&left, &right);
		QCOMPARE(left, 0.5f);
		QCOMPARE(right, 0.25f);
	}

	void aPeriodWithNoInputReadsSilence()
	{
		inputmeter::measure(nullptr, 0);  // no capture device: the engine's input bus has no frames
		float left = -1.f;
		float right = -1.f;
		inputmeter::live(&left, &right);
		QCOMPARE(left, 0.f);
		QCOMPARE(right, 0.f);
	}

	//! A reader that clears the peak in the raise's observation window must not swallow the sample:
	//!  the meter keeps it, so the next read still sees it (cluster new-p14-117).
	void aClearInTheObservationWindowDoesNotSwallowTheSample()
	{
		const auto loud = period(0.8f, 0.8f);
		const auto quiet = period(0.5f, 0.5f);
		inputmeter::measure(loud.data(), Frames);  // the meter holds 0.8
		inputmeter::detail::setPeakObservationHook(&clearTheObservedCell);  // a reader, in that window
		inputmeter::measure(quiet.data(), Frames);  // 0.5 is raised into a cell the reader clears
		inputmeter::detail::setPeakObservationHook(nullptr);
		float left = 0.f;
		float right = 0.f;
		inputmeter::take(&left, &right);
		QCOMPARE(left, 0.5f);
		QCOMPARE(right, 0.5f);
		float liveLeft = 0.f;
		float liveRight = 0.f;
		inputmeter::live(&liveLeft, &liveRight);
		QCOMPARE(liveLeft, 0.5f);
		QCOMPARE(liveRight, 0.5f);
	}

	//! A reader with a null output asks for the other side only: the null side is not read, so its peak is held
	//!  for the next reader, and a call with both outputs null reads nothing (cluster new-p21-190).
	void aNullOutputSkipsItsSideOfTheTake()
	{
		const auto loud = period(0.5f, 0.25f);
		inputmeter::measure(loud.data(), Frames);
		float right = -1.f;
		inputmeter::take(nullptr, &right);
		QCOMPARE(right, 0.25f);
		inputmeter::take(nullptr, nullptr);
		float left = -1.f;
		float rightAgain = -1.f;
		inputmeter::take(&left, &rightAgain);
		QCOMPARE(left, 0.5f);  // the left peak was never taken, so it is still held
		QCOMPARE(rightAgain, 0.f);
	}

	//! The same for the live read: a null side is skipped and its peak stays for the live reader's next read.
	void aNullOutputSkipsItsSideOfTheLiveRead()
	{
		const auto loud = period(0.5f, 0.25f);
		inputmeter::measure(loud.data(), Frames);
		float right = -1.f;
		inputmeter::live(nullptr, &right);
		QCOMPARE(right, 0.25f);
		inputmeter::live(nullptr, nullptr);
		float left = -1.f;
		float rightAgain = -1.f;
		inputmeter::live(&left, &rightAgain);
		QCOMPARE(left, 0.5f);
		QCOMPARE(rightAgain, 0.f);
	}

	//! The audio thread's half allocates nothing (realtime rule).
	void measuringAllocatesNothing()
	{
		// The negative control: the probe is live in this binary - a deliberate allocation is counted.
		static int* volatile sink = nullptr;
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		sink = new int(1);
		test::tlCountAllocations = false;
		delete sink;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{1});
		const auto loud = period(0.5f, 0.25f);
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int i = 0; i < 64; ++i)
		{
			inputmeter::measure(loud.data(), Frames);
			inputmeter::measure(nullptr, 0);
		}
		test::tlCountAllocations = false;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(InputMeterTest)
#include "InputMeterTest.moc"
