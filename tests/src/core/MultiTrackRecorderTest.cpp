/*
 * MultiTrackRecorderTest.cpp - invariants of the two-track capture owner
 *                              (task #556)
 *
 * Copyright (c) 2026 LMMS developers
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

#include "MultiTrackRecorder.h"

#include <QtTest>

#include <cstdint>
#include <vector>

#include <QTemporaryDir>

#include "SampleFrame.h"

using lmms::MultiTrackRecorder;
using lmms::SampleFrame;

class MultiTrackRecorderTest : public QObject
{
	Q_OBJECT

private slots:
	//! Out-of-range track indexes are rejected before touching any track.
	void ArmTrackRejectsOutOfRangeIndexes()
	{
		MultiTrackRecorder recorder;
		QVERIFY(!recorder.armTrack(-1, "unused.wav", 44100, 0));
		QVERIFY(!recorder.armTrack(MultiTrackRecorder::NumTracks, "unused.wav", 44100, 0));
		QVERIFY(!recorder.armTrack(MultiTrackRecorder::NumTracks + 7, "unused.wav", 44100, 0));
		// In-range index: the failure comes from the track's own arm(), which
		// must also leave the recorder disarmed.
		QVERIFY(!recorder.armTrack(0, "/nonexistent-dir/unwritable.wav", 44100, 0));
		QVERIFY(!recorder.track(0).isArmed());
	}

	//! totalOverflowCount() sums the per-track ring-buffer counters.
	void TotalOverflowCountAggregatesBothTracks()
	{
		MultiTrackRecorder recorder;
		QCOMPARE(recorder.totalOverflowCount(), std::uint64_t{0});
		QCOMPARE(recorder.track(0).overflowCount(), std::uint64_t{0});
		QCOMPARE(recorder.track(1).overflowCount(), std::uint64_t{0});

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QVERIFY(recorder.armTrack(0, dir.filePath("track0.wav").toStdString(), 48000, 0));
		QVERIFY(recorder.track(0).isArmed());
		QVERIFY(!recorder.track(1).isArmed());

		std::vector<SampleFrame> period(512, SampleFrame{0.25f, 0.5f});
		recorder.processInput(period.data(), static_cast<lmms::f_cnt_t>(period.size()));
		QCOMPARE(recorder.track(0).framesPushed(), std::uint64_t{512});

		const std::uint64_t perTrack =
			recorder.track(0).overflowCount() + recorder.track(1).overflowCount();
		QCOMPARE(recorder.totalOverflowCount(), perTrack);

		recorder.disarmAll();
		QVERIFY(!recorder.track(0).isArmed());
		QVERIFY(!recorder.track(1).isArmed());
	}
} ;

QTEST_APPLESS_MAIN(MultiTrackRecorderTest)

#include "MultiTrackRecorderTest.moc"
