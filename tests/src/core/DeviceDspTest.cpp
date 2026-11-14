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

#define private public // the regression needs a live mixer with the song pointer absent
#include "Engine.h"
#undef private

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
	void initTestCase();

	void cleanupTestCase();

	void init();

	void delayLineTemplatesInstantiate();

	//! Each channel keeps its own ring cursor, so a stereo frame's samples delay by the same amount.
	void stereoDelayPrimitivesAdvanceOneCursorPerChannel();

	//! OSS PCM output must saturate samples before converting them to signed 16-bit values.
	void ossConversionClipsOverrangeSamples();

	//! Full scale maps to the signed 16-bit limits without wrapping, including the first float above +1.0.
	void ossConversionFullScaleStaysInRange();

	//! A sample-rate drop must keep an existing LFO frequency at or below the new Nyquist limit.
	void quadratureLfoDoesNotExceedNyquistAfterSampleRateDrop();

	void rmsHelperRejectsZeroAtConstruction();

	void rmsHelperRejectsZeroResize();

	//! Growing the RMS window must release its new[] buffer with delete[].
	void growingRmsBufferUsesArrayDeallocation();

	void formantFilterClampsToLastVowel();

	//! The second formant's first stage feeds back its own band-pass history.
	void formantSecondStageUsesItsOwnHistory();

	void rejectedCombFeedbackDelaysPreserveThePreviousDelay();

	void zeroThresholdFoldbackDistortionBypassesSafely();

	void upsamplerSetupRejectsNegativeStages();

	void upsamplerSetStagesRejectsNegativeStages();

	void downsamplerSetupRejectsNegativeStages();

	void downsamplerSetStagesRejectsNegativeStages();

	void upsamplerSetStagesRejectsStagesAboveMaximum();

	void upsamplerSetupRejectsStagesAboveMaximum();

	void downsamplerSetStagesRejectsStagesAboveMaximum();

	void downsamplerSetupRejectsStagesAboveMaximum();

	void oversamplerStageRangeEndpointsRemainValid();

	void quadratureLfoRejectsNaNFrequency();

	void quadratureLfoRejectsZeroSampleRate();

	void quadratureLfoIgnoresZeroSampleRateUpdate();

	void quadratureLfoIgnoresNonFiniteOffset();

	void nonFiniteFormantFrequenciesUseMinimumFrequency();

	//! MX-147: a loud 9 kHz tone is turned down and reported as reduction; a 200 Hz tone passes.
	void theDeEsserTurnsDownTheHighBandOnly();

	//! MX-147: the detector is linked, so sibilance that is loud in BOTH channels is caught even when
	//! the two are opposite in phase. A mono-sum detector reads R = -L as silence and turns nothing
	//! down, so this is the same 9 kHz tone at the same level as the case above, phase-inverted on
	//! the right: it must be reduced just the same.
	void theDeEsserDetectsOppositePhaseSibilance();

	//! Mute routing must not let the dry mix term reach the output.
	void theFrequencyShifterMuteRouteDoesNotPassDryInput();

	//! MX-148: +100 % attack lifts the start of a burst; -100 % softens it; neither moves the steady level.
	void theTransientShaperShapesAnAttack();

	//! MX-154: with the voice on the left, the carrier on the right sounds only while the voice does.
	void theVocoderFollowsTheVoice();

	//! MX-143: transparent at rest; a static cut; a dynamic cut that only a loud band gets; mid only.
	void theDynamicEqCutsStaticallyDynamicallyAndOnTheMid();

	//! FFTW allocation and plan failures reject the kernel and release resources already acquired.
	void theConvolutionKernelRejectsFftwResourceFailures();

	//! MX-152: an impulse response of one click plays the input back one partition late at the
	//! input's level; a device that is not a convolution reverb, or a missing file, is refused; undo.
	void theConvolutionReverbPlaysALoadedImpulse();

	//! W6 (review lane10 #4): an impulse response recorded at twice the engine's rate is resampled band-limited. Its
	//! click plus a tone above the engine's Nyquist frequency must play as the click alone - flat - and not with
	//! the tone folded down to a quarter of the rate (the old linear resampler: about 7 times louder there).
	void aHighRateImpulseIsResampledWithoutAliasing();

	//! MX-156: the Recorder passes the sound unchanged and writes what passed to its take; the take lands
	//! on a new audio track as one undo step; a device that is not a Recorder is refused.
	void recorderLandRefusesWithoutSong();

	void emptyRecorderFileUsesDefaultTakePath();

	void theRecorderRecordsWhatPassesIt();

	//! MX-150: a quiet signal passes the console unchanged; mixer.console_apply adds one to every channel,
	//! changes them all on a second call, removes them, and each call undoes in one step.
	void theConsoleGoesOnEveryChannel();

	//! Luna 4760: removing consoles from a project that has none changed nothing, so the reply says
	//! zero and the project must not become dirty.
	void removingNoConsoleLeavesTheProjectClean();

	//! Luna 3872: 'changed' counts the settings that actually changed - applying the value a
	//! console already carries changes nothing and must not be reported as a change.
	void applyingTheValueAlreadyThereReportsNoChange();

	//! Luna new-p15-10: applying settings a console already carries changes nothing, so it must take no
	//! undo step and must not dirty a project that was saved clean.
	void applyingTheSameSettingsTakesNoUndoStepAndLeavesTheProjectClean();

	//! Luna 3873: one mixer.console_apply is ONE undo step however many channels it touched -
	//! the removal of every channel's console has to be a single step, not one per channel.
	void oneConsoleApplyIsOneUndoStep();

	//! A NaN drive passes the schema's number check, and every range comparison with NaN is false, so it
	//! used to pass the 0..100 check and reach the consoles. It is refused as InvalidArgs, and the
	//! console already on a channel keeps its drive.
	void aNaNDriveIsRefusedAndChangesNothing();
};

#include "DeviceDspTestParts1.h"
#include "DeviceDspTestParts2.h"

QTEST_MAIN(DeviceDspTest)
#include "DeviceDspTest.moc"
