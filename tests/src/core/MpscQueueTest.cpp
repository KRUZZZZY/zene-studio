/*
 * MpscQueueTest.cpp - the bounded lock-free MPSC queue (JACK MIDI out, E25)
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

#include <QtTest>

#include <set>
#include <thread>
#include <vector>

#include "MpscQueue.h"

using namespace lmms;

class MpscQueueTest : public QObject
{
	Q_OBJECT

private slots:
	//! Four producers push 4 x 2000 distinct values while one consumer pops: every value arrives
	//! exactly once (a full queue makes a producer retry, as a caller counting drops would not).
	void everyPushedValueArrivesOnce()
	{
		MpscQueue<int, 256> queue;
		constexpr int Producers = 4;
		constexpr int PerProducer = 2000;
		std::vector<std::thread> producers;
		for (int p = 0; p < Producers; ++p)
		{
			producers.emplace_back([&queue, p] {
				for (int i = 0; i < PerProducer; ++i)
				{
					while (!queue.push(p * PerProducer + i)) { std::this_thread::yield(); }
				}
			});
		}
		std::set<int> seen;
		int value = 0;
		while (static_cast<int>(seen.size()) < Producers * PerProducer)
		{
			if (queue.pop(&value)) { QVERIFY2(seen.insert(value).second, "a value arrived twice"); }
		}
		for (auto& t : producers) { t.join(); }
		QVERIFY(!queue.pop(&value));
	}

	void aFullQueueRefusesAndAnEmptyOneReportsIt()
	{
		MpscQueue<int, 4> queue;
		int value = 0;
		QVERIFY(!queue.pop(&value));
		for (int i = 0; i < 4; ++i) { QVERIFY(queue.push(i)); }
		QVERIFY(!queue.push(99));
		for (int i = 0; i < 4; ++i)
		{
			QVERIFY(queue.pop(&value));
			QCOMPARE(value, i);  // one producer: FIFO
		}
		QVERIFY(!queue.pop(&value));
	}
};

QTEST_GUILESS_MAIN(MpscQueueTest)
#include "MpscQueueTest.moc"
