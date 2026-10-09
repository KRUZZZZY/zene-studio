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
#include <thread>

#include "SessionTuning.h"

using namespace lmms;

namespace
{

std::atomic<bool> s_readerStarted{false};
std::atomic<bool> s_readerReturned{false};
std::atomic<bool> s_readerReturnedInWindow{false};
std::atomic<float> s_readerFrequency{0.0f};
std::thread s_reader;

//! Runs while commit() is paused between dropping the claim and publishing the new front slot.
void holdWriterAtPublicationBoundary()
{
	s_readerStarted.store(false, std::memory_order_relaxed);
	s_readerReturned.store(false, std::memory_order_relaxed);
	s_reader = std::thread([] {
		s_readerStarted.store(true, std::memory_order_release);
		s_readerFrequency.store(SessionTuning::instance()->noteToFreq(60), std::memory_order_relaxed);
		s_readerReturned.store(true, std::memory_order_release);
	});

	const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!s_readerStarted.load(std::memory_order_acquire)
		&& std::chrono::steady_clock::now() < startDeadline)
	{
		std::this_thread::yield();
	}
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

	//! The writer pause sits between the claim release and front-slot publish. A reader must return the old
	//! table during that pause; with the old publish-then-clear order it sees the claimed front slot and stalls.
	void readerReturnsWhileWriterIsPausedBetweenReleaseAndPublish()
	{
		SessionTuning* tuning = SessionTuning::instance();
		tuning->reset();
		const float oldFrequency = tuning->noteToFreq(60);
		s_readerReturnedInWindow.store(false, std::memory_order_relaxed);
		SessionTuning::setPublicationBoundaryHookForTest(&holdWriterAtPublicationBoundary);

		QString error;
		const bool committed = tuning->setNoteFrequency(60, 450.0, QStringLiteral("publication test"), &error);
		SessionTuning::setPublicationBoundaryHookForTest(nullptr);
		if (s_reader.joinable()) { s_reader.join(); }

		QVERIFY2(committed, qPrintable(error));
		QVERIFY2(s_readerReturnedInWindow.load(std::memory_order_relaxed),
			"reader did not return while the writer was paused at the claim-release/publication boundary");
		QCOMPARE(s_readerFrequency.load(std::memory_order_relaxed), oldFrequency);
		QCOMPARE(tuning->noteToFreq(60), 450.0f);
	}
};

QTEST_GUILESS_MAIN(SessionTuningPublicationTest)
#include "SessionTuningPublicationTest.moc"
