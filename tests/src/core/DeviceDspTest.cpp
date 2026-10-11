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
 *  mixer.console_apply puts it on every channel as one undo step (MX-150), the dynamic EQ cuts a
 *  band statically, dynamically and on the mid alone (MX-143), the convolution reverb plays a loaded
 *  impulse response one partition late (MX-152), the recorder writes what passes it (MX-156) - and
 *  none of them allocates while processing. */

#include <QtTest>

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BasicFilters.h"
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
	// plugin.param_get reads ONE parameter, by 'name' (or 'index').
	const ControlResult read = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
		{QStringLiteral("plugin"), plugin}, {QStringLiteral("name"), name}});
	return read.ok ? read.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble() : -1e9;
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

//! A mono 16-bit WAV of @a samples in @a path, at @a hz - by default the engine's rate, so nothing is resampled.
bool writeWav(const QString& path, const std::vector<qint16>& samples, quint32 hz = 0)
{
	if (hz == 0) { hz = static_cast<quint32>(rate()); }
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	const auto bytes = static_cast<quint32>(samples.size() * 2);
	out.writeRawData("RIFF", 4);
	out << quint32(36 + bytes);
	out.writeRawData("WAVEfmt ", 8);
	out << quint32(16) << quint16(1) << quint16(1) << hz << quint32(hz * 2) << quint16(2) << quint16(16);
	out.writeRawData("data", 4);
	out << bytes;
	for (const qint16 sample : samples) { out << sample; }
	return out.status() == QDataStream::Ok;
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
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test reads and writes channel buffers itself
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

	void formantFilterClampsToLastVowel()
	{
		using Filter = BasicFilters<1>;
		Filter endpointFilter{48000};
		Filter aboveRangeFilter{48000};
		endpointFilter.setFilterType(Filter::FilterType::Formantfilter);
		aboveRangeFilter.setFilterType(Filter::FilterType::Formantfilter);
		endpointFilter.calcFilterCoeffs(17500.0f, 0.5f);
		aboveRangeFilter.calcFilterCoeffs(20000.0f, 0.5f);

		for (int sample = 0; sample < 64; ++sample)
		{
			const float input = sample == 0 ? 0.5f : 0.0f;
			QCOMPARE(aboveRangeFilter.update(input, 0), endpointFilter.update(input, 0));
		}
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

	//! MX-143: transparent at rest; a static cut; a dynamic cut that only a loud band gets; mid only.
	void theDynamicEqCutsStaticallyDynamicallyAndOnTheMid()
	{
		const QString eq = loadByName(ch(1), QStringLiteral("dynamiceq"));
		if (eq.isEmpty()) { QSKIP("this build has no dynamic EQ module"); }
		long at = 0;
		QVERIFY(std::abs(runBlocks(chainOf(1), sine(500.0, 0.5f), 40, &at)[0] - 0.5f) < 0.01f);
		set(ch(1), eq, "Gain 2", -12.0);
		QVERIFY(std::abs(runBlocks(chainOf(1), sine(500.0, 0.5f), 40, &at)[0] - 0.126f) < 0.02f);
		set(ch(1), eq, "Gain 2", 0.0);
		set(ch(1), eq, "Dynamic 2", -12.0);
		set(ch(1), eq, "Threshold 2", -40.0);
		QVERIFY(std::abs(runBlocks(chainOf(1), sine(500.0, 0.5f), 80, &at)[0] - 0.126f) < 0.02f);  // loud: cut
		QVERIFY(std::abs(runBlocks(chainOf(1), sine(500.0, 0.001f), 200, &at)[0] - 0.001f) < 0.0002f);  // quiet: left alone
		set(ch(1), eq, "Dynamic 2", 0.0);
		set(ch(1), eq, "Gain 2", -12.0);
		set(ch(1), eq, "Channels", 1.0);  // the mid only
		const Signal sideOnly = [](long f) {
			const auto x = static_cast<float>(0.5 * std::sin(kTwoPi * 500.0 * static_cast<double>(f) / rate()));
			return SampleFrame(x, -x);
		};
		QVERIFY(std::abs(runBlocks(chainOf(1), sideOnly, 40, &at)[0] - 0.5f) < 0.01f);  // no mid: untouched
	}

	//! MX-152: an impulse response of one click plays the input back one partition late at the
	//! input's level; a device that is not a convolution reverb, or a missing file, is refused; undo.
	void theConvolutionReverbPlaysALoadedImpulse()
	{
		const QString reverb = loadByName(ch(1), QStringLiteral("convolutionreverb"));
		if (reverb.isEmpty()) { QSKIP("this build has no convolution reverb module"); }
		QVERIFY(chainOf(1).latencyFrames() > 0);  // reported to delay compensation
		QTemporaryDir dir;
		const QString click = dir.filePath(QStringLiteral("click.wav"));
		std::vector<qint16> samples(64, 0);
		samples[0] = 16384;
		QVERIFY(writeWav(click, samples));
		const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), reverb}};
		QJsonObject load = device;
		load.insert(QStringLiteral("path"), click);
		const ControlResult loaded = run(QStringLiteral("plugin.impulse_load"), load);
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		long at = 0;
		const SampleFrame peak = runBlocks(chainOf(1), sine(1000.0, 0.5f), 80, &at);
		QVERIFY2(std::abs(peak[0] - 0.5f) < 0.03f, qPrintable(QString::number(peak[0])));
		test::tlCountAllocations = true;
		test::resetAllocationCount();
		runBlocks(chainOf(1), sine(1000.0, 0.5f), 2, &at);
		const std::uint64_t allocations = test::tlAllocationCount;
		test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
		load.insert(QStringLiteral("path"), dir.filePath(QStringLiteral("missing.wav")));
		QCOMPARE(run(QStringLiteral("plugin.impulse_load"), load).errorKind, ControlErrorKind::NotFound);
		const QString console = loadByName(ch(1), QStringLiteral("console"));
		QVERIFY(!console.isEmpty());
		QJsonObject wrong{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), console}, {QStringLiteral("path"), click}};
		QCOMPARE(run(QStringLiteral("plugin.impulse_load"), wrong).errorKind, ControlErrorKind::Refused);
		chainOf(1).clear();
	}

	//! W6 (review lane10 #4): an impulse response recorded at twice the engine's rate is resampled band-limited. Its
	//! click plus a tone above the engine's Nyquist frequency must play as the click alone - flat - and not with
	//! the tone folded down to a quarter of the rate (the old linear resampler: about 7 times louder there).
	void aHighRateImpulseIsResampledWithoutAliasing()
	{
		const QString reverb = loadByName(ch(1), QStringLiteral("convolutionreverb"));
		if (reverb.isEmpty()) { QSKIP("this build has no convolution reverb module"); }
		QTemporaryDir dir;
		const QString file = dir.filePath(QStringLiteral("high-rate.wav"));
		std::vector<qint16> samples(2048, 0);
		samples[0] = 16384;
		for (std::size_t n = 0; n < samples.size(); ++n)  // 0.375 of the file's rate: 0.75 of the engine's
		{
			samples[n] = static_cast<qint16>(samples[n] + 2000.0 * std::sin(kTwoPi * 0.375 * static_cast<double>(n)));
		}
		QVERIFY(writeWav(file, samples, static_cast<quint32>(2 * rate())));
		const ControlResult loaded = run(QStringLiteral("plugin.impulse_load"),
			{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), reverb}, {QStringLiteral("path"), file}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		long at = 0;
		const float low = runBlocks(chainOf(1), sine(1000.0, 0.25f), 80, &at)[0];
		const float folded = runBlocks(chainOf(1), sine(0.99 * rate() / 4.0, 0.25f), 80, &at)[0];
		QVERIFY2(low > 0.01f, qPrintable(QString::number(low)));
		QVERIFY2(folded / low < 1.5f && folded / low > 0.67f,
			qPrintable(QStringLiteral("a quarter of the rate %1, 1 kHz %2").arg(folded).arg(low)));
		chainOf(1).clear();
	}

	//! MX-156: the Recorder passes the sound unchanged and writes what passed to its take; the take lands
	//! on a new audio track as one undo step; a device that is not a Recorder is refused.
	void theRecorderRecordsWhatPassesIt()
	{
		const QString recorder = loadByName(ch(1), QStringLiteral("insertrecorder"));
		if (recorder.isEmpty()) { QSKIP("this build has no recorder module"); }
		QTemporaryDir dir;
		const QString file = dir.filePath(QStringLiteral("take.wav"));
		const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), recorder}};
		QJsonObject start = device;
		start.insert(QStringLiteral("on"), true);
		start.insert(QStringLiteral("file"), file);
		const ControlResult started = run(QStringLiteral("plugin.record"), start);
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QVERIFY(started.result.value(QStringLiteral("recording")).toBool());
		long at = 0;
		const int blocks = 20;
		QVERIFY(std::abs(runBlocks(chainOf(1), sine(440.0, 0.5f), blocks, &at)[0] - 0.5f) < 0.01f);  // passes unchanged
		QJsonObject stop = device;
		stop.insert(QStringLiteral("on"), false);
		const ControlResult stopped = run(QStringLiteral("plugin.record"), stop);
		QVERIFY(stopped.ok);
		QCOMPARE(stopped.result.value(QStringLiteral("frames")).toDouble(), static_cast<double>(blocks) * static_cast<double>(block().size()));
		QCOMPARE(stopped.result.value(QStringLiteral("dropped")).toDouble(), 0.0);
		QVERIFY(QFileInfo(file).size() > 44);
		const int tracks = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult landed = run(QStringLiteral("plugin.recorder_land"), device);
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks);
		const QString console = loadByName(ch(1), QStringLiteral("console"));
		QVERIFY(!console.isEmpty());
		QCOMPARE(run(QStringLiteral("plugin.record"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), console}}).errorKind,
			ControlErrorKind::Refused);
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
		set(ch(1), console, "Oversampling", 2.0);  // MX-100: 4x, still transparent when quiet
		QVERIFY(std::abs(runBlocks(chainOf(1), sine(1000.0, 0.05f), 60, &at)[0] - 0.05f) < 0.003f);
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
