/*
 * PluginParamGestureTest.cpp - the queue that carries a plug-in's gesture marks to the GUI poll
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

/*! Q050-214. The queue on its own: order and values between polls, what a full queue does, and the marks of plug-in
 *  threads crossing to a reader that runs at the same time. PluginGestureUndoTest holds what the history does with them.
 *  Local TSan does not run on this box, so the threaded cases check what a race would break: lost or reordered reports. */

#include <QtTest>

#include <atomic>
#include <thread>

#include "PluginParamGesture.h"

using namespace lmms;

class PluginParamGestureTest : public QObject
{
	Q_OBJECT

private:
	using Gesture = PluginParamGesture;

	//! @a count complete gestures from one thread; each one's value is @a base + its number.
	static void reportGestures(Gesture* gesture, double base, int count)
	{
		std::atomic<double> value{base};
		for (int i = 1; i <= count; ++i)
		{
			gesture->begin(value);
			value.store(base + i, std::memory_order_relaxed);
			gesture->end(value);
		}
	}

private slots:
	void reportsArriveInOrderWithTheirValues()
	{
		Gesture gesture;
		std::atomic<double> value{0.5};
		gesture.begin(value);
		value.store(0.25);
		gesture.end(value);
		gesture.begin(value);
		value.store(0.75);

		Gesture::Edits edits;
		gesture.read(value, &edits);
		QCOMPARE(edits.count, 3u);
		QVERIFY(edits.reports[0].mark == Gesture::Mark::Begin);
		QCOMPARE(edits.reports[0].value, 0.5);
		QVERIFY(edits.reports[1].mark == Gesture::Mark::End);
		QCOMPARE(edits.reports[1].value, 0.25);
		QVERIFY(edits.reports[2].mark == Gesture::Mark::Begin);
		QCOMPARE(edits.reports[2].value, 0.25);
		QVERIFY(edits.settled);
		QCOMPARE(edits.value, 0.75);
		QVERIFY(!edits.lost);

		gesture.read(value, &edits);
		QCOMPARE(edits.count, 0u);
		QVERIFY(edits.settled);
	}

	void aFullQueueDropsAndSaysSo()
	{
		Gesture gesture;
		std::atomic<double> value{0.0};
		for (std::uint32_t i = 0; i < Gesture::Capacity + 3; ++i)
		{
			value.store(i);
			gesture.begin(value);
		}

		Gesture::Edits edits;
		gesture.read(value, &edits);
		QCOMPARE(edits.count, Gesture::Capacity);
		for (std::uint32_t i = 0; i < Gesture::Capacity; ++i) { QCOMPARE(edits.reports[i].value, static_cast<double>(i)); }
		QVERIFY(edits.lost);
		QVERIFY(edits.open);

		// Room again, and the loss was told once.
		gesture.end(value);
		gesture.read(value, &edits);
		QCOMPARE(edits.count, 1u);
		QVERIFY(!edits.lost);
		QVERIFY(!edits.open);
	}

	void positionsWrapTheCellsCleanly()
	{
		Gesture gesture;
		std::atomic<double> value{0.0};
		Gesture::Edits edits;
		for (std::uint32_t round = 0; round < Gesture::Capacity * 3 + 1; ++round)
		{
			value.store(round);
			gesture.end(value);
			gesture.read(value, &edits);
			QCOMPARE(edits.count, 1u);
			QCOMPARE(edits.reports[0].value, static_cast<double>(round));
			QVERIFY(edits.settled);
			QVERIFY(!edits.lost);
		}
	}

	void oneThreadReportingWhileThePollReads()
	{
		const int gestures = 200000;
		Gesture gesture;
		std::atomic<double> unused{0.0};
		std::atomic<bool> running{true};
		std::thread plugin([&] { reportGestures(&gesture, 0.0, gestures); running.store(false); });
		Gesture::Edits edits;
		double last = -1.0;
		std::uint32_t taken = 0;
		bool more = true;
		while (more)
		{
			more = running.load();  // read once more after it stopped
			gesture.read(unused, &edits);
			for (std::uint32_t i = 0; i < edits.count; ++i)
			{
				QVERIFY2(edits.reports[i].value >= last, "a report came out of order");
				last = edits.reports[i].value;
			}
			taken += edits.count;
			if (edits.count == 0) { std::this_thread::yield(); }
		}
		plugin.join();
		QVERIFY(taken > 0u);
		QVERIFY(last >= 0.0 && last <= gestures);
	}

	//! Two threads reporting at once (a VST3 editor out of contract): nothing torn, each thread's order kept.
	void twoThreadsReportingAtOnce()
	{
		const int gestures = 100000;
		const double second = 1e7;
		Gesture gesture;
		std::atomic<double> unused{0.0};
		std::atomic<int> running{2};
		const auto run = [&](double base) { reportGestures(&gesture, base, gestures); running.fetch_sub(1); };
		std::thread one(run, 0.0);
		std::thread two(run, second);
		Gesture::Edits edits;
		double lastOne = -1.0;
		double lastTwo = -1.0;
		bool more = true;
		while (more)
		{
			more = running.load() != 0;  // read once more after both stopped
			gesture.read(unused, &edits);
			for (std::uint32_t i = 0; i < edits.count; ++i)
			{
				const double value = edits.reports[i].value;
				double& last = value >= second ? lastTwo : lastOne;
				QVERIFY2(value >= last && value <= second + gestures, "a report was torn or out of order");
				last = value;
			}
			if (edits.count == 0) { std::this_thread::yield(); }
		}
		one.join();
		two.join();
		QVERIFY(lastOne >= 0.0 || lastTwo >= second);  // on a loaded box one thread may find the queue full throughout
	}
};

QTEST_GUILESS_MAIN(PluginParamGestureTest)
#include "PluginParamGestureTest.moc"
