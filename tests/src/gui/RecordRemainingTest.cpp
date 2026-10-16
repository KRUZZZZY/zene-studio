/*
 * RecordRemainingTest.cpp - the header's Rec left readout (record.remaining in the UI)
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

/*! 0.5.0 Q050-179 (WF-085): the header's readouts show record.remaining's estimate next to the record
 *  mode, so the recording budget is visible in the GUI while a route is armed and while a pass runs.
 *  Zero, finite and unavailable estimates each get their words ("0m", "2h 3m", "-"); the readout
 *  refreshes through the readouts' tick (the main window's periodic update), and record.remaining's
 *  socket reply keeps the keys its transcript tests measure (the handler is untouched). */

#include <QtTest>

#include "ControlRegistry.h"
#include "Engine.h"
#include "TransportReadouts.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult remaining(double seconds)
{
	return ControlResult::success({{QStringLiteral("seconds"), seconds}});
}

} // namespace

class RecordRemainingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aZeroEstimateSaysTheBudgetIsSpent()
	{
		QCOMPARE(recordRemainingLabel(remaining(0.0)), QStringLiteral("0m"));
		QCOMPARE(recordRemainingLabel(remaining(0.9)), QStringLiteral("0m"));
	}

	void aFiniteEstimateIsMinutesAndHours()
	{
		QCOMPARE(recordRemainingLabel(remaining(119.0)), QStringLiteral("1m"));
		QCOMPARE(recordRemainingLabel(remaining(7380.0)), QStringLiteral("2h 3m"));
	}

	void anUnavailableEstimateSaysSoRatherThanZero()
	{
		const ControlResult refused = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot measure the volume holding 'x'"));
		QCOMPARE(recordRemainingLabel(refused), QStringLiteral("-"));
		QCOMPARE(recordRemainingLabel(ControlResult::success()), QStringLiteral("-"));
	}

	void theReadoutShowsTheCommandTheAgentsAreShown()
	{
		TransportReadouts readouts;
		ReadoutField* field = readouts.recordRemaining();
		QVERIFY(field != nullptr);
		// One caption row of the header, bound to the same command the agent surface answers.
		QVERIFY2(field->toolTip().contains(QStringLiteral("record.remaining")), qPrintable(field->toolTip()));
		const ControlResult live = ControlRegistry::instance()->invoke(QStringLiteral("record.remaining"));
		QVERIFY2(live.ok, qPrintable(live.errorMessage));
		QVERIFY2(live.result.value(QStringLiteral("seconds")).isDouble(),
			"record.remaining must still report a 'seconds' number (the socket reply is unchanged)");
		QCOMPARE(field->valueText(), recordRemainingLabel(live));
	}

	void theTickKeepsTheEstimateRunningWithTheBudget()
	{
		TransportReadouts readouts;
		ReadoutField* field = readouts.recordRemaining();

		// The readouts' tick is one per header frame (the main window's 60 Hz update; the
		// recording budget gets its news once a second). Two seconds of ticking re-ask
		// record.remaining; the text keeps naming the command's answer.
		for (int i = 0; i < 121; ++i) { readouts.tick(); }
		const ControlResult live = ControlRegistry::instance()->invoke(QStringLiteral("record.remaining"));
		QCOMPARE(field->valueText(), recordRemainingLabel(live));
	}
};

QTEST_MAIN(RecordRemainingTest)
#include "RecordRemainingTest.moc"
