/*
 * SessionTuningPublicationTest.cpp - a reader is not stalled during table publication (cluster 5313)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "SessionTuning.h"
#include "AllocationProbe.h"

using namespace lmms;

namespace
{

std::atomic<bool> s_readerStarted{false};
std::atomic<bool> s_readerReturned{false};
std::atomic<bool> s_readerStartedInWindow{false};
std::atomic<bool> s_readerReturnedInWindow{false};
std::atomic<float> s_readerFrequency{0.0f};
std::atomic<bool> s_readerActive{false};
std::atomic<std::uint64_t> s_readerAllocations{0};
std::thread s_reader;

//! Runs while commit() is paused between dropping the claim and publishing the new front slot.
void holdWriterAtPublicationBoundary()
{
	s_readerStarted.store(false, std::memory_order_relaxed);
	s_readerReturned.store(false, std::memory_order_relaxed);
	s_reader = std::thread([] {
		s_readerStarted.store(true, std::memory_order_release);
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		const bool active = SessionTuning::instance()->isActive();
		const float frequency = SessionTuning::instance()->noteToFreq(60);
		test::tlCountAllocations = false;
		s_readerActive.store(active, std::memory_order_relaxed);
		s_readerFrequency.store(frequency, std::memory_order_relaxed);
		s_readerAllocations.store(test::tlAllocationCount, std::memory_order_relaxed);
		s_readerReturned.store(true, std::memory_order_release);
	});

	const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!s_readerStarted.load(std::memory_order_acquire)
		&& std::chrono::steady_clock::now() < startDeadline)
	{
		std::this_thread::yield();
	}
	s_readerStartedInWindow.store(s_readerStarted.load(std::memory_order_acquire), std::memory_order_relaxed);
	const auto returnDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (s_readerStarted.load(std::memory_order_acquire)
		&& !s_readerReturned.load(std::memory_order_acquire)
		&& std::chrono::steady_clock::now() < returnDeadline)
	{
		std::this_thread::yield();
	}
	s_readerReturnedInWindow.store(s_readerReturned.load(std::memory_order_acquire), std::memory_order_relaxed);
}

} // namespace

class SessionTuningPublicationTest : public QObject
{
	Q_OBJECT

private slots:
	void cleanup()
	{
		SessionTuning::setPublicationBoundaryHookForTest(nullptr);
		if (s_reader.joinable()) { s_reader.join(); }
		SessionTuning::instance()->reset();
	}

	//! The seam holds the writer mutex after front publication. The audio reader must return with no
	//! allocation while that lock is held; restoring publish-before-release makes the in-window timeout.
	void readerReturnsWhileWriterIsPausedAfterPublication()
	{
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		s_readerStartedInWindow.store(false, std::memory_order_relaxed);
		s_readerReturnedInWindow.store(false, std::memory_order_relaxed);
		SessionTuning::setPublicationBoundaryHookForTest(&holdWriterAtPublicationBoundary);

		QString error;
		const bool committed = tuning->setNoteFrequency(60, 450.0, QStringLiteral("publication test"), &error);
		SessionTuning::setPublicationBoundaryHookForTest(nullptr);
		if (s_reader.joinable()) { s_reader.join(); }

		QVERIFY2(committed, qPrintable(error));
		QVERIFY2(s_readerStartedInWindow.load(std::memory_order_relaxed),
			"reader thread did not start while the writer was paused after publication");
		QVERIFY2(s_readerReturnedInWindow.load(std::memory_order_relaxed),
			"reader timed out while the writer was paused after publication");
		QVERIFY(s_readerActive.load(std::memory_order_relaxed));
		QCOMPARE(s_readerFrequency.load(std::memory_order_relaxed), 450.0f);
		QCOMPARE(s_readerAllocations.load(std::memory_order_relaxed), std::uint64_t{0});
		QCOMPARE(tuning->noteToFreq(60), 450.0f);
	}

	//! Stress: the writer republishes the table with changing source metadata while a reader loops on the
	//! audio-path accessors. Every read must return a value of the active or inactive table (never a torn
	//! mix), and the reader must keep advancing both during the writes and after them.
	void readersKeepProgressingUnderPublicationStress()
	{
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		std::atomic<bool> stopReader{false};
		std::atomic<std::uint64_t> reads{0};
		std::atomic<std::uint64_t> badReads{0};
		std::thread reader([&] {
			while (!stopReader.load(std::memory_order_acquire))
			{
				const float frequency = tuning->noteToFreq(60);
				if (frequency != 0.0f && frequency != 450.0f) { badReads.fetch_add(1, std::memory_order_relaxed); }
				reads.fetch_add(1, std::memory_order_relaxed);
			}
		});

		const auto waitForReads = [&](std::uint64_t target) {
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (reads.load(std::memory_order_relaxed) < target
				&& std::chrono::steady_clock::now() < deadline)
			{
				std::this_thread::yield();
			}
			return reads.load(std::memory_order_relaxed) >= target;
		};

		const std::uint64_t readsBefore = reads.load(std::memory_order_relaxed);
		QString error;
		for (int i = 0; i < 2000; ++i)
		{
			const QString source = QStringLiteral("stress source %1").arg(i);
			if (!tuning->setNoteFrequency(60, 450.0, source, &error)) { break; }
			tuning->reset();
		}
		const bool readerProgressedDuringWrites = waitForReads(readsBefore + 1000);
		const std::uint64_t readsAfterWrites = reads.load(std::memory_order_relaxed);
		const bool readerProgressedAfterWrites = waitForReads(readsAfterWrites + 1000);
		stopReader.store(true, std::memory_order_release);
		reader.join();

		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY2(readerProgressedDuringWrites, "reader stopped advancing while the writer republished");
		QVERIFY2(readerProgressedAfterWrites, "reader stopped advancing after the writer finished");
		QCOMPARE(badReads.load(std::memory_order_relaxed), std::uint64_t{0});
		tuning->reset();
	}
};

QTEST_GUILESS_MAIN(SessionTuningPublicationTest)
#include "SessionTuningPublicationTest.moc"
