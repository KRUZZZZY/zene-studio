/*
 * RecordingCrashLossTest.cpp - Q050-045: a real crash loses well under a second of
 *                              recorded audio, and capture stays allocation-free
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

// The test binary doubles as the crashing process. `RecordingCrashLossTest --crash-child
// <take> <report> <blocks> <extra-ms>` arms a TrackRecorder, pushes <blocks> blocks of 512
// frames at real-time pace (what the audio callback would deliver), records the exact count
// the ring accepted in <report>, sleeps <extra-ms> and kills itself with SIGKILL: no
// destructor, no disarm, no flush. The parent then runs the recovery path (journal scan and
// recoverableFrames) on what the dead process left and compares it with that exact count.

#include <QtTest>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <sndfile.h>

#include "RecordingJournal.h"
#include "TrackRecorder.h"

#include "AllocationProbe.h"

namespace recjournal = lmms::recordingjournal;

namespace
{

constexpr int Rate = 48000;
constexpr int BlockFrames = 512;

#ifndef _WIN32 // only the POSIX crash cases push blocks
void pushBlock(lmms::TrackRecorder& recorder, std::vector<float>& block, std::size_t index)
{
	for (std::size_t i = 0; i < block.size(); ++i)
	{
		block[i] = 0.5f * static_cast<float>((index * block.size() + i) % 100) / 100.f;
	}
	recorder.processInputInterleaved(block.data(), 1, static_cast<lmms::f_cnt_t>(block.size()));
}
#endif

#ifndef _WIN32
//! The crashing process: never returns.
int runCrashChild(const char* take, const char* report, int blocks, int extraMs)
{
	lmms::TrackRecorder recorder;
	if (!recorder.arm(take, Rate, 0)) { return 2; }
	std::vector<float> block(BlockFrames, 0.f);
	const auto period = std::chrono::nanoseconds(1000000000LL * BlockFrames / Rate);
	auto next = std::chrono::steady_clock::now();
	for (int b = 0; b < blocks; ++b)
	{
		pushBlock(recorder, block, static_cast<std::size_t>(b));
		next += period;
		std::this_thread::sleep_until(next);
	}
	// Exact, because the audio side is single-threaded here: this is what the ring accepted.
	pushBlock(recorder, block, static_cast<std::size_t>(blocks));
	const std::uint64_t captured = recorder.framesPushed();
	{
		std::ofstream out(report, std::ios::trunc);
		out << captured << ' ' << recorder.framesRecorded() << ' ' << recorder.commitCount()
			<< ' ' << recorder.commitNanoseconds() << ' ' << recorder.overflowCount() << '\n';
	}
	if (extraMs > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(extraMs)); }
	raise(SIGKILL);
	return 3; // unreachable
}
#endif

} // namespace

class RecordingCrashLossTest : public QObject
{
	Q_OBJECT

private slots:
	//! A child process records and is SIGKILLed at controlled points: right after the push
	//! (the writer has had no time), mid-interval, and after a short grace. Each time the
	//! recovered take must hold all but < 1 s of what was captured - and in fact all but
	//! the stated bound.
	void sigkillAtControlledPointsLosesLessThanTheBound_data()
	{
		QTest::addColumn<int>("blocks");
		QTest::addColumn<int>("extraMs");
		QTest::newRow("immediately after the first block") << 0 << 0;
		QTest::newRow("a few blocks in") << 5 << 0;
		QTest::newRow("just under one commit interval") << 8 << 0;
		QTest::newRow("just over one commit interval") << 11 << 0;
		QTest::newRow("0.5 s in") << 47 << 0;
		QTest::newRow("0.5 s in, 2 ms grace") << 47 << 2;
		QTest::newRow("1 s in") << 94 << 0;
		QTest::newRow("1 s in, 30 ms grace") << 94 << 30;
		QTest::newRow("2.1 s in") << 197 << 0;
		QTest::newRow("2.1 s in, 150 ms grace") << 197 << 150;
	}

	void sigkillAtControlledPointsLosesLessThanTheBound()
	{
#ifdef _WIN32
		QSKIP("SIGKILL of a child process is POSIX-only");
#else
		QFETCH(int, blocks);
		QFETCH(int, extraMs);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString take = dir.filePath("crash.wav");
		const QString report = dir.filePath("report.txt");

		QProcess child;
		child.start(QCoreApplication::applicationFilePath(),
			{QStringLiteral("--crash-child"), take, report, QString::number(blocks),
				QString::number(extraMs)});
		QVERIFY(child.waitForFinished(30000));
		QCOMPARE(child.exitStatus(), QProcess::CrashExit); // killed, not exited
		QVERIFY2(!QFile::exists(report) || QFileInfo(report).size() > 0, "child report empty");

		std::ifstream in(report.toStdString());
		std::uint64_t captured = 0, recordedAtReport = 0, commits = 0, nanos = 0, overflow = 1;
		in >> captured >> recordedAtReport >> commits >> nanos >> overflow;
		QVERIFY2(captured >= static_cast<std::uint64_t>(BlockFrames), "the child captured nothing");
		QCOMPARE(overflow, std::uint64_t{0});

		// THE RECOVERY PATH, exactly as File > Recover Recordings runs it.
		const auto found = recjournal::scan(dir.path());
		QCOMPARE(found.size(), 1);
		const std::uint64_t recovered = recjournal::recoverableFrames(found.first());
		QVERIFY(recovered <= captured);
		const std::uint64_t lost = captured - recovered;

		qInfo().noquote() << QStringLiteral("CRASHLOSS blocks=%1 grace=%2ms captured=%3 recovered=%4 "
			"lost=%5 frames (%6 s) bound=%7 frames; commits_before_report=%8 commit_ms=%9")
			.arg(blocks).arg(extraMs).arg(captured).arg(recovered).arg(lost)
			.arg(static_cast<double>(lost) / Rate, 0, 'f', 4)
			.arg(recjournal::lossBoundFrames(Rate)).arg(commits).arg(nanos / 1.0e6, 0, 'f', 2);

		QVERIFY2(lost < static_cast<std::uint64_t>(Rate),
			qPrintable(QStringLiteral("lost %1 frames, a second or more").arg(lost)));
		QVERIFY2(lost <= recjournal::lossBoundFrames(Rate),
			qPrintable(QStringLiteral("lost %1 frames, above the stated bound %2")
				.arg(lost).arg(recjournal::lossBoundFrames(Rate))));

		// The recovered take must really hold those frames: open it with libsndfile, as
		// the recovery import does.
		SF_INFO info{};
		SNDFILE* sf = sf_open(take.toStdString().c_str(), SFM_READ, &info);
		QVERIFY2(sf != nullptr, "the crashed take does not open");
		QVERIFY2(static_cast<std::uint64_t>(info.frames) >= recovered,
			qPrintable(QStringLiteral("file holds %1 frames, recoverable claims %2")
				.arg(info.frames).arg(recovered)));
		std::vector<float> samples(static_cast<std::size_t>(recovered), 0.f);
		QCOMPARE(static_cast<std::uint64_t>(sf_readf_float(sf, samples.data(),
			static_cast<sf_count_t>(recovered))), recovered);
		sf_close(sf);
		// First frames carry the pattern the child wrote, so this is audio, not zero fill.
		if (recovered > 1) { QVERIFY(samples[1] > 0.f); }
#endif
	}

	//! The recovery report states the bound, in frames and seconds at the project's rate.
	void theReportStatesTheMeasuredBound()
	{
		QCOMPARE(recjournal::updateIntervalFrames(48000), std::uint64_t{4800});
		QCOMPARE(recjournal::lossBoundFrames(48000), std::uint64_t{4800 + 4096});
		const QString text = recjournal::boundText(48000);
		QVERIFY2(text.contains(QStringLiteral("at most 8896 frames (0.185 s at 48000 Hz)")),
			qPrintable(text));
		QVERIFY(recjournal::lossBoundFrames(48000) < 48000u);
		QVERIFY(recjournal::lossBoundFrames(44100) < 44100u);
	}

	//! The audio thread pushes into the ring while the writer commits on its own thread;
	//! the audio side makes no allocation at all (thread-local counter: the writer's
	//! commits, which do allocate, are on another thread and are not counted).
	void captureStaysAllocationFreeWhileTheWriterCommits()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		lmms::TrackRecorder recorder;
		QVERIFY(recorder.arm(dir.filePath("alloc.wav").toStdString(), Rate, 0));
		std::vector<float> block(BlockFrames, 0.25f);

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int b = 0; b < 120; ++b) // ~1.3 s of audio: about a dozen commits
		{
			recorder.processInputInterleaved(block.data(), 1, BlockFrames);
			std::this_thread::sleep_for(std::chrono::microseconds(10667));
		}
		lmms::test::tlCountAllocations = false;
		const std::uint64_t allocations = lmms::test::tlAllocationCount;

		QTRY_VERIFY(recorder.commitCount() >= 5);
		const std::uint64_t commits = recorder.commitCount();
		const double commitMs = static_cast<double>(recorder.commitNanoseconds()) / 1.0e6;
		recorder.disarm();
		qInfo().noquote() << QStringLiteral("COMMITCOST commits=%1 total=%2 ms mean=%3 ms")
			.arg(commits).arg(commitMs, 0, 'f', 2).arg(commitMs / static_cast<double>(commits), 0, 'f', 3);
		QCOMPARE(allocations, std::uint64_t{0});
	}
};

int main(int argc, char** argv)
{
#ifndef _WIN32
	if (argc == 6 && QByteArray(argv[1]) == "--crash-child")
	{
		return runCrashChild(argv[2], argv[3], QByteArray(argv[4]).toInt(),
			QByteArray(argv[5]).toInt());
	}
#endif
	QCoreApplication app(argc, argv);
	RecordingCrashLossTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "RecordingCrashLossTest.moc"
