/*
 * AudioAlsaSuspendShutdownTest.cpp - an ALSA capture suspend recovery observes the stop flag
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

/*! cluster 5490: the -ESTRPIPE recovery used to retry snd_pcm_resume() while it returned
 *  -EAGAIN, sleeping a second between attempts, with no bound and without looking at the
 *  capture stop flag. closeCapture() sets that flag and then JOINS the capture thread, so a
 *  device that stays suspended hung the shutdown for as long as resume kept saying -EAGAIN.
 *
 *  The fakes below stand in for the capture PCM only: the device opens on the real ALSA
 *  "null" PCM, the first read reports -ESTRPIPE, and resume() answers -EAGAIN. The seam is
 *  the drop() call: closeCapture() sets the stop flag and drops the PCM before it joins, so
 *  counting resume() calls AFTER the drop measures exactly whether the recovery looked at the
 *  flag. The value is a call count, not a duration, so the test has no timing assertion; the
 *  fake stops answering -EAGAIN after three post-drop calls so a loop that ignores the flag
 *  still ends, and the test fails on the count instead of hanging. */

#include <QtTest>

#include <atomic>
#include <cerrno>

#include "AudioAlsa.h"
#include "AudioEngine.h"
#include "AudioInputPath.h"
#include "ConfigManager.h"
#include "Engine.h"

using namespace lmms;

namespace
{

//! How many resume() calls the fake serves once the capture is closing. The recovery must
//! not make more than one (it may be inside the call it already started); the extra ones
//! are what the buggy loop did, and a terminal error ends it so the test cannot hang.
constexpr int kPostDropRetries = 3;

std::atomic<bool> captureClosing{false};
std::atomic<int> resumeCallsBeforeClose{0};
std::atomic<int> resumeCallsWhileClosing{0};
std::atomic<int> readiCalls{0};
//! Released by the first resume(): the capture thread is in the suspend recovery.
QSemaphore* firstResume = nullptr;
//! Held by every read after the first, so the capture thread waits instead of spinning.
QSemaphore* readGate = nullptr;

} // namespace

//! The capture device reports the suspend (-ESTRPIPE) on its first read; later reads park
//! until the PCM is dropped, the way a silent device's read would.
extern "C" snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t*, void*, snd_pcm_uframes_t)
{
	if (++readiCalls == 1)
	{
		return -ESTRPIPE;
	}
	if (readGate != nullptr)
	{
		readGate->acquire();
	}
	return 0;
}

//! The suspended device never finishes resuming: every attempt answers -EAGAIN. Calls made
//! after closeCapture() began are counted separately - those are the ones the fix forbids.
extern "C" int snd_pcm_resume(snd_pcm_t*)
{
	if (captureClosing.load(std::memory_order_acquire))
	{
		const int call = ++resumeCallsWhileClosing;
		return call > kPostDropRetries ? -EIO : -EAGAIN;
	}
	const int call = ++resumeCallsBeforeClose;
	if (call == 1 && firstResume != nullptr)
	{
		firstResume->release();
	}
	return -EAGAIN;
}

//! closeCapture() sets the stop flag and then drops the PCM before joining the thread:
//! from here on the recovery must stop. The drop also unblocks a parked read.
extern "C" int snd_pcm_drop(snd_pcm_t*)
{
	captureClosing.store(true, std::memory_order_release);
	if (readGate != nullptr)
	{
		readGate->release();
	}
	return 0;
}

//! Both the open and a recovery prepare succeed; the defect is the unbounded retry, not a
//! failed prepare.
extern "C" int snd_pcm_prepare(snd_pcm_t*)
{
	return 0;
}

class AudioAlsaSuspendShutdownTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		ConfigManager::inst()->setValue(QStringLiteral("audioalsa"), QStringLiteral("device"), QStringLiteral("null"));
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void suspendRecoveryStopsWhenTheCaptureCloses()
	{
		captureClosing.store(false, std::memory_order_release);
		resumeCallsBeforeClose.store(0, std::memory_order_release);
		resumeCallsWhileClosing.store(0, std::memory_order_release);
		readiCalls.store(0, std::memory_order_release);
		QSemaphore resumeSeen;
		QSemaphore gate;
		firstResume = &resumeSeen;
		readGate = &gate;

		bool successful = false;
		AudioAlsa device(successful, Engine::audioEngine());
		QVERIFY2(successful, "the ALSA null PCM did not open");
		device.startProcessing();
		const bool captureOpen = AudioInputPath::live().open;

		// The capture thread is inside the suspend recovery (its first resume() is
		// in flight), so closing the capture now is the situation the bug hangs in.
		const bool reachedRecovery = resumeSeen.tryAcquire(1, 5000);
		device.stopProcessing();
		const AudioDevice& deviceStatus = device;
		const bool playbackStopped = !deviceStatus.isRunning();
		const int retriesWhileClosing = resumeCallsWhileClosing.load();
		const int retriesBeforeClose = resumeCallsBeforeClose.load();

		firstResume = nullptr;
		readGate = nullptr;

		QVERIFY2(captureOpen, "the ALSA capture device did not open on the null device");
		QVERIFY2(reachedRecovery, "the capture thread never entered the -ESTRPIPE recovery");
		QVERIFY2(retriesBeforeClose >= 1, "the suspend recovery never retried snd_pcm_resume()");
		QVERIFY2(playbackStopped, "the device was still running after stopProcessing()");
		QVERIFY2(retriesWhileClosing <= 1,
			qPrintable(QStringLiteral("the suspend recovery retried snd_pcm_resume() %1 times "
				"after the capture started closing; it must stop at the first check (a "
				"retry that ignores m_captureStop hangs closeCapture()'s join)")
				.arg(retriesWhileClosing)));
	}
};

QTEST_GUILESS_MAIN(AudioAlsaSuspendShutdownTest)

#include "AudioAlsaSuspendShutdownTest.moc"
