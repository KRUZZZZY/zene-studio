/*
 * MultiTrackRecorderTest.cpp - invariants of the two-track capture owner
 *                              (task #556)
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

#include "MultiTrackRecorder.h"

#include <QtTest>

#include <cstdint>
#include <system_error>
#include <vector>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "RecordingJournal.h"
#include "SampleFrame.h"

using lmms::MultiTrackRecorder;
using lmms::SampleFrame;
using lmms::TrackRecorder;
using lmms::TakeJournal;
namespace recjournal = lmms::recordingjournal;

class MultiTrackRecorderTest : public QObject
{
	Q_OBJECT

private slots:
	//! A null block or a channel count below one is refused before any track is walked
	//! (gate 2, hosted run 36747963198: both early returns entered uncovered).
	void NullOrChannellessInputIsIgnored()
	{
		MultiTrackRecorder recorder;
		recorder.processInput(nullptr, 64);
		recorder.processInputInterleaved(nullptr, 2, 64);
		const float samples[4] = {0.5f, 0.5f, 0.5f, 0.5f};
		recorder.processInputInterleaved(samples, 0, 2);
		recorder.processInputInterleaved(samples, -3, 2);
		for (int i = 0; i < MultiTrackRecorder::NumTracks; ++i)
		{
			QCOMPARE(recorder.track(i).framesPushed(), std::uint64_t{0});
		}
	}

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

	//! A portable startup failure must leave one route retryable without disturbing another.
	void writerStartFailureLeavesRouteRetryable()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		MultiTrackRecorder recorder;
		const QString healthyTake = dir.filePath(QStringLiteral("healthy.wav"));
		const QString retryTake = dir.filePath(QStringLiteral("retry.wav"));
		QVERIFY(recorder.armTrack(0, healthyTake.toStdString(), 48000, 0));

		TrackRecorder::failNextWriterThreadStartForTesting();
		bool failedArmReturned = false;
		bool failureEscaped = false;
		try
		{
			failedArmReturned = recorder.armTrack(1, retryTake.toStdString(), 48000, 1);
		}
		catch (const std::system_error&)
		{
			failureEscaped = true;
		}

		QVERIFY(!recorder.track(1).isArmed());
		QVERIFY(!failureEscaped);
		QVERIFY(!failedArmReturned);
		QVERIFY(recorder.track(0).isArmed());
		QVERIFY(!QFileInfo::exists(recjournal::journalPathFor(retryTake)));

		QVERIFY(recorder.armTrack(1, retryTake.toStdString(), 48000, 1));
		std::vector<SampleFrame> period(32, SampleFrame{0.25f, -0.5f});
		recorder.processInput(period.data(), static_cast<lmms::f_cnt_t>(period.size()));
		QCOMPARE(recorder.track(0).framesPushed(), std::uint64_t{32});
		QCOMPARE(recorder.track(1).framesPushed(), std::uint64_t{32});
		recorder.disarmAll();
		QCOMPARE(recorder.track(0).framesRecorded(), std::uint64_t{32});
		QCOMPARE(recorder.track(1).framesRecorded(), std::uint64_t{32});
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

	//! 0.3.0 (recording crash recovery): arming a capture JOURNALS the take, the
	//! disk-writer keeps the journal's frame count current, and a clean stop
	//! RETIRES the journal. That last part is the whole design: "there is a
	//! journal" and "the capture died" are the same fact, so nothing that stops
	//! cleanly can leave one behind - and if this ever drifted, the next start
	//! would offer every recording ever made as a crash.
	void ArmingJournalsTheTakeAndDisarmingRetiresIt()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString take = dir.filePath("journalled.wav");
		const QString journal = recjournal::journalPathFor(take);
		QVERIFY(!QFileInfo::exists(journal));

		MultiTrackRecorder recorder;
		QVERIFY(recorder.armTrack(0, take.toStdString(), 48000, 0));
		QVERIFY(recorder.track(0).journalPath() == journal.toStdString());

		// The journal exists as soon as the take is armed, at zero frames, and
		// reads back as an INTERRUPTED capture - which is what makes an exit
		// that never reaches disarm() recoverable.
		TakeJournal opened;
		QVERIFY(recjournal::read(journal, &opened));
		QCOMPARE(opened.takePath, take);
		QCOMPARE(opened.state, QStringLiteral("in_progress"));
		QCOMPARE(opened.sampleRate, 48000);
		QCOMPARE(opened.framesOnDisk, std::uint64_t{0});
		QVERIFY(recjournal::isRecoverable(opened));

		// Feed a period and let the disk-writer flush: the journal's count is the
		// frames that actually reached the file, which is the count the recovery
		// offer reports as guaranteed.
		std::vector<SampleFrame> period(512, SampleFrame{0.25f, 0.5f});
		recorder.processInput(period.data(), static_cast<lmms::f_cnt_t>(period.size()));
		QTRY_VERIFY(recorder.track(0).framesRecorded() > 0);

		recorder.disarmAll();
		QVERIFY(!QFile::exists(journal));

		// A clean take therefore leaves NO offer, and the take itself is intact.
		QCOMPARE(recjournal::scan(dir.path()).size(), 0);
		QVERIFY(QFileInfo::exists(take));
		QCOMPARE(recorder.track(0).journalPath(), std::string());
	}

	//! A take path may legally END in a space - so the journal must carry it
	//! through verbatim. Trimming the whole line used to rewrite the path on
	//! the way back in, and recovery then found no take at the recorded place
	//! and offered nothing for a capture that really did happen.
	void JournalRoundTripsATakePathThatEndsInASpace()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString take = dir.filePath("spaced take ");
		// isRecoverable() asks the filesystem whether the take is there, so the
		// take itself must exist for this to be about the journal's own parsing.
		QFile takeFile(take);
		QVERIFY(takeFile.open(QIODevice::WriteOnly));
		takeFile.close();

		TakeJournal written;
		written.state = QStringLiteral("in_progress");
		written.takePath = take;
		written.sampleRate = 48000;
		written.channels = 2;
		const QString journal = recjournal::journalPathFor(take);
		QVERIFY(recjournal::write(journal, written));

		TakeJournal opened;
		QVERIFY(recjournal::read(journal, &opened));
		QCOMPARE(opened.takePath, take);
		// the whole point: recovery must find the take it journalled
		QVERIFY(recjournal::isRecoverable(opened));
		QCOMPARE(recjournal::scan(dir.path()).size(), 1);
	}

	//! A line break in a valid POSIX filename must survive journal recovery.
	//! The journal is a `key=value` file read one line at a time, so a raw
	//! newline inside the path split the value across two lines: the reader
	//! looked for a take that was never recorded and recovery offered nothing
	//! for a capture that really happened.
	void JournalRecoversTakePathContainingNewline()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString take = dir.filePath(QStringLiteral("take\npart.wav"));
		// isRecoverable() asks the filesystem whether the take is there, so the
		// take itself must exist for this to be about the journal's own format.
		QFile takeFile(take);
		QVERIFY(takeFile.open(QIODevice::WriteOnly));
		takeFile.close();

		TakeJournal written;
		written.state = QStringLiteral("in_progress");
		written.takePath = take;
		written.sampleRate = 48000;
		written.channels = 2;
		const QString journal = recjournal::journalPathFor(take);
		QVERIFY(recjournal::write(journal, written));

		TakeJournal opened;
		QVERIFY(recjournal::read(journal, &opened));
		QCOMPARE(opened.takePath, take);
		QVERIFY(recjournal::isRecoverable(opened));
		const QList<TakeJournal> recovered = recjournal::scan(dir.path());
		QCOMPARE(recovered.size(), 1);
		QCOMPARE(recovered.front().takePath, take);
	}

	//! A journal written by an OLDER build (version 1) holds LITERAL values:
	//! its fields were never escaped, so decoding them would rewrite a path
	//! that merely CONTAINS a backslash. Old journals must still load exactly
	//! as they did.
	void JournalReadsAVersion1FileLiterally()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString take = dir.filePath(QStringLiteral("literal\\n take.wav"));
		QFile takeFile(take);
		QVERIFY(takeFile.open(QIODevice::WriteOnly));
		takeFile.close();

		const QString journal = recjournal::journalPathFor(take);
		QFile file(journal);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
		QTextStream out(&file);
		out << "# zene-studio recording journal - a side file, never part of a project\n";
		out << "version=1\n";
		out << "state=in_progress\n";
		out << "take=" << take << "\n";
		out << "sample_rate=48000\n";
		out.flush();
		file.close();

		TakeJournal opened;
		QVERIFY(recjournal::read(journal, &opened));
		QCOMPARE(opened.takePath, take);
		// the backslash is still a backslash: nothing decoded a version 1 value.
		QVERIFY(opened.takePath.contains(QLatin1Char('\\')));
	}
} ;

QTEST_APPLESS_MAIN(MultiTrackRecorderTest)

#include "MultiTrackRecorderTest.moc"
