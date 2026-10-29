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

#include "Engine.h"

namespace {
// The regression needs a live mixer with the song pointer absent. Engine::s_song is private; an
// explicit instantiation may name a private member, which reaches it without `#define private
// public` (MSVC mangles the access level, so that redefinition no longer links against lmmsobjs).
template <lmms::Song** Slot>
struct EngineSongSlot
{
	friend lmms::Song** engineSongSlot() { return Slot; }
};
lmms::Song** engineSongSlot();
template struct EngineSongSlot<&lmms::Engine::s_song>;
} // namespace

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <limits>
#include <limits>
#include <limits>
#include <vector>

#include <fftw3.h>

namespace
{
	enum class KernelFftwFailure { None, Allocation, Plan };
	KernelFftwFailure g_kernelFftwFailure = KernelFftwFailure::None;
	int g_kernelFftwFailureAt = -1;
	int g_kernelFftwAllocationCalls = 0;
	int g_kernelFftwPlanCalls = 0;
	int g_kernelFftwLiveAllocations = 0;
	int g_kernelFftwLivePlans = 0;
}

extern "C"
{
	void* fftwf_mallocForKernelTest(size_t size)
	{
		const int call = g_kernelFftwAllocationCalls++;
		if (g_kernelFftwFailure == KernelFftwFailure::Allocation && call == g_kernelFftwFailureAt) { return nullptr; }
		void* allocation = std::calloc(1, size);
		if (allocation != nullptr) { ++g_kernelFftwLiveAllocations; }
		return allocation;
	}

	void fftwf_freeForKernelTest(void* allocation)
	{
		if (allocation != nullptr)
		{
			--g_kernelFftwLiveAllocations;
			std::free(allocation);
		}
	}

	fftwf_plan fftwf_plan_dft_r2c_1dForKernelTest(int, float* input, fftwf_complex* output, unsigned)
	{
		const int call = g_kernelFftwPlanCalls++;
		if (input == nullptr || output == nullptr ||
			(g_kernelFftwFailure == KernelFftwFailure::Plan && call == g_kernelFftwFailureAt)) { return nullptr; }
		++g_kernelFftwLivePlans;
		return reinterpret_cast<fftwf_plan>(static_cast<std::uintptr_t>(call + 1));
	}

	fftwf_plan fftwf_plan_dft_c2r_1dForKernelTest(int, fftwf_complex* input, float* output, unsigned)
	{
		const int call = g_kernelFftwPlanCalls++;
		if (input == nullptr || output == nullptr ||
			(g_kernelFftwFailure == KernelFftwFailure::Plan && call == g_kernelFftwFailureAt)) { return nullptr; }
		++g_kernelFftwLivePlans;
		return reinterpret_cast<fftwf_plan>(static_cast<std::uintptr_t>(call + 1));
	}

	void fftwf_executeForKernelTest(const fftwf_plan) {}

	void fftwf_destroy_planForKernelTest(fftwf_plan plan)
	{
		if (plan != nullptr) { --g_kernelFftwLivePlans; }
	}
}

#define ConvolutionKernel ConvolutionKernelAllocationTest
#define fftwf_malloc fftwf_mallocForKernelTest
#define fftwf_free fftwf_freeForKernelTest
#define fftwf_plan_dft_r2c_1d fftwf_plan_dft_r2c_1dForKernelTest
#define fftwf_plan_dft_c2r_1d fftwf_plan_dft_c2r_1dForKernelTest
#define fftwf_execute fftwf_executeForKernelTest
#define fftwf_destroy_plan fftwf_destroy_planForKernelTest
#include "../../../plugins/ConvolutionReverb/ConvolutionKernel.cpp"
#undef fftwf_destroy_plan
#undef fftwf_execute
#undef fftwf_plan_dft_c2r_1d
#undef fftwf_plan_dft_r2c_1d
#undef fftwf_free
#undef fftwf_malloc
#undef ConvolutionKernel

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BasicFilters.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Delay.h"
#include "DspEffectLibrary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "QuadratureLfo.h"
#include "RmsHelper.h"
#include "OversamplingHelpers.h"
#include "QuadratureLfo.h"
#include "SampleFrame.h"
#include "Song.h"
#include "../../../src/core/audio/OssSampleConversion.h"

using namespace lmms;

namespace
{

void armKernelFftwFailure(KernelFftwFailure failure, int call)
{
	g_kernelFftwFailure = failure;
	g_kernelFftwFailureAt = call;
	g_kernelFftwAllocationCalls = 0;
	g_kernelFftwPlanCalls = 0;
}

void constructKernelForFftwFailureTest()
{
	const std::vector<float> emptyImpulse;
	lmms::ConvolutionKernelAllocationTest kernel(emptyImpulse, emptyImpulse);
}

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

constexpr int kDelayProbeFrames = 8;

bool nearly(double a, double b) { return std::abs(a - b) < 1.0e-9; }

//! Eight stereo frames through @a delay: an impulse of 1 on the left and of 10 on the right in frame 0.
template<typename Delay>
void stereoImpulsePair(Delay& delay, double left[kDelayProbeFrames], double right[kDelayProbeFrames])
{
	for (int frame = 0; frame < kDelayProbeFrames; ++frame)
	{
		left[frame] = delay.update(frame == 0 ? 1.0 : 0.0, 0);
		right[frame] = delay.update(frame == 0 ? 10.0 : 0.0, 1);
	}
}

//! A ring cursor shared between the channels walks the buffer twice per frame, so the two impulse answers
//! stop being ten times each other and land before the configured delay.
template<typename Delay>
bool channelsStayAligned(Delay& delay, double delayTime)
{
	delay.setDelay(delayTime);
	delay.setGain(0.0);
	double left[kDelayProbeFrames];
	double right[kDelayProbeFrames];
	stereoImpulsePair(delay, left, right);
	const int answered = static_cast<int>(delayTime);
	for (int frame = 0; frame < kDelayProbeFrames; ++frame)
	{
		if (!nearly(left[frame] * 10.0, right[frame])) { return false; }
		if (frame < answered && !nearly(left[frame], 0.0)) { return false; }
	}
	return nearly(left[answered], 1.0) && nearly(right[answered], 10.0);
}

//! Both taps of a dual-tap comb must answer on the left channel exactly as they do on the right.
bool dualTapChannelsStayAligned()
{
	StereoCombFeedbackDualtap dualtap(12);
	dualtap.setDelays(2.0, 5.0);
	dualtap.setGain(0.0);
	double left[kDelayProbeFrames];
	double right[kDelayProbeFrames];
	stereoImpulsePair(dualtap, left, right);
	return nearly(left[2], 1.0) && nearly(right[2], 10.0) && nearly(left[5], 1.0) && nearly(right[5], 10.0);
}

//! A shrinking setMaxDelay folds every channel cursor into the new size.
bool shrinkingMaxDelayKeepsCursorsInRange()
{
	StereoCombFeedback comb(8);
	comb.setDelay(2.0);
	comb.setGain(0.0);
	for (int frame = 0; frame < 6; ++frame) { comb.update(0.0, 0); comb.update(0.0, 1); }
	comb.setMaxDelay(4);
	double left[kDelayProbeFrames];
	double right[kDelayProbeFrames];
	stereoImpulsePair(comb, left, right);
	return nearly(left[2], 1.0) && nearly(right[2], 10.0);
}

} // namespace

template class lmms::CombFeedback<2>;
template class lmms::CombFeedfwd<2>;
template class lmms::CombFeedbackDualtap<2>;
template class lmms::AllpassDelay<2>;

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

	void delayLineTemplatesInstantiate()
	{
		lmms::CombFeedback<2> feedback{4};
		feedback.setDelay(1.0);
		feedback.setGain(0.5);
		QCOMPARE(feedback.update(0.0, 0), 0.0);

		lmms::AllpassDelay<2> allpass{4};
		allpass.setDelay(1.0);
		allpass.setGain(0.5);
		QCOMPARE(allpass.update(0.0, 0), 0.0);
	}

	//! Each channel keeps its own ring cursor, so a stereo frame's samples delay by the same amount.
	void stereoDelayPrimitivesAdvanceOneCursorPerChannel()
	{
		StereoCombFeedback comb(8);
		QVERIFY2(channelsStayAligned(comb, 3.0), "StereoCombFeedback must keep one cursor per channel");
		StereoCombFeedfwd feedforward(8);
		QVERIFY2(channelsStayAligned(feedforward, 3.0), "StereoCombFeedfwd must keep one cursor per channel");
		StereoAllpassDelay allpass(8);
		QVERIFY2(channelsStayAligned(allpass, 3.0), "StereoAllpassDelay must keep one cursor per channel");
		QVERIFY2(dualTapChannelsStayAligned(), "StereoCombFeedbackDualtap must keep one cursor per channel");
		QVERIFY2(shrinkingMaxDelayKeepsCursorsInRange(), "setMaxDelay must fold every channel cursor into the new size");
	}

	//! OSS PCM output must saturate samples before converting them to signed 16-bit values.
	void ossConversionClipsOverrangeSamples()
	{
		QCOMPARE(audio::oss::toPcmSample(1.25f), std::int16_t{32767});
		QCOMPARE(audio::oss::toPcmSample(-1.25f), std::int16_t{-32767});
		QCOMPARE(audio::oss::toPcmSample(0.5f), std::int16_t{16383});
		QCOMPARE(audio::oss::toPcmSample(std::numeric_limits<float>::quiet_NaN()), std::int16_t{0});
	}

	//! Full scale maps to the signed 16-bit limits without wrapping, including the first float above +1.0.
	void ossConversionFullScaleStaysInRange()
	{
		QCOMPARE(audio::oss::toPcmSample(1.0f), std::int16_t{32767});
		QCOMPARE(audio::oss::toPcmSample(-1.0f), std::int16_t{-32767});
		QCOMPARE(audio::oss::toPcmSample(std::nextafter(1.0f, 2.0f)), std::int16_t{32767});
		QCOMPARE(audio::oss::toPcmSample(std::nextafter(-1.0f, -2.0f)), std::int16_t{-32767});
	}

	//! A sample-rate drop must keep an existing LFO frequency at or below the new Nyquist limit.
	void quadratureLfoDoesNotExceedNyquistAfterSampleRateDrop()
	{
		QuadratureLfo lfo(48000);
		lfo.setFrequency(20000.0);
		lfo.setSampleRate(32000);
		lfo.restart();
		float left;
		float right;
		lfo.tick(&left, &right);
		lfo.tick(&left, &right);
		QVERIFY2(std::abs(left) < 1.0e-5f, qPrintable(QString::number(left)));
	}

	void rmsHelperRejectsZeroAtConstruction()
	{
		QVERIFY_EXCEPTION_THROWN(RmsHelper(0), std::invalid_argument);
	}

	void rmsHelperRejectsZeroResize()
	{
		RmsHelper rms{1};
		QVERIFY_EXCEPTION_THROWN(rms.setSize(0), std::invalid_argument);
		QCOMPARE(rms.update(2.0f), 2.0f);
	}

	//! Growing the RMS window must release its new[] buffer with delete[].
	void growingRmsBufferUsesArrayDeallocation()
	{
		test::resetMismatchedArrayReleases();
		test::tlUntrackedArrayBlocks = 0;
		test::tlTrackArrayBlocks = true;
		RmsHelper rms{4};
		// Keep the inline resize opaque so compilers report the defect at runtime.
		void (RmsHelper::* volatile resize)(std::size_t) = &RmsHelper::setSize;
		(rms.*resize)(8);
		test::tlTrackArrayBlocks = false;

		QVERIFY2(test::tlMismatchedArrayReleases == 0,
			"RmsHelper::setSize() released a new[] window with scalar delete");
		QCOMPARE(test::tlUntrackedArrayBlocks, std::uint64_t{0});
		QCOMPARE(rms.update(0.0f), 0.0f);
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

	//! The second formant's first stage feeds back its own band-pass history.
	void formantSecondStageUsesItsOwnHistory()
	{
		BasicFilters<1> filter{48000};
		filter.setFilterType(BasicFilters<1>::FilterType::Formantfilter);
		filter.calcFilterCoeffs(500.0f, 8.0f);
		filter.update(0.5f, 0);
		filter.update(0.0f, 0);
		const float output = filter.update(0.25f, 0);
		QVERIFY2(std::abs(output - 0.02071115f) < 1.0e-5f, qPrintable(QString::number(output, 'g', 9)));
	}

	void rejectedCombFeedbackDelaysPreserveThePreviousDelay()
	{
		CombFeedback<1> delay{4};
		const double intOverflow = static_cast<double>(std::numeric_limits<int>::max()) + 1.0;
		const std::array<double, 5> invalidDelays{
			-1.0,
			-0.25,
			std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity(),
			intOverflow
		};

		for (const double invalidDelay : invalidDelays)
		{
			delay.clearHistory();
			delay.setDelay(1.0);
			delay.setDelay(invalidDelay);
			QCOMPARE(delay.update(1.0, 0), 0.0);
			QCOMPARE(delay.update(0.0, 0), 1.0);
		}
	}

	void zeroThresholdFoldbackDistortionBypassesSafely()
	{
		DspEffectLibrary::FoldbackDistortion foldback{0.0f, 1.5f};
		const sample_t output = foldback.nextSample(0.5f);
		QVERIFY(std::isfinite(output));
		QCOMPARE(output, 0.75f);
	}

	void upsamplerSetupRejectsNegativeStages()
	{
		Upsampler<4> sampler;
		sampler.setup(2, 44100.f);
		sampler.setup(-1, 48000.f);
		QCOMPARE(sampler.getStages(), 2);
		QCOMPARE(sampler.getSampleRate(), 44100.f);
	}

	void upsamplerSetStagesRejectsNegativeStages()
	{
		Upsampler<4> sampler;
		sampler.setup(2, 44100.f);
		sampler.setStages(-1);
		QCOMPARE(sampler.getStages(), 2);
	}

	void downsamplerSetupRejectsNegativeStages()
	{
		Downsampler<4> sampler;
		sampler.setup(2, 44100.f);
		sampler.setup(-1, 48000.f);
		QCOMPARE(sampler.getStages(), 2);
		QCOMPARE(sampler.getSampleRate(), 44100.f);
	}

	void downsamplerSetStagesRejectsNegativeStages()
	{
		Downsampler<4> sampler;
		sampler.setup(2, 44100.f);
		sampler.setStages(-1);
		QCOMPARE(sampler.getStages(), 2);
	}

	void upsamplerSetStagesRejectsStagesAboveMaximum()
	{
		Upsampler<4> sampler;
		sampler.setup(2, 44100.f);
		sampler.setStages(5);
		QCOMPARE(sampler.getStages(), 2);
	}

	void upsamplerSetupRejectsStagesAboveMaximum()
	{
		Upsampler<4> sampler;
		sampler.setup(2, 44100.f);
		sampler.setup(5, 48000.f);
		QCOMPARE(sampler.getStages(), 2);
		QCOMPARE(sampler.getSampleRate(), 44100.f);
	}

	void downsamplerSetStagesRejectsStagesAboveMaximum()
	{
		Downsampler<4> sampler;
		sampler.setup(2, 44100.f);
		sampler.setStages(5);
		QCOMPARE(sampler.getStages(), 2);
	}

	void downsamplerSetupRejectsStagesAboveMaximum()
	{
		Downsampler<4> sampler;
		sampler.setup(2, 44100.f);
		sampler.setup(5, 48000.f);
		QCOMPARE(sampler.getStages(), 2);
		QCOMPARE(sampler.getSampleRate(), 44100.f);
	}

	void oversamplerStageRangeEndpointsRemainValid()
	{
		Upsampler<4> upsampler;
		Downsampler<4> downsampler;
		upsampler.setup(0, 44100.f);
		downsampler.setup(0, 44100.f);
		QCOMPARE(upsampler.getStages(), 0);
		QCOMPARE(downsampler.getStages(), 0);
		upsampler.setStages(4);
		downsampler.setStages(4);
		QCOMPARE(upsampler.getStages(), 4);
		QCOMPARE(downsampler.getStages(), 4);
	}

	void quadratureLfoRejectsNaNFrequency()
	{
		QuadratureLfo lfo(48000);
		lfo.setFrequency(1.0);
		lfo.setFrequency(std::numeric_limits<double>::quiet_NaN());
		lfo.restart();

		float left = 0.0f;
		float right = 0.0f;
		lfo.tick(&left, &right);
		lfo.tick(&left, &right);
		QVERIFY(std::isfinite(left));
		QVERIFY(std::isfinite(right));
	}

	void quadratureLfoRejectsZeroSampleRate()
	{
		QuadratureLfo lfo(0);
		float left = 0.0f;
		float right = 0.0f;
		lfo.tick(&left, &right);
		lfo.tick(&left, &right);
		QVERIFY(std::isfinite(left));
		QVERIFY(std::isfinite(right));
	}

	void quadratureLfoIgnoresZeroSampleRateUpdate()
	{
		QuadratureLfo lfo(48000);
		lfo.setFrequency(1.0);
		lfo.setSampleRate(0);
		float left = 0.0f;
		float right = 0.0f;
		lfo.tick(&left, &right);
		lfo.tick(&left, &right);
		QVERIFY(std::isfinite(left));
		QVERIFY(std::isfinite(right));
	}

	void nonFiniteFormantFrequenciesUseMinimumFrequency()
	{
		using Filters = BasicFilters<2>;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		for (const auto type : {Filters::FilterType::Formantfilter, Filters::FilterType::FastFormant})
		{
			Filters actual(48000);
			Filters expected(48000);
			actual.setFilterType(type);
			expected.setFilterType(type);
			actual.calcFilterCoeffs(nan, 0.5f);
			expected.calcFilterCoeffs(Filters::minFreq(), 0.5f);

			for (const float input : {0.1f, -0.2f, 0.45f})
			{
				const float output = actual.update(input, 0);
				const float expectedOutput = expected.update(input, 0);
				QVERIFY(std::isfinite(output));
				QVERIFY(std::abs(output - expectedOutput) < 1e-6f);
			}
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

	//! MX-147: the detector is linked, so sibilance that is loud in BOTH channels is caught even when
	//! the two are opposite in phase. A mono-sum detector reads R = -L as silence and turns nothing
	//! down, so this is the same 9 kHz tone at the same level as the case above, phase-inverted on
	//! the right: it must be reduced just the same.
	void theDeEsserDetectsOppositePhaseSibilance()
	{
		const QString deesser = loadByName(ch(1), QStringLiteral("deesser"));
		if (deesser.isEmpty()) { QSKIP("this build has no de-esser module"); }
		set(ch(1), deesser, "Threshold", -40.0);
		set(ch(1), deesser, "Range", 12.0);
		// The same tone as the case above with the right channel inverted: the mono sum is zero.
		const Signal opposed = [](long f) {
			const auto x = static_cast<float>(0.5 * std::sin(kTwoPi * 9000.0 * static_cast<double>(f) / rate()));
			return SampleFrame(x, -x);
		};
		long at = 0;
		const SampleFrame high = runBlocks(chainOf(1), opposed, 60, &at);
		QVERIFY2(high[0] < 0.5f * 0.5f, qPrintable(QString("opposite-phase sibilance was not "
			"detected: left peak %1 (0.5 is untouched, 0.25 is the 6 dB the case above gets)")
			.arg(high[0])));
		QVERIFY2(chainOf(1).effects().front()->gainReductionDb() > 6.f,
			qPrintable(QString("no reduction reported for opposite-phase sibilance: %1 dB")
				.arg(chainOf(1).effects().front()->gainReductionDb())));
		// The lows are still left alone: a linked detector must not duck the whole spectrum.
		at = 0;
		const Signal opposedLow = [](long f) {
			const auto x = static_cast<float>(0.5 * std::sin(kTwoPi * 200.0 * static_cast<double>(f) / rate()));
			return SampleFrame(x, -x);
		};
		const SampleFrame low = runBlocks(chainOf(1), opposedLow, 60, &at);
		QVERIFY2(std::abs(low[0] - 0.5f) < 0.03f, qPrintable(QString::number(low[0])));
	}

	//! Mute routing must not let the dry mix term reach the output.
	void theFrequencyShifterMuteRouteDoesNotPassDryInput()
	{
		const QString shifter = loadByName(ch(1), QStringLiteral("frequencyshifter"));
		QVERIFY2(!shifter.isEmpty(), "the frequency shifter module is available");
		set(ch(1), shifter, "Route Mode", 2.0);
		set(ch(1), shifter, "Mix", 0.5);

		long at = 0;
		const SampleFrame output = runBlocks(chainOf(1), [](long) { return SampleFrame(0.8f, -0.4f); }, 1, &at);
		QVERIFY2(output[0] < 1.0e-6f, qPrintable(QString::number(output[0])));
		QVERIFY2(output[1] < 1.0e-6f, qPrintable(QString::number(output[1])));
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

	//! FFTW allocation and plan failures reject the kernel and release resources already acquired.
	void theConvolutionKernelRejectsFftwResourceFailures()
	{
		armKernelFftwFailure(KernelFftwFailure::Allocation, 3);  // the fourth FFTW buffer
		QVERIFY_EXCEPTION_THROWN(constructKernelForFftwFailureTest(), std::bad_alloc);
		QCOMPARE(g_kernelFftwLiveAllocations, 0);
		QCOMPARE(g_kernelFftwLivePlans, 0);

		armKernelFftwFailure(KernelFftwFailure::Plan, 1);  // the inverse plan
		QVERIFY_EXCEPTION_THROWN(constructKernelForFftwFailureTest(), std::bad_alloc);
		QCOMPARE(g_kernelFftwLiveAllocations, 0);
		QCOMPARE(g_kernelFftwLivePlans, 0);
		g_kernelFftwFailure = KernelFftwFailure::None;
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
	void recorderLandRefusesWithoutSong()
	{
		const QString recorder = loadByName(ch(1), QStringLiteral("insertrecorder"));
		if (recorder.isEmpty()) { QSKIP("this build has no recorder module"); }
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), recorder}};
		QJsonObject start = device;
		start.insert(QStringLiteral("on"), true);
		start.insert(QStringLiteral("file"), dir.filePath(QStringLiteral("take.wav")));
		const ControlResult started = run(QStringLiteral("plugin.record"), start);
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QJsonObject stop = device;
		stop.insert(QStringLiteral("on"), false);
		QVERIFY2(run(QStringLiteral("plugin.record"), stop).ok, "could not finish the test take");

		QVERIFY(*engineSongSlot() != nullptr);
		const ControlCommand* command = ControlRegistry::instance()->command(QStringLiteral("plugin.recorder_land"));
		QVERIFY(command != nullptr);
		const ControlResult landed = [&]()
		{
			Song* song = *engineSongSlot();
			*engineSongSlot() = nullptr;
			// The public registry rejects a missing song before calling the handler; test the handler's
			// own no-song safety directly so this failure path is covered too.
			const ControlResult result = command->handler(device);
			*engineSongSlot() = song;
			return result;
		}();
		QCOMPARE(landed.errorKind, ControlErrorKind::Refused);
		QVERIFY(landed.errorMessage.contains(QStringLiteral("song"), Qt::CaseInsensitive));
	}

	void emptyRecorderFileUsesDefaultTakePath()
	{
		const QString recorder = loadByName(ch(1), QStringLiteral("insertrecorder"));
		if (recorder.isEmpty()) { QSKIP("this build has no recorder module"); }
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		ConfigManager* config = ConfigManager::inst();
		struct RestoreWorkingDir
		{
			QString path;
			~RestoreWorkingDir() { ConfigManager::inst()->setWorkingDir(path); }
		} restore{config->workingDir()};
		config->setWorkingDir(dir.path());

		const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), recorder}};
		QJsonObject start = device;
		start.insert(QStringLiteral("on"), true);
		start.insert(QStringLiteral("file"), QString());
		const ControlResult started = run(QStringLiteral("plugin.record"), start);
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		const QString take = started.result.value(QStringLiteral("take")).toString();
		QVERIFY(!take.isEmpty());
		QCOMPARE(QFileInfo(take).absolutePath(), QDir(config->userSamplesDir()).filePath(QStringLiteral("Recordings")));
		QVERIFY(QFileInfo(take).fileName().startsWith(QStringLiteral("Take ")));

		QJsonObject stop = device;
		stop.insert(QStringLiteral("on"), false);
		const ControlResult stopped = run(QStringLiteral("plugin.record"), stop);
		QVERIFY2(stopped.ok, qPrintable(stopped.errorMessage));
		QVERIFY(!stopped.result.value(QStringLiteral("recording")).toBool());
		QVERIFY(QFileInfo::exists(take));
	}

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

	//! Luna 4760: removing consoles from a project that has none changed nothing, so the reply says
	//! zero and the project must not become dirty.
	void removingNoConsoleLeavesTheProjectClean()
	{
		const QString console = loadByName(ch(1), QStringLiteral("console"));
		if (console.isEmpty()) { QSKIP("this build has no console module"); }
		QVERIFY(run(QStringLiteral("project.new")).ok);
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { chainOf(i).clear(); }
		QVERIFY(!Engine::getSong()->isModified());

		const ControlResult removed = run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("remove"), true}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("removed")).toInt(), 0);
		QVERIFY2(!Engine::getSong()->isModified(),
			"removing no console dirtied a project whose content did not change");
	}

	//! Luna 3872: 'changed' counts the settings that actually changed - applying the value a
	//! console already carries changes nothing and must not be reported as a change.
	void applyingTheValueAlreadyThereReportsNoChange()
	{
		const QString console = loadByName(ch(1), QStringLiteral("console"));
		if (console.isEmpty()) { QSKIP("this build has no console module"); }
		QVERIFY(run(QStringLiteral("project.new")).ok);
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { chainOf(i).clear(); }
		QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 40.0}}).ok);
		const QString onTwo = control::effectIdOf(chainOf(2).effects().back());

		const ControlResult again = run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 40.0}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QCOMPARE(again.result.value(QStringLiteral("added")).toInt(), 0);
		QCOMPARE(parameterValue(ch(2), onTwo, QStringLiteral("Drive")), 40.0);
		QVERIFY2(again.result.value(QStringLiteral("changed")).toInt() == 0,
			"a console already carrying the requested drive was reported as changed");
	}

	//! Luna new-p15-10: applying settings a console already carries changes nothing, so it must take no
	//! undo step and must not dirty a project that was saved clean.
	void applyingTheSameSettingsTakesNoUndoStepAndLeavesTheProjectClean()
	{
		const QString console = loadByName(ch(1), QStringLiteral("console"));
		if (console.isEmpty()) { QSKIP("this build has no console module"); }
		QVERIFY(run(QStringLiteral("project.new")).ok);
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { chainOf(i).clear(); }
		QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 40.0}}).ok);

		Song* song = Engine::getSong();
		ProjectJournal* journal = Engine::projectJournal();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString fixtureFile = directory.filePath(QStringLiteral("console-same.mmp"));
		QVERIFY(song->saveProjectFile(fixtureFile, false));
		song->noteProjectSaved(fixtureFile);
		QVERIFY(!song->isModified());
		const int undoDepth = journal->undoDepth();

		const ControlResult same = run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 40.0}});
		QVERIFY2(same.ok, qPrintable(same.errorMessage));
		QCOMPARE(same.result.value(QStringLiteral("added")).toInt(), 0);
		QCOMPARE(same.result.value(QStringLiteral("changed")).toInt(), 0);
		QCOMPARE(journal->undoDepth(), undoDepth);
		QVERIFY2(!song->isModified(), "applying unchanged console settings dirtied the project");
	}

	//! Luna 3873: one mixer.console_apply is ONE undo step however many channels it touched -
	//! the removal of every channel's console has to be a single step, not one per channel.
	void oneConsoleApplyIsOneUndoStep()
	{
		const QString console = loadByName(ch(1), QStringLiteral("console"));
		if (console.isEmpty()) { QSKIP("this build has no console module"); }
		QVERIFY(run(QStringLiteral("project.new")).ok);
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		const int channels = Engine::mixer()->numChannels();
		for (int i = 0; i < channels; ++i) { chainOf(i).clear(); }
		QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 30.0}}).ok);
		QCOMPARE(consolesOn(2), 1);

		QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("remove"), true}}).ok);
		QCOMPARE(consolesOn(0), 0);
		QVERIFY2(run(QStringLiteral("control.undo")).ok, "one control.undo after the removal failed");
		for (int i = 0; i < channels; ++i)
		{
			QVERIFY2(consolesOn(i) == 1,
				qPrintable(QStringLiteral("one undo put the console back on only some of the %1 channels").arg(channels)));
		}
	}
};

QTEST_MAIN(DeviceDspTest)
#include "DeviceDspTest.moc"
