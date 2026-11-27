/*
 * CrossoverEQMuteHistoryTest.cpp - regression: a muted crossover band must keep
 * running its filter, so un-muting it does not replay a stale filter history.
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
 * The scenario, all in rendered audio (no clocks, no sleeps):
 *  1. band 1 is on and a DC level drives the low-pass filter state to ~1;
 *  2. band 1 is turned off (the "Mute Band 1" model is false when the band is
 *     silenced) and the input goes silent for a full second, long enough for
 *     a running filter to decay to zero;
 *  3. band 1 is turned back on, input still silent.
 * A band that kept running its filter while off outputs silence in step 3. A
 * band whose filter froze at its step-1 state emits that stale state as a
 * transient at full scale.
 */

#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "AudioBus.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "CrossoverEQ.h"
#include "Effect.h"
#include "EffectControls.h"
#include "Engine.h"
#include "SampleFrame.h"

namespace lmms {

namespace {

constexpr double StaleTransientLimit = 1.0e-3; // a stale state is ~1.0
constexpr float DcLevel = 1.0f;

// Wet-only, effect on, auto-quit off, so every period is rendered and the
// output is exactly the band sum.
void configure(Effect& fx)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("effect"));
	root.setAttribute(QStringLiteral("on"), 1);
	root.setAttribute(QStringLiteral("wet"), QStringLiteral("1"));
	root.setAttribute(QStringLiteral("autoquit"), 0);
	(void)fx.controls()->saveState(doc, root);
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

// Renders `frames` frames of constant `level` in engine-sized blocks and
// returns the largest absolute left-channel output of the FIRST block.
double renderConstant(Effect& fx, float level, f_cnt_t frames)
{
	const f_cnt_t blockSize = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> data(blockSize);
	double firstBlockPeak = 0.0;

	for (f_cnt_t done = 0; done < frames; done += blockSize)
	{
		const f_cnt_t n = std::min(blockSize, frames - done);
		for (f_cnt_t f = 0; f < n; ++f)
		{
			data[f].left() = level;
			data[f].right() = level;
		}

		AutomatableModel::incrementPeriodCounter();
		SampleFrame* busPointers[1] = {data.data()};
		AudioBus bus{busPointers, 1, n};
		fx.processAudioBuffer(bus);

		if (done == 0)
		{
			for (f_cnt_t f = 0; f < n; ++f)
			{
				firstBlockPeak = std::max(firstBlockPeak,
					static_cast<double>(std::abs(data[f].left())));
			}
		}
	}
	return firstBlockPeak;
}

} // namespace

class CrossoverEQMuteHistoryTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();
	void unmutedBandHasNoStaleTransient();
};

void CrossoverEQMuteHistoryTest::initTestCase()
{
	Engine::init(true);
	QVERIFY2(Engine::audioEngine() != nullptr, "engine failed to initialise");
	Engine::audioEngine()->audioDev()->stopProcessing();
	QVERIFY(Engine::audioEngine()->framesPerPeriod() > 0);
}

void CrossoverEQMuteHistoryTest::unmutedBandHasNoStaleTransient()
{
	const auto sr = static_cast<f_cnt_t>(Engine::audioEngine()->outputSampleRate());

	auto fx = std::make_unique<CrossoverEQEffect>(nullptr, nullptr);
	configure(*fx);
	auto* band1 = findModel(*fx, QStringLiteral("Mute Band 1"));
	QVERIFY2(band1 != nullptr, "the Band 1 on/off model is reachable by displayName");
	QVERIFY(band1->value<bool>());

	// Control: with band 1 on, a DC input is passed (band 1 low-passes it), so
	// the harness really drives the filter state away from zero.
	const double drivenPeak = renderConstant(*fx, DcLevel, sr / 2);
	QVERIFY2(drivenPeak > 0.01,
		qPrintable(QStringLiteral("harness: DC input produced no output, peak=%1").arg(drivenPeak)));

	// Band 1 off, input silent for one second: a running filter decays to 0.
	band1->setValue(0.f);
	(void)renderConstant(*fx, 0.f, sr);

	// Band 1 back on, still silent: any output is the band's filter history.
	band1->setValue(1.f);
	const double transient = renderConstant(*fx, 0.f, Engine::audioEngine()->framesPerPeriod());
	QVERIFY2(transient < StaleTransientLimit,
		qPrintable(QStringLiteral("un-muting band 1 replayed a stale filter state: "
			"peak=%1 on silent input (limit %2)").arg(transient, 0, 'g', 6)
			.arg(StaleTransientLimit, 0, 'g', 6)));
}

} // namespace lmms

QTEST_MAIN(lmms::CrossoverEQMuteHistoryTest)

#include "CrossoverEQMuteHistoryTest.moc"
