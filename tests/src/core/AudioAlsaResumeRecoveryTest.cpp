/*
 * AudioAlsaResumeRecoveryTest.cpp - the bounded, stop-aware ALSA suspend retry policy
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

/*! cluster 5490: the ALSA suspend recovery used to retry snd_pcm_resume() while it said
 *  -EAGAIN, with no bound and without checking whether the device was being closed, so a
 *  device that stayed suspended hung the shutdown. resumePcmWithBudget() is the policy the
 *  capture and playback recoveries now share; the fake resume and wait make every path -
 *  a device that resumes, a caller that stops, an exhausted budget, a terminal error -
 *  deterministic, with no clock and no sleep. The capture-side wiring is covered by
 *  AudioAlsaSuspendShutdownTest, which drives the real capture thread. */

#include <QtTest>

#include <cerrno>
#include <vector>

#include "AudioAlsaRecovery.h"

using namespace lmms;

namespace
{

//! A snd_pcm_resume() stand-in answering a fixed script; the last code repeats forever.
struct FakePcm
{
	std::vector<int> resumeResults;
	int resumeCalls = 0;
	int waits = 0;
	int stopAfterResumes = -1;  //!< -1: never stop; otherwise stop before this many calls

	int resume()
	{
		const auto index = static_cast<std::size_t>(resumeCalls) < resumeResults.size()
			? static_cast<std::size_t>(resumeCalls)
			: resumeResults.size() - 1;
		++resumeCalls;
		return resumeResults[index];
	}

	bool stopRequested() const
	{
		return stopAfterResumes >= 0 && resumeCalls >= stopAfterResumes;
	}
};

} // namespace

class AudioAlsaResumeRecoveryTest : public QObject
{
	Q_OBJECT

private slots:
	void retriesUntilTheDeviceResumes()
	{
		FakePcm pcm{{-EAGAIN, -EAGAIN, 0}};
		const auto outcome = alsa::detail::resumePcmWithBudget(
			[&pcm]() { return pcm.resume(); },
			[&pcm]() { ++pcm.waits; },
			[&pcm]() { return pcm.stopRequested(); },
			5);
		QCOMPARE(outcome.error, 0);
		QVERIFY(!outcome.stopped);
		QCOMPARE(pcm.resumeCalls, 3);
		QCOMPARE(pcm.waits, 2);
	}

	void stopsWhenTheCallerIsStopping()
	{
		FakePcm pcm{{-EAGAIN}};
		pcm.stopAfterResumes = 2;
		const auto outcome = alsa::detail::resumePcmWithBudget(
			[&pcm]() { return pcm.resume(); },
			[&pcm]() { ++pcm.waits; },
			[&pcm]() { return pcm.stopRequested(); },
			100);
		QVERIFY2(outcome.stopped, "the retry ignored the caller's stop request");
		QCOMPARE(pcm.resumeCalls, 2);
		// Both attempts were made before the stop was visible; the third is refused.
		QCOMPARE(pcm.waits, 2);
	}

	void checksTheStopBeforeTheFirstAttempt()
	{
		FakePcm pcm{{-EAGAIN}};
		pcm.stopAfterResumes = 0;
		const auto outcome = alsa::detail::resumePcmWithBudget(
			[&pcm]() { return pcm.resume(); },
			[&pcm]() { ++pcm.waits; },
			[&pcm]() { return pcm.stopRequested(); },
			100);
		QVERIFY(outcome.stopped);
		QCOMPARE(pcm.resumeCalls, 0);
		QCOMPARE(pcm.waits, 0);
	}

	void givesUpAfterTheBudget()
	{
		FakePcm pcm{{-EAGAIN}};
		const auto outcome = alsa::detail::resumePcmWithBudget(
			[&pcm]() { return pcm.resume(); },
			[&pcm]() { ++pcm.waits; },
			[&pcm]() { return pcm.stopRequested(); },
			4);
		// The budget is a bound, not a promise: -EAGAIN here means "prepare instead".
		QCOMPARE(outcome.error, -EAGAIN);
		QVERIFY(!outcome.stopped);
		QCOMPARE(pcm.resumeCalls, 4);
		// No sleep after the last attempt: the caller's fallback is not delayed.
		QCOMPARE(pcm.waits, 3);
	}

	void reportsATerminalErrorWithoutWaiting()
	{
		FakePcm pcm{{-EIO}};
		const auto outcome = alsa::detail::resumePcmWithBudget(
			[&pcm]() { return pcm.resume(); },
			[&pcm]() { ++pcm.waits; },
			[&pcm]() { return pcm.stopRequested(); },
			10);
		QCOMPARE(outcome.error, -EIO);
		QVERIFY(!outcome.stopped);
		QCOMPARE(pcm.resumeCalls, 1);
		QCOMPARE(pcm.waits, 0);
	}
};

QTEST_GUILESS_MAIN(AudioAlsaResumeRecoveryTest)

#include "AudioAlsaResumeRecoveryTest.moc"
