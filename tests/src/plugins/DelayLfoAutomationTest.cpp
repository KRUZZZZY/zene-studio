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
 */

#include <QtTest/QtTest>

#include <algorithm>
#include <limits>
#include <memory>
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

// The Delay settings every render shares: wet only, no feedback, 1 Hz LFO
// (LfoFrequency is the LFO period in seconds), amount fixed at 0.2 s. The
// round trip mirrors the project load path the migration harness uses: the
// controls serialise their defaults first, the overrides are written onto the
// controls element, and Effect::loadSettings reads on/wet/autoquit off the
// root and the models off the controls child.
void configure(Effect& fx)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("effect"));
	root.setAttribute(QStringLiteral("on"), 1);
	root.setAttribute(QStringLiteral("wet"), QString::number(1.0, 'g', 9));
	root.setAttribute(QStringLiteral("autoquit"), 0);

	QDomElement controls = fx.controls()->saveState(doc, root);
	controls.setAttribute(QStringLiteral("DelayTimeSamples"), QStringLiteral("0.5"));
	controls.setAttribute(QStringLiteral("FeebackAmount"), QStringLiteral("0"));
	controls.setAttribute(QStringLiteral("LfoFrequency"), QStringLiteral("1.0"));
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
// set, each period publishes a constant ramp for it before processing - the
// block the automation evaluation would publish for a knob held at a value.
std::vector<float> renderWetRamp(DelayEffect& fx, AutomatableModel* automated,
	f_cnt_t warmup, f_cnt_t measure)
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
			ramp.setBlockValue(LfoAmountSeconds);
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

} // namespace

class DelayLfoAutomationTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();
	void automatedLfoAmountMatchesStatic();
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
	const auto staticOut = renderWetRamp(*staticFx, nullptr, warmup, measure);
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
	const auto automatedOut = renderWetRamp(*automatedFx, automatedAmount, warmup, measure);
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

} // namespace lmms

QTEST_MAIN(lmms::DelayLfoAutomationTest)

#include "DelayLfoAutomationTest.moc"
