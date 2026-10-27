/*
 * DispersionNonFiniteTest.cpp - regression for cluster new-p16-64:
 * a non-finite Dispersion control must not reach the audio output
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
//! no control can reach the output.
std::unique_ptr<lmms::DispersionEffect> loadedEffect(const QString& controlsXml)
{
	auto effect = std::make_unique<lmms::DispersionEffect>(nullptr, nullptr);
	QDomDocument doc;
	doc.setContent(QStringLiteral("<dispersion><amount value=\"4\"/>%1</dispersion>").arg(controlsXml));
	effect->m_dispersionControls.loadSettings(doc.documentElement());
	return effect;
}

//! Loads an effect's controls from attributes on the element itself, the other
//! form a saved project uses. The amount is set the same way. A NaN attribute
//! sets the control's init value as well as its value.
std::unique_ptr<lmms::DispersionEffect> loadedEffectFromAttributes(const QString& attributes)
{
	auto effect = std::make_unique<lmms::DispersionEffect>(nullptr, nullptr);
	QDomDocument doc;
	doc.setContent(QStringLiteral("<dispersion amount=\"4\" %1/>").arg(attributes));
	effect->m_dispersionControls.loadSettings(doc.documentElement());
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
	const auto poisoned = loadedEffect(QStringLiteral("<freq value=\"nan\"/>"));
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_freqModel.value()),
		"the project's freq=nan did not load as NaN; the test would not exercise the bug");

	const auto reference = loadedEffect(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN frequency did not render as the default frequency");
}

void DispersionNonFiniteTest::nonFiniteResonanceRendersLikeTheDefault()
{
	const auto poisoned = loadedEffect(QStringLiteral("<reso value=\"nan\"/>"));
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_resoModel.value()),
		"the project's reso=nan did not load as NaN; the test would not exercise the bug");

	const auto reference = loadedEffect(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN resonance did not render as the default resonance");
}

void DispersionNonFiniteTest::nonFiniteFeedbackRendersLikeTheDefault()
{
	const auto poisoned = loadedEffect(QStringLiteral("<feedback value=\"nan\"/>"));
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_feedbackModel.value()),
		"the project's feedback=nan did not load as NaN; the test would not exercise the bug");

	const auto reference = loadedEffect(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN feedback did not render as the default feedback");
}

void DispersionNonFiniteTest::attributeFormNonFiniteFrequencyRendersLikeTheDefault()
{
	const auto poisoned = loadedEffectFromAttributes(QStringLiteral("freq=\"nan\""));
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_freqModel.value()),
		"the attribute freq=nan did not load as NaN; the test would not exercise the bug");
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_freqModel.initValue()),
		"the attribute freq=nan did not set the init value; the init-value path is not exercised");

	const auto reference = loadedEffectFromAttributes(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN frequency loaded from attributes did not render as the default");
}

void DispersionNonFiniteTest::attributeFormNonFiniteResonanceRendersLikeTheDefault()
{
	const auto poisoned = loadedEffectFromAttributes(QStringLiteral("reso=\"nan\""));
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_resoModel.value()),
		"the attribute reso=nan did not load as NaN; the test would not exercise the bug");
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_resoModel.initValue()),
		"the attribute reso=nan did not set the init value; the init-value path is not exercised");

	const auto reference = loadedEffectFromAttributes(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN resonance loaded from attributes did not render as the default");
}

void DispersionNonFiniteTest::attributeFormNonFiniteFeedbackRendersLikeTheDefault()
{
	const auto poisoned = loadedEffectFromAttributes(QStringLiteral("feedback=\"nan\""));
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_feedbackModel.value()),
		"the attribute feedback=nan did not load as NaN; the test would not exercise the bug");
	QVERIFY2(std::isnan(poisoned->m_dispersionControls.m_feedbackModel.initValue()),
		"the attribute feedback=nan did not set the init value; the init-value path is not exercised");

	const auto reference = loadedEffectFromAttributes(QString{});
	const auto poisonedRender = renderImpulse(*poisoned);
	const auto referenceRender = renderImpulse(*reference);

	QCOMPARE(nonFiniteSampleCount(poisonedRender), 0);
	QVERIFY2(largestDifference(poisonedRender, referenceRender) <= RenderTolerance,
		"a NaN feedback loaded from attributes did not render as the default");
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

QTEST_MAIN( DispersionNonFiniteTest )

#include "DispersionNonFiniteTest.moc"
