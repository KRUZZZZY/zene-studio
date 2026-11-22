/*
 * AudioDeviceDiagnosticsTest.cpp - mixer.cpu_load's audio_device: negotiated timing, xruns, unavailable readings
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
 *
 *
 */

/*! Q050-074.2: what mixer.cpu_load reports about the audio device, on three backends.
 *  - No device (the headless Dummy): CPU and late-period history are still there, and every device
 *    reading is available:false with no value - not a zero.
 *  - ALSA on the "null" PCM (no hardware): the rate, period and buffer the PCM granted, the playback
 *    buffer as output latency, no input latency (no capture opened), and a playback underrun counted as
 *    a device xrun while the engine's late-period count does not move.
 *  - ALSA with a capture PCM negotiating ITS OWN rate (48 kHz against playback's, or 96 kHz when playback
 *    already runs at 48): input_sample_rate is the capture's, apart from sample_rate, and input latency
 *    milliseconds are its frames at the capture rate. The null PCM grants whatever it is asked; the test
 *    redirects the capture's rate request (snd_pcm_hw_params_set_rate_near, which only the capture path
 *    calls) by symbol interposition - the AudioAlsaPollDescriptorTest technique - and forwards to ALSA.
 *  - JACK against a private `jackd -d dummy` at 48 kHz / 256 frames (skipped when jackd is absent): the
 *    server's rate and period, both port latencies, no buffer figure; a render made late on purpose
 *    shows up as an engine late period AND as a server xrun, each in its own counter. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <chrono>
#include <thread>

#include "lmmsconfig.h"

#include "AudioDummy.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"

#ifdef LMMS_HAVE_ALSA
#include <dlfcn.h>

#include "AudioAlsa.h"
#include "AudioInputPath.h"

namespace
{
//! Non-zero: the rate the next capture asks ALSA for, in place of the engine's.
unsigned int s_captureRate = 0;
} // namespace

extern "C" int snd_pcm_hw_params_set_rate_near(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int* val, int* dir)
{
	using Real = int (*)(snd_pcm_t*, snd_pcm_hw_params_t*, unsigned int*, int*);
	static const auto real = reinterpret_cast<Real>(dlsym(RTLD_NEXT, "snd_pcm_hw_params_set_rate_near"));
	if (s_captureRate != 0 && val != nullptr) { *val = s_captureRate; }
	return real != nullptr ? real(pcm, params, val, dir) : -1;
}
#endif
#ifdef LMMS_HAVE_JACK
#include "AudioJack.h"
#endif

namespace lmms
{

namespace
{

QJsonObject cpuLoad()
{
	const ControlResult load = ControlRegistry::instance()->invoke(QStringLiteral("mixer.cpu_load"));
	return load.ok ? load.result : QJsonObject{};
}

double latePeriodCount(const QJsonObject& load)
{
	return load.value(QStringLiteral("late_periods")).toObject().value(QStringLiteral("count")).toDouble(-1);
}

double valueOf(const QJsonObject& device, const char* key)
{
	return device.value(QLatin1String(key)).toObject().value(QStringLiteral("value")).toDouble(-1);
}

bool available(const QJsonObject& device, const char* key)
{
	return device.value(QLatin1String(key)).toObject().value(QStringLiteral("available")).toBool();
}

constexpr const char* DeviceReadings[] = {"sample_rate", "input_sample_rate", "period_frames", "buffer_frames", "input_latency",
	"output_latency", "xruns"};

} // namespace

class AudioDeviceDiagnosticsTest : public QObject
{
	Q_OBJECT

private:
	std::atomic<bool> m_spin{false};

	static void verifyEngineBlock(const QJsonObject& device)
	{
		const QJsonObject engine = device.value(QStringLiteral("engine")).toObject();
		const AudioEngine* audio = Engine::audioEngine();
		QCOMPARE(engine.value(QStringLiteral("sample_rate_hz")).toDouble(), static_cast<double>(audio->baseSampleRate()));
		QCOMPARE(engine.value(QStringLiteral("output_sample_rate_hz")).toDouble(), static_cast<double>(audio->outputSampleRate()));
		QCOMPARE(engine.value(QStringLiteral("period_frames")).toDouble(), static_cast<double>(audio->framesPerPeriod()));
		QCOMPARE(engine.value(QStringLiteral("buffer_frames")).toDouble(), static_cast<double>(audio->framesPerAudioBuffer()));
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		// A render made late on purpose: the slot runs on the render thread inside the timed period.
		connect(Engine::audioEngine(), &AudioEngine::nextAudioBuffer, this, [this] {
			if (!m_spin.exchange(false)) { return; }
			const AudioEngine* engine = Engine::audioEngine();
			const auto period = std::chrono::microseconds(
				std::int64_t{1000000} * engine->framesPerPeriod() / engine->outputSampleRate());
			const auto until = std::chrono::steady_clock::now() + 3 * period;
			while (std::chrono::steady_clock::now() < until) {}
		}, Qt::DirectConnection);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void noDeviceStillReportsCpuAndMarksDeviceReadingsUnavailable()
	{
		const QJsonObject load = cpuLoad();
		QVERIFY2(!load.isEmpty(), "mixer.cpu_load failed");
		// The CPU and history state is all there without a device.
		QVERIFY(load.contains(QStringLiteral("engine_percent")));
		QVERIFY(load.value(QStringLiteral("tracks")).isArray());
		QVERIFY(latePeriodCount(load) >= 0);

		const QJsonObject device = load.value(QStringLiteral("audio_device")).toObject();
		QVERIFY2(!device.isEmpty(), "mixer.cpu_load has no audio_device");
		QCOMPARE(device.value(QStringLiteral("backend")).toString(), AudioDummy::name());
		QCOMPARE(device.value(QStringLiteral("reports_timing")).toBool(), false);
		verifyEngineBlock(device);
		for (const char* key : DeviceReadings)
		{
			const QJsonObject reading = device.value(QLatin1String(key)).toObject();
			QVERIFY2(reading.contains(QStringLiteral("available")) && !reading.value(QStringLiteral("available")).toBool(),
				key);
			QVERIFY2(!reading.contains(QStringLiteral("value")), key);  // unavailable, not a zero
			QVERIFY2(!reading.value(QStringLiteral("unit")).toString().isEmpty(), key);
		}
	}

	void alsaReportsWhatThePcmGrantedAndCountsUnderruns()
	{
#ifndef LMMS_HAVE_ALSA
		QSKIP("built without ALSA");
#else
		ConfigManager::inst()->setValue(QStringLiteral("audioalsa"), QStringLiteral("device"), QStringLiteral("null"));
		AudioEngine* engine = Engine::audioEngine();
		bool ok = false;
		auto* alsa = new AudioAlsa(ok, engine);
		if (!ok) { delete alsa; QSKIP("the ALSA null PCM does not open here"); }
		engine->storeAudioDevice();
		engine->setAudioDevice(alsa, false);

		const QJsonObject before = cpuLoad();
		const QJsonObject device = before.value(QStringLiteral("audio_device")).toObject();
		QCOMPARE(device.value(QStringLiteral("backend")).toString(), AudioAlsa::name());
		QVERIFY(device.value(QStringLiteral("reports_timing")).toBool());
		verifyEngineBlock(device);
		QVERIFY(available(device, "sample_rate"));
		QCOMPARE(valueOf(device, "sample_rate"), static_cast<double>(alsa->m_grantedRate));
		QVERIFY(alsa->m_grantedRate > 0);
		QCOMPARE(valueOf(device, "period_frames"), static_cast<double>(alsa->m_periodSize));
		QCOMPARE(valueOf(device, "buffer_frames"), static_cast<double>(alsa->m_bufferSize));
		QVERIFY(alsa->m_periodSize > 0 && alsa->m_bufferSize >= 2 * alsa->m_periodSize);
		const QJsonObject output = device.value(QStringLiteral("output_latency")).toObject();
		QCOMPARE(output.value(QStringLiteral("value")).toDouble(), static_cast<double>(alsa->m_bufferSize));
		QCOMPARE(output.value(QStringLiteral("basis")).toString(), QStringLiteral("playback_buffer"));
		QCOMPARE(output.value(QStringLiteral("unit")).toString(), QStringLiteral("frames"));
		QVERIFY(qAbs(output.value(QStringLiteral("ms")).toDouble() - 1000.0 * alsa->m_bufferSize / alsa->m_grantedRate) < 1e-9);
		QVERIFY2(!available(device, "input_latency"), "no capture is open, so no input latency");
		QVERIFY2(!available(device, "input_sample_rate"), "no capture is open, so no input rate");
		QVERIFY(available(device, "xruns"));
		QCOMPARE(valueOf(device, "xruns"), 0.0);

		// Two playback underruns, recovered the way the playback thread recovers them.
		QCOMPARE(alsa->handleError(-EPIPE), 0);
		QCOMPARE(alsa->handleError(-EPIPE), 0);
		const QJsonObject after = cpuLoad();
		QCOMPARE(valueOf(after.value(QStringLiteral("audio_device")).toObject(), "xruns"), 2.0);
		QCOMPARE(latePeriodCount(after), latePeriodCount(before));  // an xrun is not a late period

		engine->restoreAudioDevice();
		QCOMPARE(cpuLoad().value(QStringLiteral("audio_device")).toObject().value(QStringLiteral("backend")).toString(),
			AudioDummy::name());
#endif
	}

	void alsaCaptureAtItsOwnRateReportsInputLatencyAtThatRate()
	{
#ifndef LMMS_HAVE_ALSA
		QSKIP("built without ALSA");
#else
		ConfigManager* config = ConfigManager::inst();
		config->setValue(QStringLiteral("audioalsa"), QStringLiteral("device"), QStringLiteral("null"));
		config->setValue(QStringLiteral("audioinput"), QStringLiteral("device"), QStringLiteral("null"));
		config->setValue(QStringLiteral("audioinput"), QStringLiteral("channels"), QStringLiteral("2"));
		AudioEngine* engine = Engine::audioEngine();
		bool ok = false;
		auto* alsa = new AudioAlsa(ok, engine);
		if (!ok) { delete alsa; QSKIP("the ALSA null PCM does not open here"); }
		const unsigned int playbackRate = alsa->m_grantedRate;
		QVERIFY(playbackRate > 0);
		s_captureRate = playbackRate == 48000 ? 96000 : 48000;
		const bool opened = alsa->openCapture();
		s_captureRate = 0;
		if (!opened) { delete alsa; QSKIP("the ALSA null PCM does not open for capture here"); }
		alsa->m_captureOpen.store(true, std::memory_order_release);  // what startProcessingImpl marks, minus the thread
		engine->storeAudioDevice();
		engine->setAudioDevice(alsa, false);

		const unsigned int captureRate = playbackRate == 48000 ? 96000 : 48000;
		const QJsonObject device = cpuLoad().value(QStringLiteral("audio_device")).toObject();
		QCOMPARE(valueOf(device, "sample_rate"), static_cast<double>(playbackRate));
		QVERIFY(available(device, "input_sample_rate"));
		QCOMPARE(valueOf(device, "input_sample_rate"), static_cast<double>(captureRate));
		QCOMPARE(alsa->m_captureGrantedRate, captureRate);
		const QJsonObject input = device.value(QStringLiteral("input_latency")).toObject();
		QVERIFY(input.value(QStringLiteral("available")).toBool());
		QCOMPARE(input.value(QStringLiteral("basis")).toString(), QStringLiteral("capture_period"));
		const double frames = input.value(QStringLiteral("value")).toDouble();
		QCOMPARE(frames, static_cast<double>(alsa->m_capturePeriodSize));
		QVERIFY(frames > 0);
		const double ms = input.value(QStringLiteral("ms")).toDouble();
		QVERIFY2(qAbs(ms - 1000.0 * frames / captureRate) < 1e-9, qPrintable(QString::number(ms)));
		QVERIFY2(qAbs(ms - 1000.0 * frames / playbackRate) > 1e-6, "input latency was converted at the playback rate");
		// The output side keeps playback's rate.
		const QJsonObject output = device.value(QStringLiteral("output_latency")).toObject();
		QVERIFY(qAbs(output.value(QStringLiteral("ms")).toDouble()
			- 1000.0 * output.value(QStringLiteral("value")).toDouble() / playbackRate) < 1e-9);

		alsa->closeCapture();
		const QJsonObject closed = cpuLoad().value(QStringLiteral("audio_device")).toObject();
		QVERIFY2(!available(closed, "input_sample_rate"), "a closed capture has no input rate");
		QVERIFY2(!available(closed, "input_latency"), "a closed capture has no input latency");
		engine->restoreAudioDevice();
		config->setValue(QStringLiteral("audioinput"), QStringLiteral("device"), QString());
#endif
	}

	void jackReportsTheServersTimingAndXruns()
	{
#ifndef LMMS_HAVE_JACK
		QSKIP("built without JACK");
#else
		const QString jackd = QStandardPaths::findExecutable(QStringLiteral("jackd"));
		if (jackd.isEmpty()) { QSKIP("jackd is not installed"); }
		const QString server = QStringLiteral("zene-diagnostics-%1").arg(QCoreApplication::applicationPid());
		qputenv("JACK_DEFAULT_SERVER", server.toUtf8());
		qputenv("JACK_NO_START_SERVER", "1");
		qputenv("JACK_NO_AUDIO_RESERVATION", "1");
		QProcess daemon;
		daemon.setProcessChannelMode(QProcess::ForwardedErrorChannel);
		daemon.setStandardOutputFile(QProcess::nullDevice());
		daemon.start(jackd, {QStringLiteral("--no-realtime"), QStringLiteral("-n"), server, QStringLiteral("-d"),
			QStringLiteral("dummy"), QStringLiteral("-r"), QStringLiteral("48000"), QStringLiteral("-p"), QStringLiteral("256")});
		if (!daemon.waitForStarted(5000)) { QSKIP("jackd did not start"); }

		AudioEngine* engine = Engine::audioEngine();
		AudioJack* jack = nullptr;
		for (int attempt = 0; attempt < 100 && jack == nullptr; ++attempt)
		{
			bool ok = false;
			auto* candidate = new AudioJack(ok, engine);
			if (ok) { jack = candidate; break; }
			delete candidate;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		if (jack == nullptr) { daemon.kill(); daemon.waitForFinished(); QSKIP("no client could join the private jackd"); }
		engine->storeAudioDevice();
		engine->setAudioDevice(jack, true);

		const QJsonObject before = cpuLoad();
		const QJsonObject device = before.value(QStringLiteral("audio_device")).toObject();
		QCOMPARE(device.value(QStringLiteral("backend")).toString(), AudioJack::name());
		QVERIFY(device.value(QStringLiteral("running")).toBool());
		verifyEngineBlock(device);
		QCOMPARE(valueOf(device, "sample_rate"), 48000.0);
		QCOMPARE(valueOf(device, "input_sample_rate"), 48000.0);
		QCOMPARE(device.value(QStringLiteral("engine")).toObject().value(QStringLiteral("output_sample_rate_hz")).toDouble(), 48000.0);
		QCOMPARE(valueOf(device, "period_frames"), 256.0);
		QVERIFY2(!available(device, "buffer_frames"), "JACK does not report its period count");
		QVERIFY(available(device, "input_latency") && available(device, "output_latency"));
		QCOMPARE(device.value(QStringLiteral("output_latency")).toObject().value(QStringLiteral("basis")).toString(),
			QStringLiteral("jack_playback_latency_range"));
		QCOMPARE(device.value(QStringLiteral("input_latency")).toObject().value(QStringLiteral("basis")).toString(),
			QStringLiteral("jack_capture_latency_range"));
		QVERIFY(available(device, "xruns"));

		// One render made three periods long: the engine marks it late, and the server - left without
		// this client's output - reports an xrun. Two counters, one cause each.
		const double lateBefore = latePeriodCount(before);
		const double xrunsBefore = valueOf(device, "xruns");
		m_spin = true;
		QTRY_VERIFY_WITH_TIMEOUT(latePeriodCount(cpuLoad()) > lateBefore, 5000);
		QTRY_VERIFY_WITH_TIMEOUT(valueOf(cpuLoad().value(QStringLiteral("audio_device")).toObject(), "xruns") > xrunsBefore, 5000);

		engine->restoreAudioDevice();
		daemon.terminate();
		if (!daemon.waitForFinished(5000)) { daemon.kill(); daemon.waitForFinished(); }
#endif
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::AudioDeviceDiagnosticsTest)
#include "AudioDeviceDiagnosticsTest.moc"
