/*
 * DelayLfoAutomationTest.cpp - regression: an automated LFO amount must mean
 * the same thing as a static one (seconds of delay modulation, converted to
 * samples on the audio thread), not raw seconds misread as a sample count.
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

/*
 * The oracle: wet=1 / dry=0 / feedback=0 turns the effect into a pure delay
 * line, and the input is an exact ramp of slope RampSlope (a power of two, so
 * every value is float-exact). The wet output at frame g is then exactly
 * RampSlope * (g - D(g)) where D(g) is the integer delay length in samples at
 * that frame, so `out - RampSlope*g` is a direct readout of D(g). Its
 * min-max spread over two full LFO periods is 2 * amount * sampleRate no
 * matter what phase the freshly constructed Lfo starts at:
 *
 *  - static knob:   D sweeps +/- amount*sampleRate  -> spread is large;
 *  - automated knob (constant ramp, the block's own valueBuffer path): with
 *    the conversion applied per sample it sweeps the SAME range; without it
 *    (the bug this test pins down) the modulation is sub-sample, the integer
 *    read index can barely move, and the spread collapses to ~1 ramp step.
 *
 * Two DelayEffect instances are rendered: one where the amount is a plain
 * model value (valueBuffer() returns nullptr - the static path), one where
 * every period publishes a constant AutomationRamp for the amount (the
 * sample-exact path the engine takes when the knob is automated). Their
 * spreads must match, which also fails on a partial fix that converts the
 * automated value by the wrong factor.
 *
 * The same oracle reads the LFO PERIOD: the residual is the delay length in
 * samples, and the delay length follows the LFO, so the residual is a sine
 * whose frequency IS the LFO frequency. The model holds the period in
 * seconds, so an automated period of 2 s must modulate at 0.5 Hz - the same
 * frequency the static path produces for the same control value. Reading the
 * period as a frequency (2 Hz) is the bug this second case pins down: the
 * two probe frequencies are exactly orthogonal over a whole number of
 * seconds, so each probe sees only its own tone.
 */

#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

#include "AudioBus.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "AutomationRamp.h"
#include "DelayEffect.h"
#include "Effect.h"
#include "EffectControls.h"
#include "Engine.h"
#include "SampleFrame.h"

namespace lmms {

namespace {

constexpr float LfoAmountSeconds = 0.2f; // LfoAmount model value, seconds
constexpr float RampSlope = 1.0f / 8192.0f; // power of two: float-exact ramp
constexpr double MinDeepSpread = 0.5; // one ramp step is ~1.2e-4; a full
// 2 * 0.2 * 44100 sweep is ~2.1 - anything near 1e-4 is the sub-sample bug.
constexpr float LfoPeriodSeconds = 2.0f; // LfoFrequency model value, seconds
constexpr double PeriodHz = 0.5; // 1 / LfoPeriodSeconds: what the period means
constexpr double PeriodAsFrequencyHz = 2.0; // the period read as a frequency

// The Delay settings every render shares: wet only, no feedback, 1 Hz LFO
// (LfoFrequency is the LFO period in seconds), amount fixed at 0.2 s. The
// round trip mirrors the project load path the migration harness uses: the
// controls serialise their defaults first, the overrides are written onto the
// controls element, and Effect::loadSettings reads on/wet/autoquit off the
// root and the models off the controls child.
void configure(Effect& fx, float lfoPeriodSeconds = 1.0f)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("effect"));
	root.setAttribute(QStringLiteral("on"), 1);
	root.setAttribute(QStringLiteral("wet"), QString::number(1.0, 'g', 9));
	root.setAttribute(QStringLiteral("autoquit"), 0);

	QDomElement controls = fx.controls()->saveState(doc, root);
	controls.setAttribute(QStringLiteral("DelayTimeSamples"), QStringLiteral("0.5"));
	controls.setAttribute(QStringLiteral("FeebackAmount"), QStringLiteral("0"));
	controls.setAttribute(QStringLiteral("LfoFrequency"),
		QString::number(static_cast<double>(lfoPeriodSeconds), 'g', 9));
	controls.setAttribute(QStringLiteral("LfoAmount"), QStringLiteral("0.2"));
	controls.setAttribute(QStringLiteral("OutGain"), QStringLiteral("0"));

	fx.loadSettings(root);
}

AutomatableModel* findModel(Effect& fx, const QString& displayName)
{
	const auto models = fx.controls()->findChildren<AutomatableModel*>();
	for (auto* model : models)
	{
		if (model->displayName() == displayName) { return model; }
	}
	return nullptr;
}

// The first valueBuffer() after loadSettings() settles the pending
// interpolation setInitValue left behind, so every render period reads the
// same static nullptr an unautomated control hands the audio thread.
void flushPendingInterpolation(AutomatableModel& model)
{
	(void)model.valueBuffer();
}

// Renders warmup+measure frames in engine-sized blocks. When `automated` is
// set, each period publishes a constant ramp of `automatedValue` for it
// before processing - the block the automation evaluation would publish for a
// knob held at a value.
std::vector<float> renderWetRamp(DelayEffect& fx, AutomatableModel* automated,
	float automatedValue, f_cnt_t warmup, f_cnt_t measure)
{
	const f_cnt_t blockSize = Engine::audioEngine()->framesPerPeriod();
	const f_cnt_t total = warmup + measure;
	std::vector<SampleFrame> data(blockSize);
	std::vector<float> out;
	out.reserve(measure);

	for (f_cnt_t done = 0; done < total; done += blockSize)
	{
		const f_cnt_t frames = std::min(blockSize, total - done);
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			const float v = RampSlope * static_cast<float>(done + f);
			data[f].left() = v;
			data[f].right() = v;
		}

		AutomatableModel::incrementPeriodCounter();
		if (automated != nullptr)
		{
			AutomationRamp ramp;
			ramp.setBlockValue(automatedValue);
			automated->publishAutomationRamp(ramp);
		}

		SampleFrame* busPointers[1] = {data.data()};
		AudioBus bus{busPointers, 1, frames};
		// AudioPlugin's AudioBus override is protected; the public entry point
		// is Effect's (this is how the production chain and the migration
		// harness reach it).
		Effect& eff = fx;
		eff.processAudioBuffer(bus);

		for (f_cnt_t f = 0; f < frames; ++f)
		{
			const f_cnt_t g = done + f;
			if (g >= warmup) { out.push_back(data[f].left()); }
		}
	}
	return out;
}

// The min-max spread of the residual out - RampSlope*g over the measure
// region: exactly RampSlope * (maxD - minD) for the delay lengths the LFO
// visited, in ramp units.
double residualSpread(const std::vector<float>& out, f_cnt_t warmup)
{
	double lo = std::numeric_limits<double>::max();
	double hi = std::numeric_limits<double>::lowest();
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		const double g = static_cast<double>(warmup + i);
		const double r = static_cast<double>(out[i]) - static_cast<double>(RampSlope) * g;
		lo = std::min(lo, r);
		hi = std::max(hi, r);
	}
	return hi - lo;
}

// The amplitude of the tone at `hz` in the residual out - RampSlope*g, i.e.
// the delay length in samples filtered at that frequency. The residual is
// the delay length, and the delay length follows the LFO, so the residual IS
// the LFO. The measurement window is a whole number of seconds, so the two
// probe frequencies (0.5 Hz and 2 Hz) are exactly orthogonal: each probe
// sees only its own tone, whatever phase the LFO starts at.
double toneAmplitude(const std::vector<float>& out, f_cnt_t warmup, double sr, double hz)
{
	const double count = static_cast<double>(out.size());
	double re = 0.0;
	double im = 0.0;
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		const double g = static_cast<double>(warmup + i);
		const double r = static_cast<double>(out[i]) - static_cast<double>(RampSlope) * g;
		const double phase = 2.0 * std::numbers::pi_v<double> * hz
			* static_cast<double>(i) / sr;
		re += r * std::cos(phase);
		im += r * std::sin(phase);
	}
	return 2.0 * std::hypot(re, im) / count;
}

} // namespace

class DelayLfoAutomationTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();
	void automatedLfoAmountMatchesStatic();
	void automatedLfoPeriodUsesFrequencyNotPeriod();
};

void DelayLfoAutomationTest::initTestCase()
{
	Engine::init(true);
	QVERIFY2(Engine::audioEngine() != nullptr, "engine failed to initialise");
	Engine::audioEngine()->audioDev()->stopProcessing();
	QVERIFY(Engine::audioEngine()->framesPerPeriod() > 0);
}

void DelayLfoAutomationTest::automatedLfoAmountMatchesStatic()
{
	const auto sr = static_cast<f_cnt_t>(Engine::audioEngine()->outputSampleRate());
	// Past the longest delay the LFO can reach (0.5 s + 0.2 s of modulation),
	// then two full periods of the 1 Hz LFO so the sweep covers its extremes
	// whatever phase a freshly constructed (uninitialised) Lfo starts at.
	const f_cnt_t warmup = static_cast<f_cnt_t>(0.75 * sr) + 16;
	const f_cnt_t measure = 2 * sr;

	auto staticFx = std::make_unique<DelayEffect>(nullptr, nullptr);
	configure(*staticFx);
	auto* staticAmount = findModel(*staticFx, QStringLiteral("LFO amount"));
	QVERIFY2(staticAmount != nullptr, "the Delay LFO amount model is reachable by displayName");
	flushPendingInterpolation(*staticAmount);
	const auto staticOut = renderWetRamp(*staticFx, nullptr, 0.0f, warmup, measure);
	const double staticSpread = residualSpread(staticOut, warmup);
	if (staticOut.size() >= 4)
	{
		const auto mid = staticOut.size() / 2;
		const double gMid = static_cast<double>(warmup + mid);
		qInfo("static wet samples: [0]=%g [1]=%g [mid]=%g input[mid]=%g sr=%d blockSize=%d",
			staticOut[0], staticOut[1], staticOut[mid],
			static_cast<double>(RampSlope) * gMid,
			static_cast<int>(Engine::audioEngine()->outputSampleRate()),
			static_cast<int>(Engine::audioEngine()->framesPerPeriod()));
	}

	// Control: the static path converts seconds to samples, so its spread is
	// the full 2 * amount * sr sweep. If this fails, the harness (engine,
	// router activation, settings, oracle) is broken - not the comparison.
	QVERIFY2(staticSpread > MinDeepSpread,
		qPrintable(QStringLiteral("static LFO barely modulated the delay: spread=%1")
			.arg(staticSpread, 0, 'g', 6)));

	auto automatedFx = std::make_unique<DelayEffect>(nullptr, nullptr);
	configure(*automatedFx);
	auto* automatedAmount = findModel(*automatedFx, QStringLiteral("LFO amount"));
	QVERIFY(automatedAmount != nullptr);
	flushPendingInterpolation(*automatedAmount);
	const auto automatedOut = renderWetRamp(*automatedFx, automatedAmount,
		LfoAmountSeconds, warmup, measure);
	const double automatedSpread = residualSpread(automatedOut, warmup);

	// The regression gate: the automated path must modulate the delay by the
	// same depth as the static one. With the amount fed to setLength() as raw
	// seconds instead of samples the modulation is sub-sample, the integer
	// read index barely moves, and the spread collapses by ~4 orders of
	// magnitude - far below MinDeepSpread.
	QVERIFY2(automatedSpread > MinDeepSpread,
		qPrintable(QStringLiteral(
			"automated LFO amount no longer modulates the delay like the static one: "
			"automated spread=%1, static spread=%2 (seconds were read as samples?)")
			.arg(automatedSpread, 0, 'g', 6).arg(staticSpread, 0, 'g', 6)));

	// Same sweep on both paths: a partial or over-scaled conversion shows up
	// as a ratio far from one.
	const double ratio = automatedSpread / staticSpread;
	QVERIFY2(ratio > 0.75 && ratio < 1.25,
		qPrintable(QStringLiteral("automated/static modulation depth ratio=%1 is not ~1 "
			"(automated=%2, static=%3)").arg(ratio, 0, 'g', 6)
			.arg(automatedSpread, 0, 'g', 6).arg(staticSpread, 0, 'g', 6)));

	qInfo("delay LFO modulation spread: static=%g automated=%g ratio=%g",
		staticSpread, automatedSpread, ratio);
}

void DelayLfoAutomationTest::automatedLfoPeriodUsesFrequencyNotPeriod()
{
	const auto sr = static_cast<f_cnt_t>(Engine::audioEngine()->outputSampleRate());
	const double sampleRate = static_cast<double>(sr);
	// Past the longest delay the LFO can reach (0.5 s + 0.2 s of modulation),
	// then a whole number of seconds of measurement, so the two probe
	// frequencies are exactly orthogonal over the window.
	const f_cnt_t warmup = static_cast<f_cnt_t>(0.75 * sampleRate) + 16;
	const f_cnt_t measure = 6 * sr;
	// The depth the LFO modulates with: the residual is the delay length in
	// samples, so the tone is RampSlope * amount * sampleRate deep.
	const double expectedTone = static_cast<double>(RampSlope) * LfoAmountSeconds * sampleRate;

	// The static path: the model holds the period in seconds, and the static
	// path converts it to a frequency (1 / 2 s = 0.5 Hz). It is the reference
	// the automated path has to agree with.
	auto staticFx = std::make_unique<DelayEffect>(nullptr, nullptr);
	configure(*staticFx, LfoPeriodSeconds);
	auto* staticPeriod = findModel(*staticFx, QStringLiteral("LFO period"));
	QVERIFY2(staticPeriod != nullptr, "the Delay LFO period model is reachable by displayName");
	flushPendingInterpolation(*staticPeriod);
	const auto staticOut = renderWetRamp(*staticFx, nullptr, 0.0f, warmup, measure);
	const double staticPeriodTone = toneAmplitude(staticOut, warmup, sampleRate, PeriodHz);
	const double staticWrongTone = toneAmplitude(staticOut, warmup, sampleRate, PeriodAsFrequencyHz);
	QVERIFY2(staticPeriodTone > 0.5 * expectedTone,
		qPrintable(QStringLiteral("the static LFO period does not modulate at %1 Hz: "
			"tone=%2, expected about %3")
			.arg(PeriodHz).arg(staticPeriodTone, 0, 'g', 6).arg(expectedTone, 0, 'g', 6)));
	QVERIFY2(staticPeriodTone > 4.0 * staticWrongTone,
		qPrintable(QStringLiteral("the static LFO period modulates at %1 Hz instead of %2 Hz: "
			"tone=%3, wrong tone=%4")
			.arg(PeriodAsFrequencyHz).arg(PeriodHz)
			.arg(staticPeriodTone, 0, 'g', 6).arg(staticWrongTone, 0, 'g', 6)));

	// The automated path: the same control value, published as a constant
	// automation ramp, so the audio thread reads it through the value buffer.
	auto automatedFx = std::make_unique<DelayEffect>(nullptr, nullptr);
	configure(*automatedFx, LfoPeriodSeconds);
	auto* automatedPeriod = findModel(*automatedFx, QStringLiteral("LFO period"));
	QVERIFY(automatedPeriod != nullptr);
	const auto automatedOut = renderWetRamp(*automatedFx, automatedPeriod,
		LfoPeriodSeconds, warmup, measure);
	const double automatedPeriodTone = toneAmplitude(automatedOut, warmup, sampleRate, PeriodHz);
	const double automatedWrongTone = toneAmplitude(automatedOut, warmup, sampleRate,
		PeriodAsFrequencyHz);

	// The regression gate: an automated period of 2 s must modulate the delay
	// at 0.5 Hz, exactly like the static path. Fed to setFrequency() as a
	// frequency the period becomes 2 Hz, so the 0.5 Hz probe sees nothing.
	QVERIFY2(automatedPeriodTone > 0.5 * expectedTone,
		qPrintable(QStringLiteral(
			"an automated LFO period of %1 s does not modulate at %2 Hz: tone=%3, "
			"expected about %4 (was the period read as a frequency?)")
			.arg(LfoPeriodSeconds).arg(PeriodHz)
			.arg(automatedPeriodTone, 0, 'g', 6).arg(expectedTone, 0, 'g', 6)));
	QVERIFY2(automatedPeriodTone > 4.0 * automatedWrongTone,
		qPrintable(QStringLiteral(
			"an automated LFO period of %1 s modulates at %2 Hz instead of %3 Hz: "
			"tone=%4, wrong tone=%5")
			.arg(LfoPeriodSeconds).arg(PeriodAsFrequencyHz).arg(PeriodHz)
			.arg(automatedPeriodTone, 0, 'g', 6).arg(automatedWrongTone, 0, 'g', 6)));

	// Same frequency on both paths: the automated period means what the
	// static one means.
	const double ratio = automatedPeriodTone / staticPeriodTone;
	QVERIFY2(ratio > 0.75 && ratio < 1.25,
		qPrintable(QStringLiteral("automated/static LFO period tone ratio=%1 is not ~1 "
			"(automated=%2, static=%3)").arg(ratio, 0, 'g', 6)
			.arg(automatedPeriodTone, 0, 'g', 6).arg(staticPeriodTone, 0, 'g', 6)));

	qInfo("delay LFO period tone at %g Hz: static=%g automated=%g (expected about %g)",
		PeriodHz, staticPeriodTone, automatedPeriodTone, expectedTone);
}

} // namespace lmms

QTEST_MAIN(lmms::DelayLfoAutomationTest)

#include "DelayLfoAutomationTest.moc"
