/*
 * StemJobManagerTest.cpp - job-manager state machine, progress and cancel
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
 */

#include <QtTest>

#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <cmath>

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
		SlowSuccess,
		//! Reports a non-finite fraction, which std::clamp passes through.
		NonFiniteProgress
	};

	FakeSeparator(Mode mode, int steps) :
		m_mode(mode),
		m_steps(steps)
	{
	}

	//! Q050-062.1: where separate() records the quality the manager handed it.
	void recordQualityInto(std::atomic<int>* sink) { m_qualitySink = sink; }

	Status separate(const SampleBuffer& mix,
		int sampleRate,
		int segmentFrames,
		StemQuality quality,
		const ProgressFn& progress,
		StemSet& out,
		QString& error) override
	{
		Q_UNUSED(segmentFrames);
		if (m_qualitySink != nullptr) { m_qualitySink->store(static_cast<int>(quality)); }
		if (m_mode == Mode::Fail)
		{
			error = QStringLiteral("fake failure");
			return Status::Failed;
		}

		for (int step = 1; step <= m_steps; ++step)
		{
			if (m_mode == Mode::SlowSuccess || m_mode == Mode::NonFiniteProgress)
			{
				// NonFiniteProgress sleeps too: the value has to be read while the
				// job is running, since the success branch overwrites it with 1.0.
				QThread::msleep(40);
			}
			if (!progress(reportedFraction(step)))
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
	//! The fraction this mode reports at \a step: the usual step/steps, or a
	//! non-finite one, as a backend dividing by a zero count would report.
	float reportedFraction(int step) const
	{
		if (m_mode == Mode::NonFiniteProgress)
		{
			if (step == 1) { return std::numeric_limits<float>::infinity(); }
			return step == 2 ? 0.5f : std::numeric_limits<float>::quiet_NaN();
		}
		return static_cast<float>(step) / static_cast<float>(m_steps);
	}

	Mode m_mode;
	int m_steps;
	std::atomic<int>* m_qualitySink = nullptr;
};

// Test double for a backend stuck inside one inference call: it blocks until
// released, then reports progress, so a cancel can only take effect after it.
class BlockingInferenceSeparator : public StemSeparator
{
public:
	BlockingInferenceSeparator(std::atomic<bool>& entered, std::atomic<bool>& release,
		std::atomic<bool>& destroyed) :
		m_entered(entered),
		m_release(release),
		m_destroyed(destroyed)
	{
	}

	~BlockingInferenceSeparator() override { m_destroyed.store(true); }

	Status separate(const SampleBuffer&, int, int, StemQuality, const ProgressFn& progress,
		StemSet&, QString&) override
	{
		m_entered.store(true);
		while (!m_release.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
		return progress(0.5f) ? Status::Success : Status::Cancelled;
	}

	QString backendName() const override { return QStringLiteral("blocking-inference"); }

private:
	std::atomic<bool>& m_entered;
	std::atomic<bool>& m_release;
	std::atomic<bool>& m_destroyed;
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

	//! Q050-062.1: a job reaches its backend at the quality it was submitted at,
	//! and a caller that names none gets the legacy Standard.
	void testSubmitHandsTheQualityToTheBackend()
	{
		const struct { bool given; StemQuality quality; StemQuality expected; } cases[] = {
			{false, StemQuality::Fast, StemQuality::Standard},
			{true, StemQuality::Fast, StemQuality::Fast},
			{true, StemQuality::Standard, StemQuality::Standard},
			{true, StemQuality::High, StemQuality::High},
		};
		for (const auto& c : cases)
		{
			std::atomic<int> seen{-1};
			StemJobManager manager;
			auto separator = std::make_unique<FakeSeparator>(FakeSeparator::Mode::Success, 2);
			separator->recordQualityInto(&seen);
			manager.setSeparator(std::move(separator));
			QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);
			const int id = c.given ? manager.submit(makeMix(), StemModelSampleRate, 1024, c.quality)
				: manager.submit(makeMix(), StemModelSampleRate, 1024);
			QVERIFY(id > 0);
			QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1, 10000);
			QCOMPARE(seen.load(), static_cast<int>(c.expected));
		}
	}

	//! Q050-062.1: the shared hop formula. Standard is the legacy segment / 2 for
	//! every segment (odd ones too), the others overlap 25% / 75%, and no hop is 0.
	void testQualityHopFormula()
	{
		for (const int segment : {2, 3, 7, 1024, 16384, HTDemucsSegmentFrames, 44100 * 600})
		{
			QCOMPARE(stemHopFrames(segment, StemQuality::Standard), segment / 2);
			QVERIFY(stemHopFrames(segment, StemQuality::High) >= 1);
			QVERIFY(stemHopFrames(segment, StemQuality::High) <= stemHopFrames(segment, StemQuality::Standard));
			QVERIFY(stemHopFrames(segment, StemQuality::Fast) >= stemHopFrames(segment, StemQuality::Standard));
		}
		QCOMPARE(stemHopFrames(16384, StemQuality::Fast), 12288);
		QCOMPARE(stemHopFrames(16384, StemQuality::High), 4096);
		QCOMPARE(stemHopFrames(HTDemucsSegmentFrames, StemQuality::Fast), 257985);
		QCOMPARE(stemHopFrames(HTDemucsSegmentFrames, StemQuality::High), 85995);
		QCOMPARE(stemOverlapFraction(StemQuality::Fast), 0.25);
		QCOMPARE(stemOverlapFraction(StemQuality::Standard), 0.5);
		QCOMPARE(stemOverlapFraction(StemQuality::High), 0.75);
		QCOMPARE(QString::fromLatin1(stemQualityName(DefaultStemQuality)), QStringLiteral("standard"));
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
		auto queuedMix = makeMix();
		const std::weak_ptr<const SampleBuffer> queuedMixRef = queuedMix;
		const int second = manager.submit(std::move(queuedMix), StemModelSampleRate, 1024);
		QCOMPARE(static_cast<int>(manager.state(second)),
			static_cast<int>(StemJobManager::State::Queued));
		QVERIFY(!queuedMixRef.expired());

		manager.cancel(second);
		QCOMPARE(static_cast<int>(manager.state(second)),
			static_cast<int>(StemJobManager::State::Cancelled));
		QCOMPARE(cancelledSpy.count(), 1);
		QVERIFY(queuedMixRef.expired());

		// the first job is unaffected and still completes
		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1, 10000);
		QCOMPARE(static_cast<int>(manager.state(first)),
			static_cast<int>(StemJobManager::State::Completed));
	}

	void testDequeueThenCancelEmitsExactlyOnce()
	{
		for (int iteration = 0; iteration < 40; ++iteration)
		{
			StemJobManager manager;
			manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::SlowSuccess, 8));
			QSignalSpy startedSpy(&manager, &StemJobManager::jobStarted);
			QSignalSpy cancelledSpy(&manager, &StemJobManager::jobCancelled);
			const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
			QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() == 1, 10000);
			manager.cancel(id);
			QTRY_VERIFY_WITH_TIMEOUT(cancelledSpy.count() == 1, 10000);
			QTest::qWait(20);
			QCOMPARE(cancelledSpy.count(), 1);
			QCOMPARE(static_cast<int>(manager.state(id)),
				static_cast<int>(StemJobManager::State::Cancelled));
		}
	}

	void testTerminalPayloadIsPublishedAsOneSnapshot()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::Success, 12));
		const int id = manager.submit(makeMix(4096), StemModelSampleRate, 1024);
		std::atomic<bool> stop{false};
		std::atomic<bool> torn{false};
		std::thread reader([&]
		{
			while (!stop.load(std::memory_order_relaxed))
			{
				const auto stems = manager.result(id);
				int present = 0;
				for (const auto& stem : stems) { present += stem != nullptr ? 1 : 0; }
				if (present != 0 && present != NumStems) { torn.store(true); break; }
			}
		});
		QTRY_VERIFY_WITH_TIMEOUT(static_cast<int>(manager.state(id))
			== static_cast<int>(StemJobManager::State::Completed), 10000);
		stop.store(true, std::memory_order_relaxed);
		reader.join();
		QVERIFY(!torn.load());
		const auto stems = manager.result(id);
		for (const auto& stem : stems) { QVERIFY(stem != nullptr); }
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

	/*! Owner decision 14: the first job fetches its model through the
	 *  provisioner, on the WORKER (never the caller's thread), before the
	 *  separator runs, and reports that it is doing so. */
	void testTheProvisionerRunsOnTheWorkerBeforeSeparation()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::Success, 4));
		std::atomic<bool> release{false};
		std::atomic<bool> ranOffTheCaller{false};
		const auto caller = std::this_thread::get_id();
		manager.setModelProvisioner([&](const std::atomic<bool>&, const std::function<void(float)>& progress,
			QString*)
		{
			ranOffTheCaller.store(std::this_thread::get_id() != caller);
			progress(0.5f);
			while (!release.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
			return true;
		});
		// Released on every way out of this slot: a failed comparison returns early, and a fake
		// provisioner still spinning would keep the worker - and ~StemJobManager, which joins it -
		// alive until QtTest's 300 s watchdog (hosted linux-arm64, run 36789477810).
		struct Release
		{
			std::atomic<bool>& flag;
			~Release() { flag.store(true); }
		} releaseOnExit{release};
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);
		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);

		QTRY_VERIFY_WITH_TIMEOUT(manager.fetchingModel(id), 5000);
		// The job raises fetchingModel BEFORE the provisioner reports its first fraction, so the
		// progress is waited for, not read the instant the flag is up (that read raced: 0 on arm64).
		QTRY_COMPARE_WITH_TIMEOUT(manager.downloadProgress(id), 0.5f, 5000);
		QCOMPARE(manager.progress(id), 0.0f); // separation has not begun
		release.store(true);
		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1, 10000);
		QVERIFY(!manager.fetchingModel(id));
		QVERIFY(ranOffTheCaller.load());
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Completed));
	}

	void testAFailedFetchFailsTheJobWithItsReason()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::Success, 4));
		manager.setModelProvisioner([](const std::atomic<bool>&, const std::function<void(float)>&,
			QString* error)
		{
			*error = QStringLiteral("Download failed: host unreachable");
			return false;
		});
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);
		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 10000);
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Failed));
		QCOMPARE(manager.error(id), QStringLiteral("Download failed: host unreachable"));
		QVERIFY(manager.result(id)[0] == nullptr); // the separator never ran
	}

	//! A cancel during the fetch reaches the provisioner's flag and ends the job
	//! cancelled - the transfer is not something a user has to wait out.
	void testCancelDuringTheFetchCancelsTheJob()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::Success, 4));
		manager.setModelProvisioner([](const std::atomic<bool>& cancel, const std::function<void(float)>&,
			QString* error)
		{
			while (!cancel.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
			*error = QStringLiteral("Download failed: cancelled");
			return false;
		});
		QSignalSpy cancelledSpy(&manager, &StemJobManager::jobCancelled);
		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(manager.fetchingModel(id), 5000);
		manager.cancel(id);
		QTRY_VERIFY_WITH_TIMEOUT(cancelledSpy.count() == 1, 10000);
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Cancelled));
	}

	/*! Luna cluster 3576: a backend that reports a non-finite fraction must not
	 *  put one into the job's progress. std::clamp(NaN, 0, 1) is NaN, so the
	 *  clamp does not make the value a fraction; every reported value must be a
	 *  real number inside [0, 1]. */
	void testANonFiniteFractionIsNeverPublished()
	{
		StemJobManager manager;
		manager.setSeparator(
			std::make_unique<FakeSeparator>(FakeSeparator::Mode::NonFiniteProgress, 8));
		QSignalSpy progressSpy(&manager, &StemJobManager::jobProgress);

		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		// Sampled while the job runs: the success branch overwrites progress with
		// 1.0, so a read after the fact cannot see what the backend published.
		// Waited for rather than read at once: submit() returns before the worker starts.
		QTRY_VERIFY_WITH_TIMEOUT(manager.state(id) == StemJobManager::State::Running, 5000);
		bool sawRunning = false;
		while (manager.state(id) == StemJobManager::State::Running)
		{
			sawRunning = true;
			const float fraction = manager.progress(id);
			QVERIFY2(std::isfinite(fraction), "a non-finite progress value was stored");
			QVERIFY2(fraction >= 0.0f && fraction <= 1.0f, "progress outside [0, 1]");
			QTest::qWait(5);
		}
		QVERIFY(sawRunning);

		for (const auto& emission : progressSpy)
		{
			const float fraction = emission.at(1).toFloat();
			QVERIFY2(std::isfinite(fraction), "a non-finite progress value was published");
			QVERIFY2(fraction >= 0.0f && fraction <= 1.0f, "progress outside [0, 1]");
		}
		QTRY_VERIFY_WITH_TIMEOUT(
			static_cast<int>(manager.state(id)) == static_cast<int>(StemJobManager::State::Completed), 10000);
	}

	//! The same rule for the model-download fraction, which is a second callback into the same field.
	void testANonFiniteDownloadFractionIsNeverPublished()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::Success, 4));
		std::atomic<bool> release{false};
		std::atomic<bool> reported{false};
		manager.setModelProvisioner([&](const std::atomic<bool>&, const std::function<void(float)>& progress,
			QString*)
		{
			// NaN is reported LAST and then the fake spins, so the stored value is
			// NaN for as long as the test reads it.
			progress(0.25f);
			progress(std::numeric_limits<float>::quiet_NaN());
			reported.store(true);
			while (!release.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
			return true;
		});
		struct Release
		{
			std::atomic<bool>& flag;
			~Release() { flag.store(true); }
		} releaseOnExit{release};

		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);
		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(reported.load(), 5000);
		const float downloaded = manager.downloadProgress(id);
		QVERIFY2(std::isfinite(downloaded), "a non-finite download progress was stored");
		QVERIFY2(downloaded >= 0.0f && downloaded <= 1.0f, "download progress outside [0, 1]");
		release.store(true);
		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1, 10000);
	}

	/*! Luna cluster 1758: a backend whose final update is 1.0 must not make
	 *  listeners see 100% twice - the success branch publishes its own. */
	void testOneHundredPercentIsPublishedExactlyOnce()
	{
		StemJobManager manager;
		// The fake's last step is step/steps == 1.0, i.e. it reports completion itself.
		manager.setSeparator(std::make_unique<FakeSeparator>(FakeSeparator::Mode::Success, 4));
		QSignalSpy progressSpy(&manager, &StemJobManager::jobProgress);
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);

		manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1, 10000);

		int hundred = 0;
		for (const auto& emission : progressSpy)
		{
			if (emission.at(1).toFloat() == 1.0f) { ++hundred; }
		}
		QCOMPARE(hundred, 1);
		QCOMPARE(progressSpy.last().at(1).toFloat(), 1.0f);
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

	void testDestructionDoesNotWaitForAnInferenceCall()
	{
		std::atomic<bool> entered{false};
		std::atomic<bool> release{false};
		std::atomic<bool> backendDestroyed{false};
		auto manager = std::make_unique<StemJobManager>();
		manager->setSeparator(std::make_unique<BlockingInferenceSeparator>(entered, release, backendDestroyed));
		manager->submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(entered.load(), 5000);

		std::atomic<bool> destructionStarted{false};
		std::thread releaser([&]
		{
			while (!destructionStarted.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			release.store(true);
		});
		destructionStarted.store(true);
		manager.reset();
		const bool returnedBeforeRelease = !release.load();
		releaser.join();
		while (!backendDestroyed.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
		QVERIFY(returnedBeforeRelease);
	}
};

QTEST_GUILESS_MAIN(StemJobManagerTest)
#include "StemJobManagerTest.moc"
