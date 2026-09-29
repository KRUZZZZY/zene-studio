/*
 * PunchWindowTest.cpp - R2.4: the punch region's frame-accurate audio gate
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

/*! punchFramesForPeriod() decides, once per engine period on the render thread, which frames
 *  the armed recorders take. Every case a punch pass meets is held here as arithmetic, then
 *  the whole pass is held end to end: a long run of periods over a region captures exactly
 *  the region's frames - no frame twice, none missing, none outside.
 */

#include <QtTest>

#include "PunchWindow.h"

using namespace lmms;

namespace
{

constexpr double kFpt = 100.0;  // frames per tick, so ticks read as hundreds of frames
constexpr f_cnt_t kPeriod = 256;

} // namespace

class PunchWindowTest : public QObject
{
	Q_OBJECT

private slots:
	void noArmedRegionCapturesEverything()
	{
		const PunchFrames all = punchFramesForPeriod(false, false, 12345.0, kFpt, kPeriod, 0, 0);
		QCOMPARE(all.begin, f_cnt_t{0});
		QCOMPARE(all.end, kPeriod);
	}

	void anArmedRegionWithTheTransportStoppedCapturesNothing()
	{
		QCOMPARE(punchFramesForPeriod(true, false, 0.0, kFpt, kPeriod, 0, 100).count(), f_cnt_t{0});
	}

	void aPeriodEntirelyInsideCapturesItWhole()
	{
		// Region ticks [10, 20) = frames [1000, 2000); the period [1200, 1456).
		const PunchFrames inside = punchFramesForPeriod(true, true, 1200.0, kFpt, kPeriod, 10, 20);
		QCOMPARE(inside.begin, f_cnt_t{0});
		QCOMPARE(inside.end, kPeriod);
	}

	void punchInMidPeriodStartsAtThatFrame()
	{
		// The period [900, 1156) meets punch-in at song frame 1000 = its frame 100.
		const PunchFrames in = punchFramesForPeriod(true, true, 900.0, kFpt, kPeriod, 10, 20);
		QCOMPARE(in.begin, f_cnt_t{100});
		QCOMPARE(in.end, kPeriod);
	}

	void punchOutMidPeriodStopsBeforeThatFrame()
	{
		// The period [1900, 2156) meets punch-out at song frame 2000 = its frame 100.
		const PunchFrames out = punchFramesForPeriod(true, true, 1900.0, kFpt, kPeriod, 10, 20);
		QCOMPARE(out.begin, f_cnt_t{0});
		QCOMPARE(out.end, f_cnt_t{100});
	}

	void periodsBeforeAndAfterCaptureNothing()
	{
		QCOMPARE(punchFramesForPeriod(true, true, 0.0, kFpt, kPeriod, 10, 20).count(), f_cnt_t{0});
		QCOMPARE(punchFramesForPeriod(true, true, 2000.0, kFpt, kPeriod, 10, 20).count(), f_cnt_t{0});
	}

	void aRegionInsideOnePeriodIsCutOutOfIt()
	{
		// Ticks [1, 2) = frames [100, 200) inside the period [0, 256).
		const PunchFrames cut = punchFramesForPeriod(true, true, 0.0, kFpt, kPeriod, 1, 2);
		QCOMPARE(cut.begin, f_cnt_t{100});
		QCOMPARE(cut.end, f_cnt_t{200});
	}

	void aFractionalTickRateIsExactOverAWholePass()
	{
		// 140 BPM at 44.1 kHz is 393.75 frames/tick: a region of 96 ticks is 37800
		// frames. Walk a pass period by period and count what the gate lets through.
		const double fpt = 393.75;
		const tick_t begin = 48;
		const tick_t end = 144;
		long long captured = 0;
		f_cnt_t lastEnd = 0;
		for (double start = 0.0; start < 80000.0; start += kPeriod)
		{
			const PunchFrames window = punchFramesForPeriod(true, true, start, fpt, kPeriod, begin, end);
			QVERIFY(window.begin <= window.end && window.end <= kPeriod);
			captured += window.count();
			lastEnd = window.end;
		}
		Q_UNUSED(lastEnd)
		QCOMPARE(captured, static_cast<long long>((end - begin) * fpt));
	}
};

QTEST_GUILESS_MAIN(PunchWindowTest)
#include "PunchWindowTest.moc"
