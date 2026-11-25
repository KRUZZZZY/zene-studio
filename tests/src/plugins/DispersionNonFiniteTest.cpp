/*
 * DispersionNonFiniteTest.cpp - regressions for the Dispersion effect:
 * cluster new-p16-64 (a non-finite control must not reach the audio output)
 * and cluster new-p11-91 (a sample-rate change must reach the coefficients)
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

//! The finding (cluster new-p16-64) was filed against the historical line,
//! where the frequency was clamped with `std::min( value, limit )`. A project
//! that saves `freq` as `nan` loads a NaN model value (the model's loader
//! parses it with LocaleHelper::toFloat and clamps nothing), and
//! `std::min` returns its first argument when that argument is NaN, so the
//! Nyquist clamp passed the NaN on. The all-pass coefficients became NaN and
//! the filter output was poisoned from the first sample on. Resonance and
//! feedback are read the same way and had the same hole.
//!
//! The regression renders an impulse through the effect twice: once with the
//! controls a project loaded as NaN, once with the same project with those
//! controls left at their defaults. A NaN control must render finite samples
//! that equal the default render, and a finite control must still drive the
//! filter.
//!
//! The plugin is built as a module, so its sources are compiled into this
//! test binary (the MultitapEchoBoundsTest / Lb302NotePathTest pattern). The
//! test is deterministic: no timing, no threads, no loader and no audio device.
//!
//! The second finding in this file (cluster new-p11-91) says that the effect
//! keeps the output sample rate from the moment it was constructed: the
//! constructor stores `outputSampleRate()` once and `processImpl()` derives the
//! Nyquist clamp, the all-pass coefficients and the DC-removal time constant
//! from that stored value only. The engine does announce a rate change -
//! `AudioEngine::setAudioDevice()` and `restoreAudioDevice()` emit
//! `sampleRateChanged()` after `stopProcessing()`, and `initDevices()` emits it
//! before processing starts - but nothing in the effect listened, so a device
//! swap while the effect stayed loaded left every coefficient at the old rate.
//! The regression below constructs the effect at one rate, changes the device
//! rate while it stays loaded, and requires its render to equal the render of
//! an effect constructed at the new rate.
//!
//! The project parser now rejects non-finite project values: LocaleHelper::toFloat
//! returns 0.0f with ok=false for nan, inf and overflow, so an XML fixture can no
//! longer inject a NaN control. The tests therefore inject NaN through the model,
//! the surface the plugin reads: setValue for the child form and setInitValue for
//! the attribute form (which also sets the value). The assertions are unchanged.

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include <QtTest>
#include <QDomDocument>

#include "AudioBufferView.h"
#include "DispersionControlDialog.h"

#define private public
#include "DispersionControls.h"
#include "Dispersion.h"
#undef private

#include "Engine.h"
#include "AudioDevice.h"
#include "AudioEngine.h"

namespace
{

//! The number of frames each render processes; the impulse is in the first.
constexpr lmms::f_cnt_t FrameCount = 256;

//! The tolerance for "the same render": the fallback runs the same arithmetic
//! as the default, so the difference is expected to be exactly zero.
constexpr float RenderTolerance = 1e-6f;

//! The difference the finite frequency must make against the default render;
//! anything larger than rounding proves the control reached the filter.
constexpr float FilterEffectThreshold = 1e-3f;

//! Loads an effect's controls from a project element, the way a saved project
//! does. The amount is always set, otherwise the filter runs for no stages and
//! no control can reach the output. The parser rejects non-finite values, so a NaN
//! control is set on the model by the caller after loading, not through controlsXml.
std::unique_ptr<lmms::DispersionEffect> loadedEffect(const QString& controlsXml)
{
	auto effect = std::make_unique<lmms::DispersionEffect>(nullptr, nullptr);
	QDomDocument doc;
	doc.setContent(QStringLiteral("<dispersion><amount value=\"4\"/>%1</dispersion>").arg(controlsXml));
	effect->m_dispersionControls.loadSettings(doc.documentElement());
	return effect;
}

//! Loads an effect's controls from attributes on the element itself, the other
//! form a saved project uses. The amount is set the same way. The helper loads
//! finite attributes only; NaN is injected by the caller with setInitValue.
std::unique_ptr<lmms::DispersionEffect> loadedEffectFromAttributes(const QString& attributes)
{
	auto effect = std::make_unique<lmms::DispersionEffect>(nullptr, nullptr);
	QDomDocument doc;
	doc.setContent(QStringLiteral("<dispersion amount=\"4\" %1/>").arg(attributes));
	effect->m_dispersionControls.loadSettings(doc.documentElement());
	return effect;
}

//! The rate change a device swap performs: pin the device's rate, then deliver
//! the signal the engine emits after stopProcessing() (AudioEngine::setAudioDevice,
//! src/core/AudioEngine.cpp). No thread, no clock: the slot runs inline.
void setEngineRate(lmms::AudioEngine* engine, lmms::sample_rate_t rate)
{
	engine->audioDev()->setSampleRateForTesting(rate);
	emit engine->sampleRateChanged();
}

//! Restores the device rate after a slot. Declared before the effects so the
//! effects are destroyed first and this restore reaches no live slot.
struct RestoreEngineRate
{
	lmms::AudioEngine* engine;
	lmms::sample_rate_t rate;
	~RestoreEngineRate() { setEngineRate(engine, rate); }
};

//! An effect whose output depends on the sample rate on both paths the rate
//! feeds: the all-pass coefficients (frequency against the rate) and the
//! DC-removal time constant (44100 / rate). Both are recomputed every period
//! from the stored rate, so a stale rate changes the render.
std::unique_ptr<lmms::DispersionEffect> rateSensitiveEffect()
{
	auto effect = loadedEffect(QStringLiteral("<freq value=\"12000\"/>"));
	effect->m_dispersionControls.m_dcModel.setValue(true);
	return effect;
}

//! Renders one impulse through the effect and returns the interleaved result.
std::vector<float> renderImpulse(lmms::DispersionEffect& effect)
{
	std::vector<float> samples(2 * FrameCount, 0.f);
	samples[0] = 1.f;
	samples[1] = 1.f;
	effect.processImpl(lmms::InterleavedBufferView<float, 2>{samples.data(), FrameCount});
	return samples;
}

//! The number of samples that are not finite.
int nonFiniteSampleCount(const std::vector<float>& samples)
{
	return static_cast<int>(std::count_if(samples.begin(), samples.end(),
		[](float sample) { return !std::isfinite(sample); }));
}

//! The largest difference between two renders. A NaN difference compares as
//! infinite, so a render that is not finite can never count as equal.
float largestDifference(const std::vector<float>& a, const std::vector<float>& b)
{
	float largest = 0.f;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		const float difference = std::fabs(a[i] - b[i]);
		largest = std::isnan(difference) ? std::numeric_limits<float>::infinity() : std::max(largest, difference);
	}
	return largest;
}

} // namespace

class DispersionNonFiniteTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void nonFiniteFrequencyRendersLikeTheDefault();
	void nonFiniteResonanceRendersLikeTheDefault();
	void nonFiniteFeedbackRendersLikeTheDefault();
	void attributeFormNonFiniteFrequencyRendersLikeTheDefault();
	void attributeFormNonFiniteResonanceRendersLikeTheDefault();
	void attributeFormNonFiniteFeedbackRendersLikeTheDefault();
	void finiteFrequencyStillDrivesTheFilter();
	void nullBufferWithFramesIsNotRead();
	void rateChangeReachesTheFilterAndDcCoefficients();
	void sampleRateTracksTheEngineAfterAChange();
};

void DispersionNonFiniteTest::initTestCase()
{
	// The effect reads the engine (output sample rate) when it is constructed.
	lmms::Engine::init(true);
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
}

void DispersionNonFiniteTest::cleanupTestCase()
{
	lmms::Engine::destroy();
}

void DispersionNonFiniteTest::nonFiniteFrequencyRendersLikeTheDefault()
{
	const auto poisoned = loadedEffect(QString{});
	poisoned->m_dispersionControls.m_freqModel.setValue(std::numeric_limits<float>::quiet_NaN());
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_freqModel.value()),
		"the injected NaN frequency did not reach the model; the test would not exercise the bug");

	const auto reference = loadedEffect(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN frequency did not render as the default frequency");
}

void DispersionNonFiniteTest::nonFiniteResonanceRendersLikeTheDefault()
{
	const auto poisoned = loadedEffect(QString{});
	poisoned->m_dispersionControls.m_resoModel.setValue(std::numeric_limits<float>::quiet_NaN());
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_resoModel.value()),
		"the injected NaN resonance did not reach the model; the test would not exercise the bug");

	const auto reference = loadedEffect(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN resonance did not render as the default resonance");
}

void DispersionNonFiniteTest::nonFiniteFeedbackRendersLikeTheDefault()
{
	const auto poisoned = loadedEffect(QString{});
	poisoned->m_dispersionControls.m_feedbackModel.setValue(std::numeric_limits<float>::quiet_NaN());
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_feedbackModel.value()),
		"the injected NaN feedback did not reach the model; the test would not exercise the bug");

	const auto reference = loadedEffect(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN feedback did not render as the default feedback");
}

void DispersionNonFiniteTest::attributeFormNonFiniteFrequencyRendersLikeTheDefault()
{
	const auto poisoned = loadedEffectFromAttributes(QString{});
	poisoned->m_dispersionControls.m_freqModel.setInitValue(std::numeric_limits<float>::quiet_NaN());
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_freqModel.value()),
		"the injected NaN frequency did not reach the model value; the test would not exercise the bug");
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_freqModel.initValue()),
		"the injected NaN frequency did not reach the model init value; the init-value path is not exercised");

	const auto reference = loadedEffectFromAttributes(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN frequency in the attribute-form model did not render as the default");
}

void DispersionNonFiniteTest::attributeFormNonFiniteResonanceRendersLikeTheDefault()
{
	const auto poisoned = loadedEffectFromAttributes(QString{});
	poisoned->m_dispersionControls.m_resoModel.setInitValue(std::numeric_limits<float>::quiet_NaN());
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_resoModel.value()),
		"the injected NaN resonance did not reach the model value; the test would not exercise the bug");
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_resoModel.initValue()),
		"the injected NaN resonance did not reach the model init value; the init-value path is not exercised");

	const auto reference = loadedEffectFromAttributes(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN resonance in the attribute-form model did not render as the default");
}

void DispersionNonFiniteTest::attributeFormNonFiniteFeedbackRendersLikeTheDefault()
{
	const auto poisoned = loadedEffectFromAttributes(QString{});
	poisoned->m_dispersionControls.m_feedbackModel.setInitValue(std::numeric_limits<float>::quiet_NaN());
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_feedbackModel.value()),
		"the injected NaN feedback did not reach the model value; the test would not exercise the bug");
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_feedbackModel.initValue()),
		"the injected NaN feedback did not reach the model init value; the init-value path is not exercised");

	const auto reference = loadedEffectFromAttributes(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN feedback in the attribute-form model did not render as the default");
}

void DispersionNonFiniteTest::finiteFrequencyStillDrivesTheFilter()
{
	// Guards against a fix that replaces every frequency: a finite value must
	// still reach the coefficients, so its render must differ from the default.
	const auto moved = loadedEffect(QStringLiteral("<freq value=\"1000\"/>"));
	const auto reference = loadedEffect(QString{});
	const auto movedRender = renderImpulse(*moved);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(movedRender), 0);
	QVERIFY2(largestDifference(movedRender, referenceRender) > FilterEffectThreshold,
		"a finite frequency no longer changes the filter output");
}

void DispersionNonFiniteTest::nullBufferWithFramesIsNotRead()
{
	// A view with null data reports empty(), so the effect must not read it.
	// Before the framesView() fix the process loop began at the null data
	// pointer and dereferenced it, so this slot crashes on the unfixed code.
	const auto effect = loadedEffect(QString{});
	float* nullData = nullptr;
	lmms::InterleavedBufferView<float, 2> nullView{nullData, FrameCount};
	QVERIFY2(nullView.empty(), "a view with null data must report empty()");
	effect->processImpl(nullView);
	QVERIFY(nullView.empty());
}

void DispersionNonFiniteTest::rateChangeReachesTheFilterAndDcCoefficients()
{
	// Cluster new-p11-91: create the effect at 44.1 kHz, then change the output
	// rate to 48 kHz while it stays loaded. Its coefficients must be the ones
	// an effect constructed at 48 kHz uses, not the ones it was born with.
	auto* const engine = lmms::Engine::audioEngine();
	QVERIFY2(engine->audioDev() != nullptr, "the engine has no audio device");
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 44100);
	// Rendered at 44.1 kHz before the change: the coefficients the unfixed
	// effect would keep using.
	const auto atTheOldRate = rateSensitiveEffect();
	const auto oldRateRender = renderImpulse(*atTheOldRate);
	// The effect under test; constructed at 44.1 kHz and loaded across the
	// change below, untouched until then.
	const auto reconfigured = rateSensitiveEffect();
	QVERIFY2(reconfigured->m_dispersionControls.m_dcModel.value(),
		"the DC-removal path is not enabled; the scenario is not reproduced");
	QCOMPARE(reconfigured->m_sampleRate, 44100.f);

	setEngineRate(engine, 48000);
	// Constructed at the new rate: the coefficients the fix must produce.
	const auto atTheNewRate = rateSensitiveEffect();
	QCOMPARE(atTheNewRate->m_sampleRate, 48000.f);

	const auto newRateRender = renderImpulse(*atTheNewRate);
	const auto reconfiguredRender = renderImpulse(*reconfigured);

	// Guards against a vacuous test: if the rate never reached the coefficients
	// these two reference renders would be identical and the check below would
	// pass on the unfixed code as well.
	QVERIFY2(largestDifference(newRateRender, oldRateRender) > FilterEffectThreshold,
		"the sample rate does not reach the coefficients; the regression would be vacuous");
	QVERIFY2(largestDifference(newRateRender, reconfiguredRender) <= RenderTolerance,
		"the effect kept the coefficients of the rate it was constructed at");
}

void DispersionNonFiniteTest::sampleRateTracksTheEngineAfterAChange()
{
	// The stored rate itself must follow the engine, including on an effect
	// that has already rendered a period at the old rate, and the coefficients
	// derived from it must stay finite after the change.
	auto* const engine = lmms::Engine::audioEngine();
	QVERIFY2(engine->audioDev() != nullptr, "the engine has no audio device");
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 44100);
	const auto effect = rateSensitiveEffect();
	QCOMPARE(effect->m_sampleRate, 44100.f);
	QCOMPARE(nonFiniteSampleCount(renderImpulse(*effect)), 0);

	setEngineRate(engine, 48000);
	QCOMPARE(effect->m_sampleRate, 48000.f);

	setEngineRate(engine, 96000);
	QCOMPARE(effect->m_sampleRate, 96000.f);

	QCOMPARE(nonFiniteSampleCount(renderImpulse(*effect)), 0);
}

QTEST_MAIN( DispersionNonFiniteTest )

#include "DispersionNonFiniteTest.moc"
