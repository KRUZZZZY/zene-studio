/*
 * StemJobLoaderTest.cpp - StemJobManager::submitFrom: a job's mix is made on the worker (Q050-010)
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
#include <memory>
#include <thread>
#include <vector>

#include <QSignalSpy>
#include <QTimer>

#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "StemSeparation/StemJobManager.h"

using namespace lmms;

namespace
{

std::shared_ptr<const SampleBuffer> makeMix()
{
	return std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(256, SampleFrame(0.5f, -0.5f)), StemModelSampleRate);
}

//! How many times any FakeSeparator was asked to separate: a cancelled loader must leave it at zero.
std::atomic<int> s_separations{0};

class FakeSeparator : public StemSeparator
{
public:
	Status separate(const SampleBuffer& mix, int sampleRate, int, StemQuality, const ProgressFn& progress, StemSet& out, QString&) override
	{
		++s_separations;
		if (!progress(1.0f)) { return Status::Cancelled; }
		for (int s = 0; s < NumStems; ++s)
		{
			out[static_cast<std::size_t>(s)] = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(mix.size()), sampleRate);
		}
		return Status::Success;
	}
	QString backendName() const override { return QStringLiteral("fake"); }
};

} // namespace

class StemJobLoaderTest : public QObject
{
	Q_OBJECT

private slots:
	/*! Q050-010: submitFrom() returns while the loader is still running ON THE WORKER, and the caller's event loop
	 *  turns meanwhile - so decoding and resampling never run on the caller's thread. */
	void testTheLoaderRunsOnTheWorkerAfterSubmitReturned()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>());
		std::atomic<bool> release{false};
		std::atomic<bool> started{false};
		std::atomic<bool> ranOffTheCaller{false};
		const auto caller = std::this_thread::get_id();
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);
		const auto mix = makeMix();
		int ticks = 0;
		QTimer ticker;
		ticker.setInterval(1);
		connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
		ticker.start();
		const int id = manager.submitFrom([&](const std::atomic<bool>&, QString*) {
			ranOffTheCaller.store(std::this_thread::get_id() != caller);
			started.store(true);
			for (int i = 0; i < 5000 && !release.load(); ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
			return mix;
		}, StemModelSampleRate, 1024);
		QVERIFY(id > 0);
		QTRY_VERIFY_WITH_TIMEOUT(started.load(), 5000);
		QVERIFY(ranOffTheCaller.load());
		QTRY_VERIFY_WITH_TIMEOUT(ticks >= 20, 5000);
		QCOMPARE(finishedSpy.count(), 0);
		release.store(true);
		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1, 10000);
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Completed));
		QCOMPARE(manager.result(id)[0]->size(), mix->size());
	}

	void testALoaderThatFailsFailsTheJobWithItsReason()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>());
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);
		const int id = manager.submitFrom([](const std::atomic<bool>&, QString* why) -> std::shared_ptr<const SampleBuffer> {
			*why = QStringLiteral("could not decode");
			return nullptr;
		}, StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 5000);
		QCOMPARE(manager.error(id), QStringLiteral("could not decode"));
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Failed));
	}

	void testCancelDuringTheLoaderCancelsTheJobWithoutSeparating()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<FakeSeparator>());
		s_separations.store(0);
		std::atomic<bool> inLoader{false};
		QSignalSpy cancelledSpy(&manager, &StemJobManager::jobCancelled);
		QSignalSpy progressSpy(&manager, &StemJobManager::jobProgress);
		const auto mix = makeMix();
		const int id = manager.submitFrom([&](const std::atomic<bool>& cancel, QString*) {
			inLoader.store(true);
			while (!cancel.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
			return mix;  // a loader that still hands a mix back: a cancelled job must not separate it
		}, StemModelSampleRate, 1024);
		QTRY_VERIFY_WITH_TIMEOUT(inLoader.load(), 5000);
		manager.cancel(id);
		QTRY_VERIFY_WITH_TIMEOUT(cancelledSpy.count() == 1, 5000);
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Cancelled));
		QCOMPARE(progressSpy.count(), 0);
		QCOMPARE(s_separations.load(), 0);
		QVERIFY(manager.result(id)[0] == nullptr);
	}

	void testAnEmptyLoaderIsRefused()
	{
		StemJobManager manager;
		QCOMPARE(manager.submitFrom(StemJobManager::MixLoader(), StemModelSampleRate, 1024), -1);
	}

};

QTEST_GUILESS_MAIN(StemJobLoaderTest)
#include "StemJobLoaderTest.moc"
