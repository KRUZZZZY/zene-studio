/*
 * AudioAlsaFatalWriteTest.cpp - ALSA playback stops on a write error its recovery cannot handle
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 *
 *
 */

/*! cluster new-p17-220: an ALSA playback write error that handleError() cannot recover
 *  from (a removed device: snd_pcm_writei keeps returning -ENODEV) must stop the run loop.
 *  The fakes below stand in for the playback PCM write only, so the loop, the real
 *  recoveries and the real device lifecycle all run. The test is deterministic: the
 *  first write returns the fatal error, the second write (the buggy retry) is counted,
 *  and the report is read only after the device has stopped - no sample clock is raced. */

#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <cerrno>

#include "lmmsconfig.h"

#include "AudioAlsa.h"
#include "AudioDevice.h"
#include "AudioDeviceDiagnostics.h"
#include "AudioDummy.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"

using namespace lmms;

namespace
{

//! Playback period writes served by the fake PCM.
std::atomic<int> writeiCalls{0};
//! Released by the first write, so the test does not race the thread's start.
QSemaphore* firstWrite = nullptr;
//! The loop's single period buffer: the fake records the frames it is asked for.
std::atomic<snd_pcm_uframes_t> lastRequest{0};

} // namespace

//! Every playback write fails the way a removed device does. A recovered underrun
//! (-EPIPE) never reaches here twice for one period, so a second call is the loop
//! retrying an error it already failed to recover from.
extern "C" snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t*, const void*, snd_pcm_uframes_t frames)
{
	lastRequest.store(frames, std::memory_order_relaxed);
	const int call = ++writeiCalls;
	if (call == 1 && firstWrite != nullptr) { firstWrite->release(); }
	return -ENODEV;
}

class AudioAlsaFatalWriteTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// The engine's own device must not consume the fake writes: it stays on the Dummy.
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("audiodev"), AudioDummy::name());
		ConfigManager::inst()->setValue(QStringLiteral("audioalsa"), QStringLiteral("device"), QStringLiteral("null"));
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void fatalWriteErrorStopsPlaybackAndIsPublished()
	{
		writeiCalls.store(0, std::memory_order_release);
		lastRequest.store(0, std::memory_order_release);
		QSemaphore first;
		firstWrite = &first;

		bool successful = false;
		auto* device = new AudioAlsa(successful, Engine::audioEngine());
		QVERIFY2(successful, "the ALSA null PCM did not open");
		AudioEngine* engine = Engine::audioEngine();
		engine->storeAudioDevice();
		engine->setAudioDevice(device, false);

		device->startProcessing();
		const bool wrote = first.tryAcquire(1, 5000);
		// The first write returned -ENODEV, which handleError() cannot recover from.
		// A loop that ignores that return renders and writes again; the fixed loop
		// clears its running flag and returns. The flag is AudioDevice's, not QThread's.
		const AudioDevice* deviceStatus = device;
		const bool stopped = QTest::qWaitFor([deviceStatus] { return !deviceStatus->isRunning(); }, 5000);
		const int calls = writeiCalls.load();
		const snd_pcm_uframes_t request = lastRequest.load();

		// The failure is published on the device status the diagnostics and the CPU
		// diagnostics window read, not only printed or silently dropped. It is read
		// before the device is restored, which replaces it in the engine.
		const QJsonObject status = audioDeviceReport(engine);

		// Stop the device even when the loop kept going, so a failing run does not
		// leave a spinning thread behind for cleanupTestCase to wait on.
		engine->restoreAudioDevice();
		firstWrite = nullptr;

		QVERIFY2(wrote, "the playback loop never wrote a period");
		QVERIFY2(stopped, "the loop kept running after a write error it could not recover from");
		QCOMPARE(calls, 1);
		QVERIFY2(request > 0, "the first write carried no frames");

		QVERIFY2(!status.value(QStringLiteral("running")).toBool(),
			"the stopped device is still reported as running");
		const QJsonObject failure = status.value(QStringLiteral("failure")).toObject();
		QVERIFY2(failure.value(QStringLiteral("available")).toBool(),
			qPrintable(QStringLiteral("the fatal write error was not published: %1")
				.arg(QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact)))));
		QCOMPARE(failure.value(QStringLiteral("value")).toInt(), static_cast<int>(ENODEV));
		QCOMPARE(failure.value(QStringLiteral("unit")).toString(), QStringLiteral("errno"));
		QVERIFY2(failure.value(QStringLiteral("message")).toString().contains(QStringLiteral("No such device")),
			qPrintable(failure.value(QStringLiteral("message")).toString()));
	}
};

QTEST_GUILESS_MAIN(AudioAlsaFatalWriteTest)

#include "AudioAlsaFatalWriteTest.moc"
