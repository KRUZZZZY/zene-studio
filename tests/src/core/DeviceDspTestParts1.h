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

// Complete fixture method definitions; included once after the fixture declaration.

void DeviceDspTest::initTestCase() {
#ifdef LMMS_TEST_PLUGIN_DIR
	qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();  // the test reads and writes channel buffers itself
	while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
	block().assign(Engine::audioEngine()->framesPerPeriod(), SampleFrame());
	ControlRegistry::setReady(true);
}
void DeviceDspTest::cleanupTestCase() {
	ControlRegistry::setReady(false);
	Engine::destroy();
}
void DeviceDspTest::init() {
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { chainOf(i).clear(); }
}
void DeviceDspTest::delayLineTemplatesInstantiate() {
	lmms::CombFeedback<2> feedback{4};
	feedback.setDelay(1.0);
	feedback.setGain(0.5);
	QCOMPARE(feedback.update(0.0, 0), 0.0);

	lmms::AllpassDelay<2> allpass{4};
	allpass.setDelay(1.0);
	allpass.setGain(0.5);
	QCOMPARE(allpass.update(0.0, 0), 0.0);
}
void DeviceDspTest::stereoDelayPrimitivesAdvanceOneCursorPerChannel() {
	StereoCombFeedback comb(8);
	QVERIFY2(channelsStayAligned(comb, 3.0), "StereoCombFeedback must keep one cursor per channel");
	StereoCombFeedfwd feedforward(8);
	QVERIFY2(channelsStayAligned(feedforward, 3.0), "StereoCombFeedfwd must keep one cursor per channel");
	StereoAllpassDelay allpass(8);
	QVERIFY2(channelsStayAligned(allpass, 3.0), "StereoAllpassDelay must keep one cursor per channel");
	QVERIFY2(dualTapChannelsStayAligned(), "StereoCombFeedbackDualtap must keep one cursor per channel");
	QVERIFY2(shrinkingMaxDelayKeepsCursorsInRange(), "setMaxDelay must fold every channel cursor into the new size");
}
void DeviceDspTest::ossConversionClipsOverrangeSamples() {
	QCOMPARE(audio::oss::toPcmSample(1.25f), std::int16_t{32767});
	QCOMPARE(audio::oss::toPcmSample(-1.25f), std::int16_t{-32767});
	QCOMPARE(audio::oss::toPcmSample(0.5f), std::int16_t{16383});
	QCOMPARE(audio::oss::toPcmSample(std::numeric_limits<float>::quiet_NaN()), std::int16_t{0});
}
void DeviceDspTest::ossConversionFullScaleStaysInRange() {
	QCOMPARE(audio::oss::toPcmSample(1.0f), std::int16_t{32767});
	QCOMPARE(audio::oss::toPcmSample(-1.0f), std::int16_t{-32767});
	QCOMPARE(audio::oss::toPcmSample(std::nextafter(1.0f, 2.0f)), std::int16_t{32767});
	QCOMPARE(audio::oss::toPcmSample(std::nextafter(-1.0f, -2.0f)), std::int16_t{-32767});
}
void DeviceDspTest::quadratureLfoDoesNotExceedNyquistAfterSampleRateDrop() {
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
void DeviceDspTest::rmsHelperRejectsZeroAtConstruction() {
	QVERIFY_EXCEPTION_THROWN(RmsHelper(0), std::invalid_argument);
}
void DeviceDspTest::rmsHelperRejectsZeroResize() {
	RmsHelper rms{1};
	QVERIFY_EXCEPTION_THROWN(rms.setSize(0), std::invalid_argument);
	QCOMPARE(rms.update(2.0f), 2.0f);
}
void DeviceDspTest::growingRmsBufferUsesArrayDeallocation() {
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
void DeviceDspTest::formantFilterClampsToLastVowel() {
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
void DeviceDspTest::formantSecondStageUsesItsOwnHistory() {
	BasicFilters<1> filter{48000};
	filter.setFilterType(BasicFilters<1>::FilterType::Formantfilter);
	filter.calcFilterCoeffs(500.0f, 8.0f);
	filter.update(0.5f, 0);
	filter.update(0.0f, 0);
	const float output = filter.update(0.25f, 0);
	QVERIFY2(std::abs(output - 0.02071115f) < 1.0e-5f, qPrintable(QString::number(output, 'g', 9)));
}
void DeviceDspTest::rejectedCombFeedbackDelaysPreserveThePreviousDelay() {
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
void DeviceDspTest::zeroThresholdFoldbackDistortionBypassesSafely() {
	DspEffectLibrary::FoldbackDistortion foldback{0.0f, 1.5f};
	const sample_t output = foldback.nextSample(0.5f);
	QVERIFY(std::isfinite(output));
	QCOMPARE(output, 0.75f);
}
void DeviceDspTest::upsamplerSetupRejectsNegativeStages() {
	Upsampler<4> sampler;
	sampler.setup(2, 44100.f);
	sampler.setup(-1, 48000.f);
	QCOMPARE(sampler.getStages(), 2);
	QCOMPARE(sampler.getSampleRate(), 44100.f);
}
void DeviceDspTest::upsamplerSetStagesRejectsNegativeStages() {
	Upsampler<4> sampler;
	sampler.setup(2, 44100.f);
	sampler.setStages(-1);
	QCOMPARE(sampler.getStages(), 2);
}
void DeviceDspTest::downsamplerSetupRejectsNegativeStages() {
	Downsampler<4> sampler;
	sampler.setup(2, 44100.f);
	sampler.setup(-1, 48000.f);
	QCOMPARE(sampler.getStages(), 2);
	QCOMPARE(sampler.getSampleRate(), 44100.f);
}
void DeviceDspTest::downsamplerSetStagesRejectsNegativeStages() {
	Downsampler<4> sampler;
	sampler.setup(2, 44100.f);
	sampler.setStages(-1);
	QCOMPARE(sampler.getStages(), 2);
}
void DeviceDspTest::upsamplerSetStagesRejectsStagesAboveMaximum() {
	Upsampler<4> sampler;
	sampler.setup(2, 44100.f);
	sampler.setStages(5);
	QCOMPARE(sampler.getStages(), 2);
}
void DeviceDspTest::upsamplerSetupRejectsStagesAboveMaximum() {
	Upsampler<4> sampler;
	sampler.setup(2, 44100.f);
	sampler.setup(5, 48000.f);
	QCOMPARE(sampler.getStages(), 2);
	QCOMPARE(sampler.getSampleRate(), 44100.f);
}
void DeviceDspTest::downsamplerSetStagesRejectsStagesAboveMaximum() {
	Downsampler<4> sampler;
	sampler.setup(2, 44100.f);
	sampler.setStages(5);
	QCOMPARE(sampler.getStages(), 2);
}
void DeviceDspTest::downsamplerSetupRejectsStagesAboveMaximum() {
	Downsampler<4> sampler;
	sampler.setup(2, 44100.f);
	sampler.setup(5, 48000.f);
	QCOMPARE(sampler.getStages(), 2);
	QCOMPARE(sampler.getSampleRate(), 44100.f);
}
void DeviceDspTest::oversamplerStageRangeEndpointsRemainValid() {
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
void DeviceDspTest::quadratureLfoRejectsNaNFrequency() {
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
void DeviceDspTest::quadratureLfoRejectsZeroSampleRate() {
	QuadratureLfo lfo(0);
	float left = 0.0f;
	float right = 0.0f;
	lfo.tick(&left, &right);
	lfo.tick(&left, &right);
	QVERIFY(std::isfinite(left));
	QVERIFY(std::isfinite(right));
}
void DeviceDspTest::quadratureLfoIgnoresZeroSampleRateUpdate() {
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
void DeviceDspTest::quadratureLfoIgnoresNonFiniteOffset() {
	const double nonFiniteOffsets[] = {
		std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(),
		-std::numeric_limits<double>::infinity()};
	for (const double offset : nonFiniteOffsets)
	{
		QuadratureLfo lfo(48000);
		lfo.setFrequency(1.0);
		lfo.setOffset(0.0);
		lfo.setOffset(offset);
		lfo.restart();

		for (int i = 0; i < 8; ++i)
		{
			float left = 0.0f;
			float right = 0.0f;
			lfo.tick(&left, &right);
			QVERIFY(std::isfinite(right));
			QCOMPARE(right, left);
		}

		lfo.setOffset(std::numbers::pi);
		float left = 0.0f;
		float right = 0.0f;
		lfo.tick(&left, &right);
		QVERIFY(std::isfinite(right));
		QVERIFY(std::abs(right + left) < 1e-6f);
	}
}
void DeviceDspTest::nonFiniteFormantFrequenciesUseMinimumFrequency() {
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
void DeviceDspTest::theDeEsserTurnsDownTheHighBandOnly() {
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
void DeviceDspTest::theDeEsserDetectsOppositePhaseSibilance() {
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
void DeviceDspTest::theFrequencyShifterMuteRouteDoesNotPassDryInput() {
	const QString shifter = loadByName(ch(1), QStringLiteral("frequencyshifter"));
	QVERIFY2(!shifter.isEmpty(), "the frequency shifter module is available");
	set(ch(1), shifter, "Route Mode", 2.0);
	set(ch(1), shifter, "Mix", 0.5);

	long at = 0;
	const SampleFrame output = runBlocks(chainOf(1), [](long) { return SampleFrame(0.8f, -0.4f); }, 1, &at);
	QVERIFY2(output[0] < 1.0e-6f, qPrintable(QString::number(output[0])));
	QVERIFY2(output[1] < 1.0e-6f, qPrintable(QString::number(output[1])));
}
void DeviceDspTest::theTransientShaperShapesAnAttack() {
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
void DeviceDspTest::theVocoderFollowsTheVoice() {
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
void DeviceDspTest::theDynamicEqCutsStaticallyDynamicallyAndOnTheMid() {
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
void DeviceDspTest::theConvolutionKernelRejectsFftwResourceFailures() {
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
void DeviceDspTest::theConvolutionReverbPlaysALoadedImpulse() {
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
