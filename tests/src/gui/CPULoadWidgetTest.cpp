/*
 * CPULoadWidgetTest.cpp - the CPU load display's smoothed readout
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

/*! The smoothed CPU load the widget displays (its tooltip's "DSP total"). The widget reads the
 *  profiler's integer reading, so a 1% steady load from a displayed 0% must advance the readout,
 *  a rise of one must keep advancing it, and the readout must come back down - all driven through
 *  CPULoadWidget::setCpuLoadForTesting, so no audio period is timed and the result is deterministic.
 *  The profiler reading is uncapped (AudioEngineProfiler::cpuLoad() has no 100% ceiling), so the
 *  average must also stay correct for large readings whose sum passes INT_MAX. */

#include <QtTest>

#include "AudioEngine.h"
#include "CPULoadWidget.h"
#include "Engine.h"

using namespace lmms;
using namespace lmms::gui;

class CPULoadWidgetTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	// The finding: from a displayed 0% a steady 1% profiler reading must reach the display.
	void aSteadyOnePercentReadingReachesTheReadout()
	{
		CPULoadWidget widget(nullptr);
		widget.setCpuLoadForTesting(1);

		QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
		QVERIFY2(widget.toolTip().contains(QStringLiteral("DSP total: 1%")), qPrintable(widget.toolTip()));

		// The reading is steady: the display holds at 1%, it does not fall back to 0%.
		QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
		QVERIFY2(widget.toolTip().contains(QStringLiteral("DSP total: 1%")), qPrintable(widget.toolTip()));
	}

	// A rise of one above a nonzero display must move it too: 1% -> 2% stalls under plain
	// integer truncation even after the 0% -> 1% case is fixed.
	void everyRiseOfOnePercentAdvancesTheReadout()
	{
		CPULoadWidget widget(nullptr);
		widget.setCpuLoadForTesting(1);
		QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
		QVERIFY2(widget.toolTip().contains(QStringLiteral("DSP total: 1%")), qPrintable(widget.toolTip()));

		widget.setCpuLoadForTesting(2);
		QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
		QVERIFY2(widget.toolTip().contains(QStringLiteral("DSP total: 2%")), qPrintable(widget.toolTip()));
	}

	// The readout comes back down too: an upward-only round would hold a falling 1% at 1%.
	void aReadingThatFallsBackToZeroClearsTheReadout()
	{
		CPULoadWidget widget(nullptr);
		widget.setCpuLoadForTesting(1);
		QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
		QVERIFY2(widget.toolTip().contains(QStringLiteral("DSP total: 1%")), qPrintable(widget.toolTip()));

		widget.setCpuLoadForTesting(0);
		QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
		QVERIFY2(widget.toolTip().contains(QStringLiteral("DSP total: 0%")), qPrintable(widget.toolTip()));
	}

	// Boundary, upward: the profiler reading is uncapped, so a rise from 1,073,741,823 to
	// 1,073,741,824 must still round toward the new reading - the sum-plus-one expression
	// (current + target + 1) overflows signed int for exactly these two valid readings.
	void largeReadingsRoundUpWithoutOverflow()
	{
		CPULoadWidget widget(nullptr);
		widget.setCpuLoadForTesting(1073741823);
		bool reached = false;
		for (int i = 0; i < 64 && !reached; ++i)
		{
			QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
			reached = widget.toolTip().contains(QStringLiteral("DSP total: 1073741823%"));
		}
		QVERIFY2(reached, qPrintable(widget.toolTip()));

		widget.setCpuLoadForTesting(1073741824);
		QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
		QVERIFY2(widget.toolTip().contains(QStringLiteral("DSP total: 1073741824%")), qPrintable(widget.toolTip()));
	}

	// Boundary, downward: 1,500,000,000 falling to 1,000,000,000 sums past INT_MAX, so the
	// plain (current + target) / 2 average overflows too; the midpoint of the two readings is
	// 1,250,000,000 and the readout must land there instead of wrapping.
	void largeReadingsRoundDownWithoutOverflow()
	{
		CPULoadWidget widget(nullptr);
		widget.setCpuLoadForTesting(1500000000);
		bool reached = false;
		for (int i = 0; i < 64 && !reached; ++i)
		{
			QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
			reached = widget.toolTip().contains(QStringLiteral("DSP total: 1500000000%"));
		}
		QVERIFY2(reached, qPrintable(widget.toolTip()));

		widget.setCpuLoadForTesting(1000000000);
		QVERIFY(QMetaObject::invokeMethod(&widget, "updateCpuLoad", Qt::DirectConnection));
		QVERIFY2(widget.toolTip().contains(QStringLiteral("DSP total: 1250000000%")), qPrintable(widget.toolTip()));
	}
};

QTEST_MAIN(CPULoadWidgetTest)
#include "CPULoadWidgetTest.moc"
