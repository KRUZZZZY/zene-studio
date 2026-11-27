/*
 * AudioAlsaCaptureErrorTest.cpp - the ALSA capture thread does not log
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

#include "AudioAlsa.h"
#include "AudioEngine.h"
#include "AudioInputPath.h"
#include "ConfigManager.h"
#include "Engine.h"

#include <QtTest>

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

using namespace lmms;

namespace
{

//! One printed line, with the thread that printed it.
struct PrintedLine
{
	QString text;
	std::thread::id thread;
};

std::mutex& printMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::vector<PrintedLine>& printedLines()
{
	static std::vector<PrintedLine> lines;
	return lines;
}

//! The thread the capture loop runs on: the fake read records it.
std::thread::id captureThread;

//! Posted by the fake read once the capture thread has run the recovery and
//! comes back for its next period.
QSemaphore* errorPathReached = nullptr;
//! The capture thread parks here until the test lets it go on (or until the
//! capture is closed, which drops the PCM).
QSemaphore* releaseCapture = nullptr;
//! How many reads the fake served.
std::atomic<int> readiCalls{0};
//! Set by the fake read that injects the overrun: the recovery's prepare
//! then fails, which is the failure the capture thread used to log.
std::atomic<bool> prepareShouldFail{false};

//! cluster new-p12-321: how many of the NEXT hardware-/software-parameter
//! allocations must fail with -ENOMEM. A test arms one of them immediately
//! before the call it wants to see fail, so the injection is deterministic -
//! no memory pressure and no clock involved.
std::atomic<int> hwParamsMallocFailures{0};
std::atomic<int> swParamsMallocFailures{0};

bool takeArmedFailure(std::atomic<int>& counter)
{
	if (counter.load(std::memory_order_acquire) <= 0) { return false; }
	counter.fetch_sub(1, std::memory_order_acq_rel);
	return true;
}

bool printedByCaptureThread(const QString& needle)
{
	std::lock_guard<std::mutex> guard(printMutex());
	for (const PrintedLine& line : printedLines())
	{
		if (line.thread == captureThread && line.text.contains(needle)) { return true; }
	}
	return false;
}

bool printedByAnotherThread(const QString& needle)
{
	std::lock_guard<std::mutex> guard(printMutex());
	for (const PrintedLine& line : printedLines())
	{
		if (line.thread != captureThread && line.text.contains(needle)) { return true; }
	}
	return false;
}

QString printedText(const QString& needle)
{
	QString found;
	std::lock_guard<std::mutex> guard(printMutex());
	for (const PrintedLine& line : printedLines())
	{
		if (line.text.contains(needle)) { found += line.text; }
	}
	return found;
}

} // namespace

//! Every printf() of the program is recorded (with the thread that printed
//! it) and passed on, so the test can tell WHERE a message came from: the
//! capture thread may not print one, another thread may.
extern "C" int printf(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	char buffer[2048];
	const int written = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	const int length = written < 0 ? 0 : (written < int(sizeof(buffer)) ? written : int(sizeof(buffer)) - 1);
	{
		std::lock_guard<std::mutex> guard(printMutex());
		printedLines().push_back({QString::fromUtf8(buffer, length), std::this_thread::get_id()});
	}
	fwrite(buffer, 1, size_t(length), stdout);
	return length;
}

extern "C" snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t*, void*, snd_pcm_uframes_t)
{
	const int call = ++readiCalls;
	// A capture opened by another test of this file arms neither of the two
	// hand-over points: its read is simply satisfied, so such a test can start
	// and stop a capture without the machinery below.
	if (errorPathReached == nullptr || releaseCapture == nullptr) { return 0; }
	if (call == 1)
	{
		captureThread = std::this_thread::get_id();
		// The overrun: the capture thread was too late. The recovery it
		// runs now cannot prepare the device, so the failure has to be
		// reported WITHOUT the capture thread logging it.
		prepareShouldFail.store(true, std::memory_order_release);
		return -EPIPE;
	}
	if (call == 2)
	{
		// The recovery has run by now (it runs between the two reads), so
		// this is the hand-over point the test waits for.
		errorPathReached->release();
	}
	releaseCapture->acquire();
	return 0;
}

extern "C" int snd_pcm_prepare(snd_pcm_t*)
{
	if (prepareShouldFail.exchange(false, std::memory_order_acq_rel))
	{
		return -EINVAL;
	}
	return 0;
}

extern "C" int snd_pcm_drop(snd_pcm_t*)
{
	// closeCapture() drops the PCM before it joins the capture thread: that
	// is what unblocks the parked read. A capture opened by another test of
	// this file has no parked read, and nothing to release.
	if (releaseCapture != nullptr) { releaseCapture->release(); }
	return 0;
}

//! cluster new-p12-321: the parameter allocations, with a fault the test can
//! arm. alsa-lib's own snd_pcm_hw_params_malloc() IS
//! `calloc(1, snd_pcm_hw_params_sizeof())`, so the success path stays
//! behaviour-identical and the failure path is exactly what a failed
//! allocation returns: -ENOMEM with the pointer untouched (NULL, since the
//! caller starts from NULL).
extern "C" int snd_pcm_hw_params_malloc(snd_pcm_hw_params_t** ptr)
{
	if (takeArmedFailure(hwParamsMallocFailures))
	{
		*ptr = nullptr;
		return -ENOMEM;
	}
	*ptr = static_cast<snd_pcm_hw_params_t*>(calloc(1, snd_pcm_hw_params_sizeof()));
	return *ptr == nullptr ? -ENOMEM : 0;
}

extern "C" int snd_pcm_sw_params_malloc(snd_pcm_sw_params_t** ptr)
{
	if (takeArmedFailure(swParamsMallocFailures))
	{
		*ptr = nullptr;
		return -ENOMEM;
	}
	*ptr = static_cast<snd_pcm_sw_params_t*>(calloc(1, snd_pcm_sw_params_sizeof()));
	return *ptr == nullptr ? -ENOMEM : 0;
}

class AudioAlsaCaptureErrorTest : public QObject
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

	void captureThreadDoesNotLogTheFailedRecovery()
	{
		QSemaphore reached;
		QSemaphore release;
		errorPathReached = &reached;
		releaseCapture = &release;
		readiCalls.store(0, std::memory_order_release);
		prepareShouldFail.store(false, std::memory_order_release);
		{
			std::lock_guard<std::mutex> guard(printMutex());
			printedLines().clear();
		}

		bool successful = false;
		AudioAlsa device(successful, Engine::audioEngine());
		QVERIFY(successful);
		device.startProcessing();
		const bool captureOpen = AudioInputPath::live().open;

		// The capture thread has run the recovery (and failed to recover)
		// by the time it asks for its next period.
		const bool reachedRecovery = reached.tryAcquire(1, 5000);
		const bool captureThreadLogged = printedByCaptureThread("Can't recover capture");

		// Let the capture thread go on, then close the capture: the parked
		// failure is reported by the thread that closes it, which is not an
		// audio thread.
		release.release();
		device.stopProcessing();
		const bool reportedElsewhere = printedByAnotherThread("Can't recover capture from overrun");

		errorPathReached = nullptr;
		releaseCapture = nullptr;

		QVERIFY2(captureOpen, "the ALSA capture device did not open on the null device");
		QVERIFY2(reachedRecovery, "the capture thread never reached the failed recovery");
		QVERIFY2(!captureThreadLogged,
			qPrintable("the capture thread logged the failed recovery: "
				+ printedText("Can't recover capture")));
		QVERIFY2(reportedElsewhere,
			qPrintable("the parked capture failure was never reported: "
				+ printedText("Can't recover capture")));
	}

	//! cluster new-p12-321: a capture HARDWARE-parameter allocation that fails
	//! must not hand the NULL pointer it leaves behind to the
	//! snd_pcm_hw_params_*() setup calls, which dereference it. The capture is
	//! published unavailable instead, and a later start still opens it.
	void captureHwParamsAllocationFailureIsPublished()
	{
		resetInjection();
		bool successful = false;
		AudioAlsa device(successful, Engine::audioEngine());
		QVERIFY(successful);

		// The constructor's own allocations are behind us: the next hardware
		// allocation is the capture one.
		hwParamsMallocFailures.store(1, std::memory_order_release);
		device.startProcessing();

		const AudioInputPath::Live failed = AudioInputPath::live();
		device.stopProcessing();

		QVERIFY2(hwParamsMallocFailures.load(std::memory_order_acquire) == 0,
			"the capture never allocated its hardware parameters");
		QVERIFY2(!failed.open,
			"the capture opened although its hardware parameters could not be allocated");
		QVERIFY2(failed.captureCapable,
			"the backend stopped reporting its capture path when the allocation failed");
		QVERIFY2(failed.reason.contains("capture hardware parameters"),
			qPrintable("the allocation failure was not published: " + failed.reason));

		// Exhaustion and reconfiguration: the failure left nothing behind, so a
		// fault-free start opens the capture on the same device.
		device.startProcessing();
		const AudioInputPath::Live recovered = AudioInputPath::live();
		device.stopProcessing();
		QVERIFY2(recovered.open,
			qPrintable("the capture did not open after a failed allocation: " + recovered.reason));
	}

	//! cluster new-p12-321: the capture SOFTWARE-parameter allocation, the
	//! second site the finding names. Same contract: check the result, publish
	//! the capture unavailable, never call snd_pcm_sw_params_current() with a
	//! NULL parameter block.
	void captureSwParamsAllocationFailureIsPublished()
	{
		resetInjection();
		bool successful = false;
		AudioAlsa device(successful, Engine::audioEngine());
		QVERIFY(successful);

		// Everything up to the software parameters must run for real, so the
		// failure is the software allocation and nothing earlier.
		swParamsMallocFailures.store(1, std::memory_order_release);
		device.startProcessing();

		const AudioInputPath::Live failed = AudioInputPath::live();
		device.stopProcessing();

		QVERIFY2(swParamsMallocFailures.load(std::memory_order_acquire) == 0,
			"the capture never allocated its software parameters");
		QVERIFY2(!failed.open,
			"the capture opened although its software parameters could not be allocated");
		QVERIFY2(failed.reason.contains("capture software parameters"),
			qPrintable("the allocation failure was not published: " + failed.reason));
	}

private:
	//! Each of the tests below starts from the same, unarmed state: no fault
	//! armed, no parked read and no counter carried over from an earlier slot.
	static void resetInjection()
	{
		hwParamsMallocFailures.store(0, std::memory_order_release);
		swParamsMallocFailures.store(0, std::memory_order_release);
		readiCalls.store(0, std::memory_order_release);
		prepareShouldFail.store(false, std::memory_order_release);
		errorPathReached = nullptr;
		releaseCapture = nullptr;
	}
};

QTEST_GUILESS_MAIN(AudioAlsaCaptureErrorTest)

#include "AudioAlsaCaptureErrorTest.moc"
