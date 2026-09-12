/*
 * WarpMarkersTest.cpp - task #597: the warp map itself.
 *
 * Copyright (c) 2026 Zene Studio developers
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

// The two properties a warp engine is judged on are **monotonic** and **exact
// at the markers** (docs/CLIP-CAPTURE-DESIGN.md §2.4; #597's acceptance). This
// file measures both on the map itself, with the window and the base rate
// supplied explicitly so the numbers in the assertions are the numbers a
// reader can recompute by hand.
//
// The fixture below is deliberately arithmetic:
//
//   window [0, 100000), base rate 100 source frames per tick
//   A = (source 10000, tick 20)   B = (source 30000, tick 60)   C = (source 50000, tick 120)
//
//   segment A..B: 20000 frames over 40 ticks  -> 500 frames/tick
//   segment B..C: 20000 frames over 60 ticks  -> 333.33 frames/tick
//
// so "exactly the base rate" and "exactly the segment slope" are different
// numbers everywhere but the tails, and a mapping that used the wrong one
// could not pass.

#include <QtTest>

#include <array>
#include <cmath>
#include <vector>

#include "../core/AllocationProbe.h"

#include "LmmsTypes.h"
#include "WarpMarkers.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t kSourceIn = 0;
constexpr f_cnt_t kSourceOut = 100000;
constexpr float kBaseRate = 100.0f;

const WarpMarker kA{ 10000, 20 };
const WarpMarker kB{ 30000, 60 };
const WarpMarker kC{ 50000, 120 };

std::array<WarpMarker, 3> fixture() { return { kA, kB, kC }; }

WarpMarkers makeFixture()
{
	WarpMarkers markers;
	const auto set = fixture();
	// NOTE: the call is deliberately OUTSIDE the assertion. Q_ASSERT compiles to
	// nothing under QT_NO_DEBUG (this build is RelWithDebInfo), so an assertion
	// with a side effect would silently skip it - which is exactly what the
	// first run of this file did.
	const auto accepted = markers.set(std::span<const WarpMarker>(set.data(), set.size()));
	Q_ASSERT(accepted);
	(void)accepted;
	return markers;
}

f_cnt_t at(const WarpMarkers& markers, tick_t offset)
{
	return markers.sourceFrameAt(offset, kSourceIn, kSourceOut, kBaseRate);
}

tick_t tickAt(const WarpMarkers& markers, f_cnt_t frame)
{
	return markers.timelineOffsetAt(frame, kSourceIn, kBaseRate);
}

//! f_cnt_t is `unsigned long` on LP64: comparing it to an `unsigned int`
//! literal would not deduce QTest::qCompare's template, so numeric frame
//! assertions go through this.
template <typename T>
qulonglong N(T value) { return static_cast<qulonglong>(value); }

} // namespace


class WarpMarkersTest : public QObject
{
	Q_OBJECT

private slots:
	//! A set that is not strictly increasing in both coordinates is not a
	//! mapping. It is refused outright, and the previous set is left alone.
	void nonMonotonicSetsAreRejected()
	{
		WarpMarkers markers = makeFixture();
		const auto before = markers;

		// same timeline position for two different source frames
		const std::array<WarpMarker, 2> flat{ WarpMarker{ 1000, 20 }, WarpMarker{ 2000, 20 } };
		QVERIFY(!markers.set(std::span<const WarpMarker>(flat.data(), flat.size())));
		QVERIFY(markers == before);

		// source frames increase, timeline positions fall: a rewind, not a warp
		const std::array<WarpMarker, 2> backwards{ WarpMarker{ 1000, 60 }, WarpMarker{ 2000, 20 } };
		QVERIFY(!markers.set(std::span<const WarpMarker>(backwards.data(), backwards.size())));
		QVERIFY(markers == before);

		// duplicate source frame
		const std::array<WarpMarker, 2> twice{ WarpMarker{ 1000, 20 }, WarpMarker{ 1000, 40 } };
		QVERIFY(!markers.set(std::span<const WarpMarker>(twice.data(), twice.size())));
		QVERIFY(markers == before);
	}

	//! The set is sorted, so the mapping does not depend on the order it was
	//! authored in; and the capacity is a hard limit, not a silent truncation.
	void setsAreSortedAndCapacityIsRefused()
	{
		WarpMarkers markers;
		const std::array<WarpMarker, 3> shuffled{ kC, kA, kB };
		QVERIFY(markers.set(std::span<const WarpMarker>(shuffled.data(), shuffled.size())));
		QCOMPARE(markers.size(), 3);
		QVERIFY(markers[0] == kA);
		QVERIFY(markers[1] == kB);
		QVERIFY(markers[2] == kC);
		QVERIFY(at(markers, 40) == at(makeFixture(), 40));

		std::vector<WarpMarker> tooMany(static_cast<std::size_t>(WarpMarkers::MaxMarkers) + 1);
		for (std::size_t i = 0; i < tooMany.size(); ++i)
		{
			tooMany[i] = { static_cast<f_cnt_t>(i + 1) * 10, static_cast<tick_t>(i + 1) };
		}
		QVERIFY(!markers.set(std::span<const WarpMarker>(tooMany.data(), tooMany.size())));
		QCOMPARE(markers.size(), 3);
	}

	//! **Exact at every marker.** The interpolation numerator is zero at a
	//! marker, so its own source frame comes back with no interpolation error -
	//! in both directions.
	void exactAtEveryMarker()
	{
		const auto markers = makeFixture();
		for (const auto& marker : fixture())
		{
			QCOMPARE(at(markers, marker.offsetTicks), marker.sourceFrame);
			QCOMPARE(tickAt(markers, marker.sourceFrame), marker.offsetTicks);
		}
	}

	//! Between two markers the map interpolates linearly in both coordinates:
	//! a constant rate for the segment, which is what makes a marker pair a
	//! tempo change rather than a curve.
	void interpolatesLinearlyBetweenMarkers()
	{
		const auto markers = makeFixture();

		// segment A..B (20000 frames over 40 ticks)
		QCOMPARE(N(at(markers, 40)), N(20000));   // midway
		QCOMPARE(N(at(markers, 30)), N(15000));   // a quarter in
		QCOMPARE(N(at(markers, 50)), N(25000));   // three quarters in

		// segment B..C (20000 frames over 60 ticks)
		QCOMPARE(N(at(markers, 90)), N(40000));
		QCOMPARE(N(at(markers, 63)), N(31000));   // 30000 + 20000 * 3 / 60

		// the inverse interpolates on the same line
		QCOMPARE(tickAt(markers, 20000), 40);
		QCOMPARE(tickAt(markers, 40000), 90);
	}

	//! Outside the outermost markers the map continues at the clip's own base
	//! rate, extrapolated from the nearest marker - so it is CONTINUOUS at the
	//! outer markers rather than stepping to the clip origin.
	void extrapolatesAtTheBaseRateOutsideTheMarkers()
	{
		const auto markers = makeFixture();

		QCOMPARE(N(at(markers, 0)), N(8000));     // 10000 - 20 * 100
		QCOMPARE(N(at(markers, 10)), N(9000));    // 10000 - 10 * 100
		QCOMPARE(N(at(markers, 20)), N(10000));   // exactly the first marker
		QCOMPARE(N(at(markers, 130)), N(51000));  // 50000 + 10 * 100
		QCOMPARE(N(at(markers, 200)), N(58000));

		QCOMPARE(tickAt(markers, 8000), 0);
		QCOMPARE(tickAt(markers, 51000), 130);
	}

	//! **Monotonic.** Swept across both tails and every segment, the map never
	//! goes backwards. This is the property that makes a warp safe to render.
	void monotonicAcrossTheWholeRange()
	{
		const auto markers = makeFixture();
		f_cnt_t previous = 0;
		bool first = true;
		for (tick_t offset = -500; offset <= 500; ++offset)
		{
			const auto frame = at(markers, offset);
			if (!first) { QVERIFY(frame >= previous); }
			previous = frame;
			first = false;
		}

		// and the inverse is monotonic in the source domain
		tick_t lastTick = -1;
		bool firstTick = true;
		for (f_cnt_t frame = 0; frame <= 100000; frame += 37)
		{
			const auto tick = tickAt(markers, frame);
			if (!firstTick) { QVERIFY(tick >= lastTick); }
			lastTick = tick;
			firstTick = false;
		}
	}

	//! The forward map and the inverse agree to within one segment's own
	//! frames-per-tick (the tick is the coarser unit, so that is the exact
	//! bound, not a tolerance someone chose).
	void forwardAndInverseRoundTrip()
	{
		const auto markers = makeFixture();
		for (tick_t offset = 0; offset <= 140; ++offset)
		{
			const auto frame = at(markers, offset);
			const auto rate = markers.framesPerTickAt(frame, kBaseRate);
			const auto back = at(markers, tickAt(markers, frame));
			const auto bound = static_cast<f_cnt_t>(std::ceil(rate)) + 1;
			QVERIFY2(frame >= back && frame - back <= bound,
				qPrintable(QString("offset %1: %2 -> %3 (bound %4)")
					.arg(offset).arg(asNumber(frame)).arg(asNumber(back)).arg(asNumber(bound))));
		}
	}

	//! The rate a *render* uses from a source frame onwards. Segments are
	//! half-open to the right and the tails are exactly the base rate.
	void rateIsTheSegmentSlope()
	{
		const auto markers = makeFixture();

		QCOMPARE(markers.framesPerTickAt(5000, kBaseRate), kBaseRate);      // before the first
		QCOMPARE(markers.framesPerTickAt(10000, kBaseRate), 500.0f);        // AT the first: segment 0
		QCOMPARE(markers.framesPerTickAt(29999, kBaseRate), 500.0f);
		QCOMPARE(markers.framesPerTickAt(30000, kBaseRate), 20000.0f / 60.0f);
		QCOMPARE(markers.framesPerTickAt(60000, kBaseRate), kBaseRate);     // past the last
		QCOMPARE(markers.framesPerTickAt(999999, kBaseRate), kBaseRate);

		// a single marker defines no segment: the tails' rate is all there is
		WarpMarkers one;
		const std::array<WarpMarker, 1> single{ kB };
		QVERIFY(one.set(std::span<const WarpMarker>(single.data(), single.size())));
		QCOMPARE(one.framesPerTickAt(30000, kBaseRate), kBaseRate);
	}

	//! The result never leaves the clip's own window: a window that cuts a
	//! marker off keeps the mapping well formed instead of reading past it.
	void resultIsClampedToTheWindow()
	{
		const auto markers = makeFixture();

		// no clamp where the mapping is already inside the window ...
		QCOMPARE(N(markers.sourceFrameAt(200, 20000, 100000, kBaseRate)), N(58000));
		// ... and the clamp bites at both ends
		QCOMPARE(N(markers.sourceFrameAt(0, 20000, 100000, kBaseRate)), N(20000));
		QCOMPARE(N(markers.sourceFrameAt(200, 0, 40000, kBaseRate)), N(40000));
		QCOMPARE(N(markers.sourceFrameAt(130, 0, 12000, kBaseRate)), N(12000));

		// clamping preserves monotonicity
		f_cnt_t previous = 0;
		for (tick_t offset = -100; offset <= 300; ++offset)
		{
			const auto frame = markers.sourceFrameAt(offset, 20000, 90000, kBaseRate);
			QVERIFY(frame >= previous);
			previous = frame;
		}
	}

	//! I8: the lookup path is the audio thread's, so it allocates nothing and
	//! the set is a fixed-capacity value - no growth anywhere.
	void lookupDoesNotAllocate()
	{
		const auto markers = makeFixture();

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		volatile f_cnt_t sink = 0;
		for (int i = 0; i < 10000; ++i)
		{
			sink += at(markers, i % 200);
			sink += static_cast<f_cnt_t>(tickAt(markers, static_cast<f_cnt_t>(i * 7)));
			sink += static_cast<f_cnt_t>(markers.framesPerTickAt(static_cast<f_cnt_t>(i * 3), kBaseRate));
		}
		test::tlCountAllocations = false;
		(void)sink;

		QCOMPARE(asNumber(test::tlAllocationCount), asNumber(0));
	}

private:
	static qulonglong asNumber(std::size_t value) { return static_cast<qulonglong>(value); }
};

QTEST_GUILESS_MAIN(WarpMarkersTest)
#include "WarpMarkersTest.moc"
