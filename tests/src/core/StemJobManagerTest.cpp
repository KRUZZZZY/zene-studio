/*
 * StemJobManagerTest.cpp - job-manager state machine, progress and cancel
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

#include <memory>
#include <vector>

#include <QSignalSpy>
#include <QThread>

#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "StemSeparation/StemJobManager.h"

using namespace lmms;

namespace
{

std::shared_ptr<const SampleBuffer> makeMix(std::size_t frames = 256, int sampleRate = StemModelSampleRate)
{
	std::vector<SampleFrame> data(frames);
	for (std::size_t i = 0; i < frames; ++i)
	{
		data[i] = SampleFrame(0.5f, -0.5f);
	}
	return std::make_shared<const SampleBuffer>(std::move(data), sampleRate);
}

// Test double: no ONNX, no child process. SlowSuccess sleeps between progress
// steps so the test has a deterministic window in which to cancel.
class FakeSeparator : public StemSeparator
{
public:
	enum class Mode
	{
		Success,
		Fail,
		SlowSuccess
	};

	FakeSeparator(Mode mode, int steps) :
		m_mode(mode),
		m_steps(steps)
	{
	}

	Status separate(const SampleBuffer& mix,
		int sampleRate,
		int segmentFrames,
		const ProgressFn& progress,
		StemSet& out,
		QString& error) override
	{
		Q_UNUSED(segmentFrames);
		if (m_mode == Mode::Fail)
		{
			error = QStringLiteral("fake failure");
			return Status::Failed;
		}

		for (int step = 1; step <= m_steps; ++step)
		{
			if (m_mode == Mode::SlowSuccess)
			{
				QThread::msleep(40);
			}
			if (!progress(static_cast<float>(step) / static_cast<float>(m_steps)))
			{
				return Status::Cancelled;
			}
		}

		for (int s = 0; s < NumStems; ++s)
		{
			std::vector<SampleFrame> frames(mix.size());
			const auto value = 0.25f * static_cast<float>(s + 1);
			for (auto& frame : frames)
			{
				frame = SampleFrame(value, -value);
			}
			out[static_cast<std::size_t>(s)] =
				std::make_shared<const SampleBuffer>(std::move(frames), sampleRate);
		}
		return Status::Success;
	}

	QString backendName() const override { return QStringLiteral("fake"); }

private:
	Mode m_mode;
	int m_steps;
};

} // namespace

class StemJobManagerTest : public QObject
{
	Q_OBJECT
private slots:
	void testSubmitRunsToCompletion()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::Success, 4));
		QCOMPARE(manager.backendName(), QStringLiteral("fake"));

		QSignalSpy startedSpy(&manager, &StemJobManager::jobStarted);
		QSignalSpy progressSpy(&manager, &StemJobManager::jobProgress);
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);

		const auto mix = makeMix();
		const int id = manager.submit(mix, StemModelSampleRate, 1024);
		QVERIFY(id > 0);

		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1, 10000);
		QCOMPARE(startedSpy.count(), 1);
		QCOMPARE(static_cast<int>(manager.state(id)),
			static_cast<int>(StemJobManager::State::Completed));
		QCOMPARE(manager.progress(id), 1.0f);
		QCOMPARE(manager.error(id), QString());
		QCOMPARE(manager.runningJobCount(), 0);

		// progress must be monotonic and end at exactly 1.0
		float last = -1.0f;
		for (const auto& emission : progressSpy)
		{
			const auto fraction = emission.at(1).toFloat();
			QVERIFY(fraction >= last);
			last = fraction;
		}
		QCOMPARE(progressSpy.last().at(1).toFloat(), 1.0f);

		const auto stems = manager.result(id);
		for (int s = 0; s < NumStems; ++s)
		{
			QVERIFY2(stems[static_cast<std::size_t>(s)] != nullptr, stemName(static_cast<Stem>(s)));
			QCOMPARE(stems[static_cast<std::size_t>(s)]->size(), mix->size());
			QCOMPARE(stems[static_cast<std::size_t>(s)]->sampleRate(),
				static_cast<sample_rate_t>(StemModelSampleRate));
		}
		// the fake writes stem s at 0.25*(s+1); the last stem must be 1.0
		QCOMPARE(stems[3]->data()[0].left(), 1.0f);
		QVERIFY(finishedSpy.last().at(1).toDouble() >= 0.0);
	}

	void testProgressIsReportedDuringTheRun()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::SlowSuccess, 10));
		QSignalSpy progressSpy(&manager, &StemJobManager::jobProgress);

		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(progressSpy.count() >= 2, 10000);
		// at least one intermediate progress value below 100%
		bool sawIntermediate = false;
		for (const auto& emission : progressSpy)
		{
			const auto fraction = emission.at(1).toFloat();
			if (fraction > 0.0f && fraction < 1.0f)
			{
				sawIntermediate = true;
			}
		}
		QVERIFY(sawIntermediate);
		QCOMPARE(static_cast<int>(manager.state(id)),
			static_cast<int>(StemJobManager::State::Running));
		manager.cancel(id);
		QTRY_VERIFY_WITH_TIMEOUT(
			static_cast<int>(manager.state(id)) == static_cast<int>(StemJobManager::State::Cancelled), 10000);
	}

	void testCancelRunningJob()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::SlowSuccess, 25));
		QSignalSpy progressSpy(&manager, &StemJobManager::jobProgress);
		QSignalSpy cancelledSpy(&manager, &StemJobManager::jobCancelled);
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);

		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(progressSpy.count() >= 1, 10000);
		manager.cancel(id);
		QTRY_VERIFY_WITH_TIMEOUT(cancelledSpy.count() == 1, 10000);

		QCOMPARE(cancelledSpy.last().at(0).toInt(), id);
		QCOMPARE(finishedSpy.count(), 0);
		QCOMPARE(static_cast<int>(manager.state(id)),
			static_cast<int>(StemJobManager::State::Cancelled));
		// cancellation happened before the fake could run all 25 steps
		QVERIFY(progressSpy.count() < 25);
	}

	void testCancelQueuedJobDoesNotWaitForTheWorker()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::SlowSuccess, 10));
		QSignalSpy startedSpy(&manager, &StemJobManager::jobStarted);
		QSignalSpy cancelledSpy(&manager, &StemJobManager::jobCancelled);
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);

		const int first = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() == 1, 10000);
		const int second = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QCOMPARE(static_cast<int>(manager.state(second)),
			static_cast<int>(StemJobManager::State::Queued));

		manager.cancel(second);
		QCOMPARE(static_cast<int>(manager.state(second)),
			static_cast<int>(StemJobManager::State::Cancelled));
		QCOMPARE(cancelledSpy.count(), 1);

		// the first job is unaffected and still completes
		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1, 10000);
		QCOMPARE(static_cast<int>(manager.state(first)),
			static_cast<int>(StemJobManager::State::Completed));
	}

	void testFailureCarriesTheBackendError()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::Fail, 1));
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);

		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 10000);
		QCOMPARE(static_cast<int>(manager.state(id)),
			static_cast<int>(StemJobManager::State::Failed));
		QCOMPARE(manager.error(id), QStringLiteral("fake failure"));
		QCOMPARE(failedSpy.last().at(1).toString(), QStringLiteral("fake failure"));
	}

	void testMissingBackendFailsInsteadOfCrashing()
	{
		StemJobManager manager;
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);
		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 10000);
		QCOMPARE(static_cast<int>(manager.state(id)),
			static_cast<int>(StemJobManager::State::Failed));
		QVERIFY(!manager.error(id).isEmpty());
	}

	void testNullMixIsRejected()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::Success, 1));
		QCOMPARE(manager.submit(nullptr, StemModelSampleRate, 1024), -1);
	}

	void testUnknownJobQueriesAreSafe()
	{
		StemJobManager manager;
		QCOMPARE(static_cast<int>(manager.state(12345)),
			static_cast<int>(StemJobManager::State::Queued));
		QCOMPARE(manager.progress(12345), 0.0f);
		QVERIFY(manager.error(12345).isEmpty());
		QVERIFY(manager.result(12345)[0] == nullptr);
		manager.cancel(12345); // must not crash
	}
};

QTEST_GUILESS_MAIN(StemJobManagerTest)
#include "StemJobManagerTest.moc"
