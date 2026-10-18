/*
 * TapTempoTest.cpp - the sync button does not change project tempo before a
 *                    tap interval has been measured.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include "Engine.h"
#include "Song.h"
#define private public
#include "TapTempo.h"
#undef private
using namespace lmms;

class TapTempoTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init(true); }

	void cleanupTestCase()
	{
		Engine::getSong()->stop();
		Engine::destroy();
	}

	void aPendingRecordPressDoesNotCarryIntoTheNextProject()
	{
		Song* song = Engine::getSong();
		song->record();
		QVERIFY(song->isRecording());
		song->clearProject();
		QVERIFY(!song->isRecording());
		song->playSong();
		QVERIFY(!song->isRecording());
		song->stop();
	}

	void syncWithoutMeasuredTempoLeavesSongTempoUnchanged()
	{
		Song* song = Engine::getSong();
		const auto originalTempo = song->getTempo();

		TapTempo tapTempo;
		QCOMPARE(tapTempo.bpm(), 0.0);
		tapTempo.sync();

		QCOMPARE(song->getTempo(), originalTempo);
	}

	void rollingIntervalWindowIsPreservedAcrossMedian()
	{
		using namespace std::chrono_literals;
		using clock = TapTempo::clock;

		TapTempo tapTempo;
		const auto t0 = clock::time_point{} + std::chrono::seconds{10};
		tapTempo.updateTempo(t0);
		tapTempo.updateTempo(t0 + 300ms);
		tapTempo.updateTempo(t0 + 400ms);
		tapTempo.updateTempo(t0 + 600ms);
		tapTempo.updateTempo(t0 + 750ms);

		QCOMPARE(tapTempo.m_taps, 4);
		QCOMPARE(tapTempo.m_intervals[0], std::chrono::milliseconds{150});
		QCOMPARE(tapTempo.m_intervals[1], std::chrono::milliseconds{100});
		QCOMPARE(tapTempo.m_intervals[2], std::chrono::milliseconds{200});
	}
};

QTEST_MAIN(TapTempoTest)

#include "TapTempoTest.moc"
