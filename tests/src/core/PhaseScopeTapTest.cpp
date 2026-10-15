/*
 * PhaseScopeTapTest.cpp - 0.5.0 L08 (MX-137): the stereo phase view's sample tap
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

/*! MX-137: the goniometer reads one mixer channel's newest stereo frames from a tap the mixer's workers push
 *  into (mixerstrip::updateMeters). The tap is the tuner tap's pattern (TunerTap.h) kept stereo: a fixed ring
 *  of atomics in static storage, a seqlock-shaped overrun check, a reset the producer takes. The audio side
 *  must allocate nothing (counted by the allocation probe, with a negative control proving the probe is live)
 *  and take no lock; a reader never sees a frame whose left and right came from two different pushes.
 *  Headless: no Engine, no audio device. */

#include <QtTest>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "AllocationProbe.h"
#include "PhaseScopeTap.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

PhaseScopeTap& tap() { return PhaseScopeTap::instance(); }

//! @a count frames: left = @a base + i, right = -(@a base + i).
std::vector<SampleFrame> ramp(float base, std::size_t count)
{
	std::vector<SampleFrame> frames(count);
	for (std::size_t i = 0; i < count; ++i) { frames[i] = SampleFrame(base + i, -(base + i)); }
	return frames;
}

//! @a count frames of one constant value in both channels: a channel's fingerprints, so a window holding
//! frames of two channels is visible without knowing which push each frame came from.
std::vector<SampleFrame> constant(float value, std::size_t count)
{
	std::vector<SampleFrame> frames(count);
	for (auto& frame : frames) { frame = SampleFrame(value, value); }
	return frames;
}

//! Whether the window holds frames that disagree with its first: a window copied across a rebase.
bool mixesValues(const std::vector<float>& left, const std::vector<float>& right)
{
	for (std::size_t i = 1; i < left.size(); ++i)
	{
		if (left[i] != left[0] || right[i] != right[0]) { return true; }
	}
	return false;
}

} // namespace

class PhaseScopeTapTest : public QObject
{
	Q_OBJECT

private slots:
	void init() { tap().setChannel(PhaseScopeTap::Off); }

	void itListensToNothingByDefault()
	{
		QCOMPARE(tap().channel(), PhaseScopeTap::Off);
		const auto frames = ramp(1.f, 64);
		tap().push(0, frames.data(), 64);
		std::vector<float> left(64);
		std::vector<float> right(64);
		QVERIFY(!tap().latest(left.data(), right.data(), 64));
	}

	void itKeepsTheChosenChannelsNewestFramesInStereo()
	{
		tap().setChannel(3);
		const auto other = ramp(100.f, 64);
		tap().push(2, other.data(), 64);  // not the chosen channel: ignored
		const auto first = ramp(1.f, 64);
		const auto second = ramp(65.f, 64);
		tap().push(3, first.data(), 64);
		tap().push(3, second.data(), 64);
		QCOMPARE(tap().written(), std::uint64_t{128});
		std::vector<float> left(96);
		std::vector<float> right(96);
		QVERIFY(tap().latest(left.data(), right.data(), 96));
		for (std::size_t i = 0; i < 96; ++i)
		{
			QCOMPARE(left[i], 33.f + i);  // oldest first: frames 32..127 of the two pushes
			QCOMPARE(right[i], -(33.f + i));
		}
		std::vector<float> more(129);
		std::vector<float> moreRight(129);
		QVERIFY(!tap().latest(more.data(), moreRight.data(), 129));  // more than came in
	}

	void theRingKeepsTheNewestWhenItWraps()
	{
		tap().setChannel(1);
		const std::size_t block = 1000;
		for (std::size_t n = 0; n < 3 * PhaseScopeTap::Capacity / block + 1; ++n)
		{
			const auto frames = ramp(static_cast<float>(n * block), block);
			tap().push(1, frames.data(), block);
		}
		const std::uint64_t written = tap().written();
		std::vector<float> left(PhaseScopeTap::Capacity);
		std::vector<float> right(PhaseScopeTap::Capacity);
		QVERIFY(tap().latest(left.data(), right.data(), PhaseScopeTap::Capacity));
		QCOMPARE(left.back(), static_cast<float>(written - 1));
		QCOMPARE(left.front(), static_cast<float>(written - PhaseScopeTap::Capacity));
		QVERIFY(!tap().latest(left.data(), right.data(), PhaseScopeTap::Capacity + 1));  // beyond the ring
	}

	void choosingAnotherChannelForgetsTheOld()
	{
		tap().setChannel(1);
		const auto frames = ramp(1.f, 64);
		tap().push(1, frames.data(), 64);
		tap().setChannel(2);
		std::vector<float> left(16);
		std::vector<float> right(16);
		QVERIFY(!tap().latest(left.data(), right.data(), 16));  // nothing of channel 2 yet, and channel 1's is gone
		const auto fresh = ramp(500.f, 16);
		tap().push(2, fresh.data(), 16);
		QVERIFY(tap().latest(left.data(), right.data(), 16));
		QCOMPARE(left.front(), 500.f);
		QCOMPARE(tap().written(), std::uint64_t{16});
	}

	//! The audio thread's half allocates nothing, chosen or not (realtime rule).
	void pushingAllocatesNothing()
	{
		static int* volatile sink = nullptr;
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		sink = new int(1);  // the negative control: the probe is live in this binary
		test::tlCountAllocations = false;
		delete sink;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{1});

		const auto frames = ramp(1.f, 256);
		tap().setChannel(4);
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int i = 0; i < 256; ++i)
		{
			tap().push(4, frames.data(), 256);  // the chosen channel: copied into the ring
			tap().push(5, frames.data(), 256);  // another: one relaxed load
		}
		test::tlCountAllocations = false;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
	}

	//! A channel change racing a reader is refused, never returned as a window that mixes the two
	//! channels. The tap is documented for the GUI/control thread, so setChannel() (control) and
	//! latest() (GUI) may race the audio producer on the same ring; a window copied across the reset
	//! that rebases the ring must be refused, not handed back half from each channel.
	void aChannelChangeDuringACopyIsRefusedNotMixed()
	{
		// One full ring per channel, a constant value each, so a window that mixes the two shows up
		// as frames that do not all carry the same value.
		const auto count = static_cast<f_cnt_t>(PhaseScopeTap::Capacity);
		const auto a = constant(1.f, PhaseScopeTap::Capacity);
		const auto b = constant(-1.f, PhaseScopeTap::Capacity);
		tap().setChannel(8);
		tap().push(8, a.data(), count);

		std::vector<float> left(PhaseScopeTap::Capacity);
		std::vector<float> right(PhaseScopeTap::Capacity);
		// The harness itself reads a window before the writer starts, so "no window" below cannot be
		// mistaken for "the reader never worked".
		QVERIFY(tap().latest(left.data(), right.data(), left.size()));

		std::atomic<bool> stop{false};
		std::atomic<int> switches{0};
		std::thread writer([&] {
			for (int n = 0; !stop.load(std::memory_order_relaxed); ++n)
			{
				const bool other = (n & 1) != 0;
				tap().setChannel(other ? 9 : 8);
				tap().push(other ? 9 : 8, other ? b.data() : a.data(), count);
				switches.fetch_add(1, std::memory_order_relaxed);
			}
		});

		bool mixed = false;
		QElapsedTimer clock;
		clock.start();
		while (!mixed && clock.elapsed() < 2000)
		{
			if (!tap().latest(left.data(), right.data(), left.size())) { continue; }
			mixed = mixesValues(left, right);
		}
		stop.store(true);
		writer.join();
		QVERIFY2(switches.load() > 0, "the writer never ran");
		QVERIFY2(!mixed, "latest() returned a window that mixed two channels");
	}

	//! A reader racing the producer gets a window or a refusal, never a frame mixed from two pushes.
	void aRacingReaderNeverSeesATornFrame()
	{
		tap().setChannel(7);
		std::atomic<bool> stop{false};
		std::thread producer([&stop] {
			std::vector<SampleFrame> frames(128);
			for (float n = 1.f; !stop.load(std::memory_order_relaxed); n += 1.f)
			{
				for (auto& frame : frames) { frame = SampleFrame(n, -n); }
				tap().push(7, frames.data(), frames.size());
				std::this_thread::sleep_for(std::chrono::microseconds(200));  // a period's pace, give or take
			}
		});
		std::vector<float> left(2048);
		std::vector<float> right(2048);
		int windows = 0;
		bool torn = false;
		QElapsedTimer clock;
		clock.start();
		while (clock.elapsed() < 300)
		{
			if (!tap().latest(left.data(), right.data(), left.size())) { continue; }
			++windows;
			for (std::size_t i = 0; i < left.size(); ++i) { torn = torn || left[i] != -right[i]; }
		}
		stop.store(true);
		producer.join();
		QVERIFY(!torn);
		QVERIFY(windows > 0);
	}
};

QTEST_GUILESS_MAIN(PhaseScopeTapTest)
#include "PhaseScopeTapTest.moc"
