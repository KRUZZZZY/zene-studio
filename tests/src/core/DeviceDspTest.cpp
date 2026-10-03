/*
 * DeviceDspTest.cpp - the de-esser, transient shaper, vocoder and console devices, and the console across the mix
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
/*! 0.5.0 L10 (W3): the native devices do what they say - the de-esser turns down a loud high band
 *  and leaves the lows (MX-147), the transient shaper lifts or softens an attack (MX-148), the
 *  vocoder sounds only while the voice does (MX-154), the console is transparent when quiet and
 *  mixer.console_apply puts it on every channel as one undo step (MX-150) - and none of them
 *  allocates while processing (the realtime rule). */

#include <QtTest>

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "SampleFrame.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr double kTwoPi = 6.283185307179586;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

MixerChannel* channel(int index) { return Engine::mixer()->mixerChannel(index); }

QString ch(int index) { return control::channelIdOf(channel(index)); }

EffectChain& chainOf(int index) { return channel(index)->m_fxChain; }

double rate() { return Engine::audioEngine()->outputSampleRate(); }

QString loadByName(const QString& target, const QString& name)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		if (device.toObject().value(QStringLiteral("name")).toString() != name) { continue; }
		return run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target},
			{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}}).result.value(QStringLiteral("id")).toString();
	}
	return QString();
}

void set(const QString& target, const QString& plugin, const char* name, double value)
{
	QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
		{QStringLiteral("name"), QLatin1String(name)}, {QStringLiteral("value"), value}}).ok);
}

double parameterValue(const QString& target, const QString& plugin, const QString& name)
{
	for (const QJsonValue& value : run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
		{QStringLiteral("plugin"), plugin}}).result.value(QStringLiteral("parameters")).toArray())
	{
		if (value.toObject().value(QStringLiteral("name")).toString() == name) { return value.toObject().value(QStringLiteral("value")).toDouble(); }
	}
	return -1e9;
}

using Signal = std::function<SampleFrame(long frame)>;

//! One period, allocated once (initTestCase): a block run under the allocation probe allocates nothing itself.
std::vector<SampleFrame>& block()
{
	static std::vector<SampleFrame> frames;
	return frames;
}

//! @a blocks periods of @a signal through @a chain (continuing at *@a at); the last block's peak, left
//! and right - over its first @a window frames when @a window is given.
SampleFrame runBlocks(EffectChain& chain, const Signal& signal, int blocks, long* at, std::size_t window = 0)
{
	std::vector<SampleFrame>& frames = block();
	SampleFrame* data[1] = {frames.data()};
	SampleFrame peak;
	const std::size_t measured = window > 0 ? std::min(window, frames.size()) : frames.size();
	for (int b = 0; b < blocks; ++b)
	{
		for (SampleFrame& frame : frames) { frame = signal((*at)++); }
		AudioBus bus{data, 1, static_cast<f_cnt_t>(frames.size())};
		chain.processAudioBuffer(bus);
		peak = SampleFrame();
		for (std::size_t f = 0; f < measured; ++f)
		{
			peak = SampleFrame(std::max(peak[0], std::abs(frames[f][0])), std::max(peak[1], std::abs(frames[f][1])));
		}
	}
	return peak;
}

Signal sine(double hz, float level)
{
	return [hz, level](long f) {
		const auto x = static_cast<float>(level * std::sin(kTwoPi * hz * static_cast<double>(f) / rate()));
		return SampleFrame(x, x);
	};
}

//! A repeatable noise in -level..level.
float noise(long f, float level)
{
	const auto x = static_cast<std::uint32_t>(f) * 1664525u + 1013904223u;
	return level * (static_cast<float>(x >> 8) / static_cast<float>(1u << 24) * 2.f - 1.f);
}

int consolesOn(int index)
{
	int count = 0;
	for (const Effect* effect : chainOf(index).effects()) { count += QString::fromUtf8(effect->descriptor()->name) == QLatin1String("console"); }
	return count;
}

} // namespace

class DeviceDspTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		block().assign(Engine::audioEngine()->framesPerPeriod(), SampleFrame());
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { chainOf(i).clear(); }
	}

	//! MX-147: a loud 9 kHz tone is turned down and reported as reduction; a 200 Hz tone passes.
	void theDeEsserTurnsDownTheHighBandOnly()
	{
		const QString deesser = loadByName(ch(1), QStringLiteral("deesser"));
		if (deesser.isEmpty()) { QSKIP("this build has no de-esser module"); }
		set(ch(1), deesser, "Threshold", -40.0);
		set(ch(1), deesser, "Range", 12.0);
		long at = 0;
		const SampleFrame high = runBlocks(chainOf(1), sine(9000.0, 0.5f), 60, &at);
		QVERIFY2(high[0] < 0.5f * 0.5f, qPrintable(QString::number(high[0])));  // at least 6 dB down
		QVERIFY(chainOf(1).effects().front()->gainReductionDb() > 6.f);
		at = 0;
		const SampleFrame low = runBlocks(chainOf(1), sine(200.0, 0.5f), 60, &at);
		QVERIFY2(std::abs(low[0] - 0.5f) < 0.03f, qPrintable(QString::number(low[0])));
	}

	//! MX-148: +100 % attack lifts the start of a burst; -100 % softens it; neither moves the steady level.
	void theTransientShaperShapesAnAttack()
	{
		const QString shaper = loadByName(ch(1), QStringLiteral("transientshaper"));
		if (shaper.isEmpty()) { QSKIP("this build has no transient shaper module"); }
		const Signal burst = [](long) { return SampleFrame(0.25f, 0.25f); };
		const Signal silence = [](long) { return SampleFrame(); };
		long at = 0;
		set(ch(1), shaper, "Attack", 100.0);
		runBlocks(chainOf(1), silence, 20, &at);
		QVERIFY(runBlocks(chainOf(1), burst, 1, &at, 64)[0] > 0.25f * 1.5f);
		QVERIFY(std::abs(runBlocks(chainOf(1), burst, 200, &at)[0] - 0.25f) < 0.01f);
		set(ch(1), shaper, "Attack", -100.0);
		runBlocks(chainOf(1), silence, 200, &at);
		QVERIFY(runBlocks(chainOf(1), burst, 1, &at, 64)[0] < 0.25f * 0.9f);  // the first 1.5 ms of the burst
	}

	//! MX-154: with the voice on the left, the carrier on the right sounds only while the voice does.
	void theVocoderFollowsTheVoice()
	{
		const QString vocoder = loadByName(ch(1), QStringLiteral("vocoder"));
		if (vocoder.isEmpty()) { QSKIP("this build has no vocoder module"); }
		set(ch(1), vocoder, "Voice", 1.0);  // the left side is the voice
		long at = 0;
		const Signal mute = [](long f) { return SampleFrame(0.f, noise(f, 0.5f)); };
		const Signal talk = [](long f) {
			return SampleFrame(static_cast<float>(0.5 * std::sin(kTwoPi * 1000.0 * static_cast<double>(f) / rate())), noise(f, 0.5f));
		};
		QVERIFY(runBlocks(chainOf(1), mute, 40, &at)[0] < 1e-3f);
		QVERIFY(runBlocks(chainOf(1), talk, 40, &at)[0] > 1e-2f);
		// The realtime rule: a warm block allocates nothing.
		test::tlCountAllocations = true;
		test::resetAllocationCount();
		runBlocks(chainOf(1), talk, 1, &at);
		const std::uint64_t allocations = test::tlAllocationCount;
		test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! MX-150: a quiet signal passes the console unchanged; mixer.console_apply adds one to every channel,
	//! changes them all on a second call, removes them, and each call undoes in one step.
	void theConsoleGoesOnEveryChannel()
	{
		const QString console = loadByName(ch(1), QStringLiteral("console"));
		if (console.isEmpty()) { QSKIP("this build has no console module"); }
		set(ch(1), console, "Drive", 0.0);
		long at = 0;
		QVERIFY(std::abs(runBlocks(chainOf(1), sine(1000.0, 0.05f), 60, &at)[0] - 0.05f) < 0.002f);
		chainOf(1).clear();
		const int channels = Engine::mixer()->numChannels();
		const ControlResult applied = run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 50.0},
			{QStringLiteral("character"), QStringLiteral("warm")}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
		QCOMPARE(applied.result.value(QStringLiteral("added")).toInt(), channels);
		QCOMPARE(consolesOn(2), 1);
		const QString onTwo = control::effectIdOf(chainOf(2).effects().back());
		QCOMPARE(parameterValue(ch(2), onTwo, QStringLiteral("Drive")), 50.0);
		QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 80.0}}).ok);
		QCOMPARE(consolesOn(2), 1);  // kept, not added again
		QCOMPARE(parameterValue(ch(2), onTwo, QStringLiteral("Drive")), 80.0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(parameterValue(ch(2), onTwo, QStringLiteral("Drive")), 50.0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(consolesOn(2), 0);
		QCOMPARE(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 150.0}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("mixer.console_apply")).ok);
		QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("remove"), true}}).ok);
		QCOMPARE(consolesOn(0), 0);
	}
};

QTEST_MAIN(DeviceDspTest)
#include "DeviceDspTest.moc"
