/*
 * StemSplitPipelineTest.cpp - end-to-end: mix -> 4 stems -> 4 SampleTracks
 *
 * Copyright (c) 2026 LMMS Developers
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
 */

#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>

#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "TrackContainer.h"

#include "StemSeparation/ExternalProcessStemSeparator.h"
#include "StemSeparation/StemJobManager.h"
#include "StemSeparation/StemTrackBuilder.h"

using namespace lmms;

namespace
{

// The committed fixture is a tiny linear 4-output ONNX graph (458 bytes), NOT
// HTDemucs. It exists so the whole pipeline can be exercised without the
// optional 166 MB model download.
QString stubModelPath()
{
	return QStringLiteral(LMMS_TEST_DATA_DIR) + QStringLiteral("/stub-4stem-linear.onnx");
}

QString cliPath()
{
	return QStringLiteral(LMMS_STEM_CLI_DEFAULT);
}

std::shared_ptr<const SampleBuffer> makeSineMix(int seconds)
{
	const int sampleRate = StemModelSampleRate;
	const int frames = sampleRate * seconds;
	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));
	for (int i = 0; i < frames; ++i)
	{
		const auto t = static_cast<float>(i) / static_cast<float>(sampleRate);
		data[static_cast<std::size_t>(i)] = SampleFrame(
			0.4f * std::sin(2.0f * float(M_PI) * 220.0f * t),
			0.4f * std::sin(2.0f * float(M_PI) * 330.0f * t));
	}
	return std::make_shared<const SampleBuffer>(std::move(data), sampleRate);
}

} // namespace

class StemSplitPipelineTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init(true);

		qputenv("LMMS_STEM_MODEL", QFile::encodeName(stubModelPath()));
		qputenv("LMMS_STEM_CLI", QFile::encodeName(cliPath()));
		qputenv("LMMS_STEM_PYTHON", QByteArrayLiteral("/usr/bin/python3"));

		QString error;
		if (!ExternalProcessStemSeparator::isAvailable(&error))
		{
			QSKIP(qPrintable(QStringLiteral("external-process backend unavailable: ") + error));
		}
		qInfo("backend: %s", qPrintable(ExternalProcessStemSeparator().backendName()));
		qInfo("python:  %s", qPrintable(ExternalProcessStemSeparator::locatePython()));
		qInfo("cli:     %s", qPrintable(ExternalProcessStemSeparator::locateCli()));
		qInfo("model:   %s", qPrintable(ExternalProcessStemSeparator::defaultModelPath()));
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void testSeparatorProducesFourStemsWithProgress()
	{
		ExternalProcessStemSeparator separator;
		const auto mix = makeSineMix(1);

		StemSet stems;
		QString error;
		float lastProgress = -1.0f;
		int progressCalls = 0;
		bool nonMonotonic = false;

		QElapsedTimer timer;
		timer.start();
		const auto status = separator.separate(*mix, StemModelSampleRate, 16384,
			[&](float fraction) {
				if (fraction < lastProgress)
				{
					nonMonotonic = true;
				}
				lastProgress = fraction;
				++progressCalls;
				return true;
			},
			stems, error);
		const auto elapsedMs = timer.elapsed();

		qInfo("separator wall time: %lld ms for %zu frames (%d s at %d Hz)",
			static_cast<long long>(elapsedMs), mix->size(), 1, StemModelSampleRate);
		QVERIFY2(status == StemSeparator::Status::Success, qPrintable(error));
		QVERIFY(progressCalls >= 2);
		QVERIFY2(!nonMonotonic, "progress fractions must be monotonically non-decreasing");
		QCOMPARE(lastProgress, 1.0f);

		for (int s = 0; s < NumStems; ++s)
		{
			QVERIFY2(stems[static_cast<std::size_t>(s)] != nullptr, stemName(static_cast<Stem>(s)));
			QCOMPARE(stems[static_cast<std::size_t>(s)]->size(), mix->size());
		}
	}

	void testSeparatorCanBeCancelledMidRun()
	{
		ExternalProcessStemSeparator separator;
		const auto mix = makeSineMix(1);

		// Slow the reference CLI down so the cancel file is observed reliably
		// between chunks (test hook, see ExternalProcessStemSeparator.cpp).
		qputenv("LMMS_STEM_CHUNK_DELAY_MS", QByteArrayLiteral("150"));
		StemSet stems;
		QString error;
		int progressCalls = 0;
		const auto status = separator.separate(*mix, StemModelSampleRate, 16384,
			[&](float) {
				// cancel after the first chunk
				return ++progressCalls < 2;
			},
			stems, error);
		qunsetenv("LMMS_STEM_CHUNK_DELAY_MS");

		QCOMPARE(static_cast<int>(status),
			static_cast<int>(StemSeparator::Status::Cancelled));
		QVERIFY(progressCalls < 10);
	}

	void testJobManagerRunsTheRealBackend()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<ExternalProcessStemSeparator>());
		QSignalSpy progressSpy(&manager, &StemJobManager::jobProgress);
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);

		const auto mix = makeSineMix(1);
		QElapsedTimer timer;
		timer.start();
		const int id = manager.submit(mix, StemModelSampleRate, 16384);
		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() + failedSpy.count() >= 1, 180000);
		const auto elapsedMs = timer.elapsed();
		qInfo("job wall time: %lld ms", static_cast<long long>(elapsedMs));

		QCOMPARE(failedSpy.count(), 0);
		QCOMPARE(static_cast<int>(manager.state(id)),
			static_cast<int>(StemJobManager::State::Completed));
		QVERIFY(progressSpy.count() >= 2);

		const auto stems = manager.result(id);
		for (int s = 0; s < NumStems; ++s)
		{
			QVERIFY(stems[static_cast<std::size_t>(s)] != nullptr);
			QCOMPARE(stems[static_cast<std::size_t>(s)]->size(), mix->size());
		}

		// The stub model's four gains (0.4, 0.3, 0.2, 0.1) sum to 1.0, so the
		// overlap-add output must reconstruct the input sample-for-sample.
		float maxError = 0.0f;
		for (std::size_t i = 0; i < mix->size(); i += 97)
		{
			float sumLeft = 0.0f;
			float sumRight = 0.0f;
			for (int s = 0; s < NumStems; ++s)
			{
				sumLeft += stems[static_cast<std::size_t>(s)]->data()[i].left();
				sumRight += stems[static_cast<std::size_t>(s)]->data()[i].right();
			}
			maxError = std::max(maxError, std::abs(sumLeft - mix->data()[i].left()));
			maxError = std::max(maxError, std::abs(sumRight - mix->data()[i].right()));
		}
		qInfo("max |sum(stems) - mix| = %.3e", static_cast<double>(maxError));
		QVERIFY2(maxError < 1.0e-4f,
			qPrintable(QStringLiteral("stems do not reconstruct the mix, max error %1")
				.arg(static_cast<double>(maxError))));
	}

	void testStemTracksAreCreatedInTheSong()
	{
		StemSet stems;
		QString error;
		const auto mix = makeSineMix(1);
		ExternalProcessStemSeparator separator;
		const auto status = separator.separate(*mix, StemModelSampleRate, 16384,
			[](float) { return true; }, stems, error);
		QVERIFY2(status == StemSeparator::Status::Success, qPrintable(error));

		auto* song = Engine::getSong();
		const auto before = song->tracks().size();
		const auto tracks = StemTrackBuilder::createStemTracks(
			song, stems, TimePos(0), QStringLiteral("mix"), &error);

		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(tracks.size(), 4);
		QCOMPARE(song->tracks().size(), before + 4);

		for (int s = 0; s < NumStems; ++s)
		{
			auto* track = tracks.at(s);
			QVERIFY(track != nullptr);
			QCOMPARE(static_cast<int>(track->type()),
				static_cast<int>(Track::Type::Sample));
			QVERIFY2(track->name().contains(
					QString::fromLatin1(stemName(static_cast<Stem>(s)))),
				qPrintable(track->name()));
			QCOMPARE(static_cast<int>(track->getClips().size()), 1);
			QVERIFY(song->tracks().end() !=
				std::find(song->tracks().begin(), song->tracks().end(), track));
		}
	}

	void testStemTrackBuilderRejectsAnIncompleteSet()
	{
		auto* song = Engine::getSong();
		const auto before = song->tracks().size();
		StemSet partial;
		QString error;
		const auto tracks = StemTrackBuilder::createStemTracks(
			song, partial, TimePos(0), QStringLiteral("mix"), &error);
		QVERIFY(tracks.isEmpty());
		QVERIFY(!error.isEmpty());
		// all-or-nothing: nothing may be left behind in the project
		QCOMPARE(song->tracks().size(), before);
	}
};

QTEST_GUILESS_MAIN(StemSplitPipelineTest)
#include "StemSplitPipelineTest.moc"
