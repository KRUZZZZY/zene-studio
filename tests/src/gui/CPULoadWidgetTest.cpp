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
 *  CPULoadWidget::setCpuLoadForTesting, so no audio period is timed and the result is deterministic. */

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
};

QTEST_MAIN(CPULoadWidgetTest)
#include "CPULoadWidgetTest.moc"
