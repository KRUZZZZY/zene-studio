/*
 * RealtimeSnapshotTest.cpp - what the audio thread reads while another thread writes it is published, not shared
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
 */
/*! catch-up W3: a device's keep-running flag and latency override (DeviceOptions), the compressor's key
 *  filter (plugins/Compressor/CompressorKeyFilter.h) and an LV2 plug-in's latency port (Lv2Proc) reach
 *  the thread that reads them through atomics, and a bypassed dynamics device's reduction meter reads
 *  0 dB (the device's own reading and the mixer strip's). The threaded cases are races only a TSan
 *  build reports as such; in every build they prove the value that arrives. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DeviceOptions.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerStrip.h"
#include "SampleFrame.h"
#include "lmmsconfig.h"

using namespace lmms;

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

MixerChannel& strip() { return *Engine::mixer()->mixerChannel(1); }

QString stripId() { return control::channelIdOf(&strip()); }

//! The id of a fresh @a name device on the strip, or empty when this build has no such module.
QString loadDevice(const QString& name)
{
	const QJsonArray devices = invoke(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray();
	for (const QJsonValue& device : devices)
	{
		if (device.toObject().value(QStringLiteral("name")).toString() != name) { continue; }
		return invoke(QStringLiteral("plugin.load"), {{QStringLiteral("target"), stripId()},
			{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}}).result.value(QStringLiteral("id")).toString();
	}
	return QString();
}

bool setParameter(const QString& plugin, const QString& name, double value)
{
	return invoke(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), stripId()}, {QStringLiteral("plugin"), plugin},
		{QStringLiteral("name"), name}, {QStringLiteral("value"), value}}).ok;
}

bool bypass(const QString& plugin, bool off)
{
	return invoke(QStringLiteral("plugin.bypass"), {{QStringLiteral("target"), stripId()}, {QStringLiteral("plugin"), plugin},
		{QStringLiteral("bypass"), off}}).ok;
}

//! A period-sized buffer and the sine it is filled with, one per thread that processes.
struct Feed
{
	std::vector<SampleFrame> frames = std::vector<SampleFrame>(Engine::audioEngine()->framesPerPeriod());
	double hz = 1000.0;
	long at = 0;

	//! @a blocks periods of the sine through @a chain; nothing here allocates once the buffer exists.
	void run(EffectChain& chain, int blocks)
	{
		const double rate = Engine::audioEngine()->outputSampleRate();
		for (int b = 0; b < blocks; ++b)
		{
			for (SampleFrame& frame : frames)
			{
				const auto x = static_cast<float>(0.5 * std::sin(6.283185307179586 * hz * static_cast<double>(at++) / rate));
				frame = SampleFrame(x, x);
			}
			SampleFrame* data[1] = {frames.data()};
			AudioBus bus{data, 1, static_cast<f_cnt_t>(frames.size())};
			chain.processAudioBuffer(bus);
		}
	}

	//! The strip's gain-reduction meter after the last block (mixerstrip::updateMeters, as the mixer runs it).
	float stripReduction()
	{
		mixerstrip::updateMeters(strip(), frames.data(), 1.f, static_cast<f_cnt_t>(frames.size()));
		return strip().m_gainReduction.get();
	}
};

} // namespace

class RealtimeSnapshotTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test processes the chain itself
		while (Engine::mixer()->numChannels() <= 2) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init() { strip().m_fxChain.clear(); }

	//! The two audio-read options are atomics, and DeviceOptions is still a value: copied, compared,
	//! saved and read back with them.
	void deviceOptionsStayACopyableValue()
	{
		static_assert(std::is_copy_constructible_v<DeviceOptions> && std::is_copy_assignable_v<DeviceOptions>);
		DeviceOptions options;
		QVERIFY(options.isDefault());
		options.keepRunning = true;
		options.latencyOverride = 512;
		options.setAlias(QStringLiteral("Volume"), QStringLiteral("Drive"));
		DeviceOptions copy = options;
		QVERIFY(copy == options);
		QVERIFY(copy.keepRunning);
		QCOMPARE(copy.latencyOverride.load(), 512);
		copy.latencyOverride = -1;
		QVERIFY(copy != options);
		copy = options;
		QVERIFY(copy == options);
		QDomDocument document;
		QDomElement element = document.createElement(QStringLiteral("device"));
		options.save(element);
		DeviceOptions back;
		back.load(element);
		QVERIFY(back == options);
	}

	//! applyOptions' assignment runs while the audio thread reads keepRunning (Effect::handleAutoQuit):
	//! the last options written are the ones the device and the chain's compensation end with.
	void optionsAreWrittenWhileTheAudioThreadReadsThem()
	{
		const QString amp = loadDevice(QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		Effect* effect = strip().m_fxChain.effects().front();
		Feed feed;
		std::thread audio([&feed] { feed.run(strip().m_fxChain, 400); });
		for (int i = 0; i < 400; ++i)
		{
			DeviceOptions options = effect->deviceOptions();
			options.keepRunning = i % 2 == 0;
			options.latencyOverride = i % 3 == 1 ? -1 : i;
			effect->deviceOptions() = options;
		}
		audio.join();
		QVERIFY(!effect->deviceOptions().keepRunning);  // the last write, i = 399: off, an override of 399
		QCOMPARE(effect->compensatedLatencyFrames(), 399);
		QJsonObject args{{QStringLiteral("target"), stripId()}, {QStringLiteral("plugin"), amp},
			{QStringLiteral("keep_running"), true}, {QStringLiteral("latency_override"), 256}};
		const ControlResult set = invoke(QStringLiteral("plugin.options"), args);
		QVERIFY(set.ok);
		QCOMPARE(set.result.value(QStringLiteral("keep_running")).toBool(), true);
		QCOMPARE(set.result.value(QStringLiteral("latency_override")).toInt(), 256);
		QCOMPARE(strip().m_fxChain.latencyFrames(), 256);
	}

	//! The key high-pass the knob sets is the one the detector hears: a 100 Hz tone the compressor
	//! turns down is ignored with the key at 2 kHz, and heard again once the key is off.
	void theKeyFilterReachesTheDetector()
	{
		const QString comp = loadDevice(QStringLiteral("compressor"));
		if (comp.isEmpty()) { QSKIP("this build has no compressor module"); }
		QVERIFY(setParameter(comp, QStringLiteral("Threshold"), -40.0));
		QVERIFY(setParameter(comp, QStringLiteral("Ratio"), 20.0));
		Effect* effect = strip().m_fxChain.effects().front();
		Feed feed;
		feed.hz = 100.0;
		feed.run(strip().m_fxChain, 40);
		QVERIFY2(effect->gainReductionDb() > 10.f, qPrintable(QString::number(effect->gainReductionDb())));
		QVERIFY(setParameter(comp, QStringLiteral("Key High-pass"), 2000.0));
		feed.run(strip().m_fxChain, 80);  // past the 100 ms release
		QVERIFY2(effect->gainReductionDb() < 1.f, qPrintable(QString::number(effect->gainReductionDb())));
		QVERIFY(setParameter(comp, QStringLiteral("Key High-pass"), 20.0));  // the end of its range: off
		feed.run(strip().m_fxChain, 40);
		QVERIFY2(effect->gainReductionDb() > 10.f, qPrintable(QString::number(effect->gainReductionDb())));
		// The realtime rule: the block that adopts a new key filter allocates nothing.
		QVERIFY(setParameter(comp, QStringLiteral("Key High-pass"), 1000.0));
		test::tlCountAllocations = true;
		test::resetAllocationCount();
		feed.run(strip().m_fxChain, 1);
		const std::uint64_t allocations = test::tlAllocationCount;
		test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! The key filter is redesigned while the audio thread runs it; the last design published is the
	//! one it ends up running.
	void theKeyFilterMovesWhileTheAudioThreadRuns()
	{
		const QString comp = loadDevice(QStringLiteral("compressor"));
		if (comp.isEmpty()) { QSKIP("this build has no compressor module"); }
		QVERIFY(setParameter(comp, QStringLiteral("Threshold"), -40.0));
		QVERIFY(setParameter(comp, QStringLiteral("Ratio"), 20.0));
		Feed feed;
		feed.hz = 100.0;
		std::atomic<bool> done{false};
		std::thread audio([&feed, &done] { feed.run(strip().m_fxChain, 300); done = true; });
		for (int i = 0; !done; ++i) { setParameter(comp, QStringLiteral("Key High-pass"), i % 2 == 0 ? 20.0 : 2000.0); }
		audio.join();
		QVERIFY(setParameter(comp, QStringLiteral("Key High-pass"), 2000.0));
		feed.run(strip().m_fxChain, 80);
		QVERIFY2(strip().m_fxChain.effects().front()->gainReductionDb() < 1.f,
			qPrintable(QString::number(strip().m_fxChain.effects().front()->gainReductionDb())));
	}

	void aBypassedDynamicsDeviceReadsNoReduction_data()
	{
		QTest::addColumn<QString>("device");
		QTest::addColumn<double>("hz");
		QTest::addColumn<QString>("amount");  // the parameter that lets it turn down far
		QTest::addColumn<double>("amountValue");
		QTest::newRow("de-esser") << QStringLiteral("deesser") << 9000.0 << QStringLiteral("Range") << 12.0;
		QTest::newRow("compressor") << QStringLiteral("compressor") << 100.0 << QStringLiteral("Ratio") << 20.0;
	}

	//! A device turning a loud tone down reports it, on its own and on the strip; bypassed, both read
	//! 0 dB at once (not the last block it processed); switched back on, it reports again.
	void aBypassedDynamicsDeviceReadsNoReduction()
	{
		QFETCH(QString, device);
		QFETCH(double, hz);
		QFETCH(QString, amount);
		QFETCH(double, amountValue);
		const QString plugin = loadDevice(device);
		if (plugin.isEmpty()) { QSKIP("this build has no such module"); }
		QVERIFY(setParameter(plugin, QStringLiteral("Threshold"), -40.0));
		QVERIFY(setParameter(plugin, amount, amountValue));
		Effect* effect = strip().m_fxChain.effects().front();
		Feed feed;
		feed.hz = hz;
		feed.run(strip().m_fxChain, 60);
		QVERIFY2(effect->gainReductionDb() > 6.f, qPrintable(QString::number(effect->gainReductionDb())));
		QVERIFY(feed.stripReduction() > 6.f);
		QVERIFY(bypass(plugin, true));
		feed.run(strip().m_fxChain, 1);
		QCOMPARE(effect->gainReductionDb(), 0.f);
		QCOMPARE(feed.stripReduction(), 0.f);
		QVERIFY(bypass(plugin, false));
		feed.run(strip().m_fxChain, 60);
		QVERIFY(feed.stripReduction() > 6.f);
	}

	//! An LV2 plug-in's latency is read from what its last run() left, while the audio thread runs it.
	void anLv2LatencyIsReadWhileItRuns()
	{
#ifndef LMMS_HAVE_LV2
		QSKIP("this build has no LV2 host");
#else
		EffectChain chain{nullptr};
		Effect* effect = nullptr;
		for (const ControlDeviceEntry& entry : controlDeviceCatalogue())
		{
			if (entry.format != QLatin1String("lv2") || entry.kind != QLatin1String("effect") || !entry.loadable) { continue; }
			ControlResult error;
			effect = controlInstantiateDevice(entry, &chain, &error);
			if (effect != nullptr) { break; }
		}
		if (effect == nullptr) { QSKIP("no installed LV2 effect loads"); }
		QVERIFY(effect->latencyFrames() >= 0);  // before any run(): 0 or a port value, never an unread port
		Feed feed;
		std::atomic<bool> done{false};
		std::thread audio([&feed, &chain, &done] { feed.run(chain, 100); done = true; });
		int lowest = 0;
		while (!done) { lowest = std::min(lowest, effect->latencyFrames()); }
		audio.join();
		QCOMPARE(lowest, 0);
		QVERIFY(effect->latencyFrames() >= 0);
		chain.clear();
#endif
	}
};

QTEST_MAIN(RealtimeSnapshotTest)
#include "RealtimeSnapshotTest.moc"
