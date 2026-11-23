/*
 * StemJobManagerThrowTest.cpp - cluster new-p14-325: a throwing backend must fail its job, not the DAW
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

// A stem backend and the model provisioner are native code, and both run on the
// manager's detached worker thread. An exception that escapes that thread's entry
// function calls std::terminate, so a corrupt model or an allocation error in the
// backend took the whole application down instead of failing one job. These
// regressions own that behaviour on their own target: StemJobManagerTest.cpp was
// already at the file-length ratchet's 500 code lines and cannot carry them.
//
// The doubles here are local on purpose: these cases are about what a THROW does,
// not about the state machine the other suite covers, and sharing a fake would
// couple that suite's file length to this one.

#include <QtTest>

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

#include <QSignalSpy>

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

//! A backend that throws instead of returning a status.
class ThrowingSeparator : public StemSeparator
{
public:
	//! ThrowOnFirstCall lets the next job prove the worker is still alive.
	enum class Mode
	{
		ThrowEveryCall,
		ThrowOnFirstCall
	};

	explicit ThrowingSeparator(Mode mode) : m_mode(mode) {}

	Status separate(const SampleBuffer& mix,
		int sampleRate,
		int segmentFrames,
		StemQuality quality,
		const ProgressFn& progress,
		StemSet& out,
		QString& error) override
	{
		Q_UNUSED(segmentFrames);
		Q_UNUSED(quality);
		Q_UNUSED(error);
		const int call = ++m_calls;
		if (m_mode == Mode::ThrowEveryCall || call == 1)
		{
			throw std::runtime_error("fake throw");
		}
		if (!progress(0.5f) || !progress(1.0f))
		{
			return Status::Cancelled;
		}
		for (int s = 0; s < NumStems; ++s)
		{
			std::vector<SampleFrame> frames(mix.size());
			for (auto& frame : frames)
			{
				frame = SampleFrame(0.25f, -0.25f);
			}
			out[static_cast<std::size_t>(s)] =
				std::make_shared<const SampleBuffer>(std::move(frames), sampleRate);
		}
		return Status::Success;
	}

	QString backendName() const override { return QStringLiteral("throwing"); }

private:
	Mode m_mode;
	std::atomic<int> m_calls{0};
};

} // namespace

class StemJobManagerThrowTest : public QObject
{
	Q_OBJECT
private slots:
	/*! A backend that throws fails its job with the exception's own text: the
	 *  job is Failed, it is not Completed, and no other terminal signal fires. */
	void testAThrowingBackendFailsTheJobInsteadOfTheProcess()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<ThrowingSeparator>(ThrowingSeparator::Mode::ThrowEveryCall));
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);

		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 10000);
		QCOMPARE(failedSpy.last().at(0).toInt(), id);
		QCOMPARE(failedSpy.last().at(1).toString(), QStringLiteral("fake throw"));
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Failed));
		QCOMPARE(manager.error(id), QStringLiteral("fake throw"));
		QCOMPARE(finishedSpy.count(), 0);
		QCOMPARE(manager.runningJobCount(), 0);
		QVERIFY(manager.result(id)[0] == nullptr);
	}

	/*! The provisioner runs on the same worker inside the same job, so its throw
	 *  is a failed job too - and a job that has ended is not still downloading. */
	void testAThrowingProvisionerFailsTheJob()
	{
		StemJobManager manager;
		// The separator throws on every call, so a run that reached it instead of
		// the provisioner fails this test rather than passing by accident.
		manager.setSeparator(std::make_unique<ThrowingSeparator>(ThrowingSeparator::Mode::ThrowEveryCall));
		manager.setModelProvisioner([](const std::atomic<bool>&, const std::function<void(float)>&,
			QString*) -> bool { throw std::runtime_error("provisioner exploded"); });
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);

		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 10000);
		QCOMPARE(failedSpy.last().at(1).toString(), QStringLiteral("provisioner exploded"));
		QCOMPARE(manager.error(id), QStringLiteral("provisioner exploded"));
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Failed));
		QVERIFY2(!manager.fetchingModel(id), "a failed job still reported a model download");
		QVERIFY(manager.result(id)[0] == nullptr); // separation never began
	}

	/*! The throw ends the JOB, not the worker: the job queued behind it still
	 *  runs, so one bad model cannot silently stall every later split. */
	void testTheWorkerKeepsDrainingAfterAThrowingJob()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<ThrowingSeparator>(ThrowingSeparator::Mode::ThrowOnFirstCall));
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);

		const int first = manager.submit(makeMix(), StemModelSampleRate, 1024);
		const int second = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1, 10000);
		QCOMPARE(failedSpy.count(), 1);
		QCOMPARE(failedSpy.last().at(0).toInt(), first);
		QCOMPARE(static_cast<int>(manager.state(first)), static_cast<int>(StemJobManager::State::Failed));
		QCOMPARE(static_cast<int>(manager.state(second)), static_cast<int>(StemJobManager::State::Completed));
		QVERIFY(manager.result(second)[0] != nullptr);
		QCOMPARE(manager.runningJobCount(), 0);
	}

	/*! A throw that is not a std::exception carries no what(): the job still
	 *  fails with a message of the manager's own, not with an empty error. */
	void testANonStandardThrowStillFailsTheJobWithAMessage()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<ThrowingSeparator>(ThrowingSeparator::Mode::ThrowEveryCall));
		manager.setModelProvisioner([](const std::atomic<bool>&, const std::function<void(float)>&,
			QString*) -> bool { throw 42; });
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);

		const int id = manager.submit(makeMix(), StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 10000);
		QVERIFY2(!manager.error(id).isEmpty(), "a non-standard throw left the job with no error");
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Failed));
	}
};

QTEST_GUILESS_MAIN(StemJobManagerThrowTest)
#include "StemJobManagerThrowTest.moc"
