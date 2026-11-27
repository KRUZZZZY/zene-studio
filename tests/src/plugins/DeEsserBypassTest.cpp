/*
 * DeEsserBypassTest.cpp - cluster new-p14-85: a bypassed de-esser must not keep its attenuation
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

#include <algorithm>
#include <cmath>

#include <QtTest>

#include <vector>
#include "AudioBus.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "DeEsser.h"
#include "Engine.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t BlockFrames = 4410;
constexpr float SibilanceHz = 10000.f;
//! As a literal, the way include/BiquadDesign.h does it: MSVC's <cmath> defines M_PI only under
//! _USE_MATH_DEFINES, which this project never sets.
constexpr double Pi = 3.14159265358979323846;

using Block = std::vector<SampleFrame>;

// A stereo 10 kHz sine: above the default 6 kHz detector frequency, so it is "sibilance".
Block sineBlock(float amplitude)
{
	Block block(BlockFrames);
	const double step = 2.0 * Pi * SibilanceHz / 44100.0;
	for (f_cnt_t frame = 0; frame < BlockFrames; ++frame)
	{
		const float v = amplitude * static_cast<float>(std::sin(step * static_cast<double>(frame)));
		block[frame] = SampleFrame(v, v);
	}
	return block;
}

void run(DeEsserEffect& effect, Block& block)
{
	SampleFrame* channel = block.data();
	AudioBus bus{&channel, 1, BlockFrames};
	bus.updateAll();
	static_cast<Effect&>(effect).processAudioBuffer(bus);
}

} // namespace

class DeEsserBypassTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("samplerate"),
			QStringLiteral("44100"));
		Engine::init(true);
		QCOMPARE(Engine::audioEngine()->baseSampleRate(), 44100);
	}

	void cleanupTestCase() { Engine::destroy(); }

	// The loud sibilant turns the device down; the premise of the regression.
	void loudSibilanceIsTurnedDown()
	{
		DeEsserEffect effect{nullptr, nullptr};
		for (int i = 0; i < 3; ++i)
		{
			Block loud = sineBlock(0.5f);
			run(effect, loud);
		}
		QVERIFY(effect.gainReductionDb() > 6.f);
	}

	// Pre-fix: bypass cleared only the meter, so the envelope and gain stayed where the loud signal
	// left them and the first quiet block after un-bypassing was still being turned down.
	void quietSignalAfterBypassIsNotAttenuated()
	{
		DeEsserEffect effect{nullptr, nullptr};
		for (int i = 0; i < 3; ++i)
		{
			Block loud = sineBlock(0.5f);
			run(effect, loud);
		}
		QVERIFY(effect.gainReductionDb() > 6.f);

		effect.setEnabled(false);
		Block bypassed = sineBlock(0.5f);
		run(effect, bypassed);
		QCOMPARE(effect.gainReductionDb(), 0.f);

		effect.setEnabled(true);
		const Block reference = sineBlock(0.01f); // -40 dBFS: under the -30 dB threshold
		Block quiet = sineBlock(0.01f);
		run(effect, quiet);

		QCOMPARE(effect.gainReductionDb(), 0.f);
		float worst = 0.f;
		for (f_cnt_t frame = 0; frame < BlockFrames; ++frame)
		{
			for (int ch = 0; ch < 2; ++ch)
			{
				worst = std::max(worst,
					std::abs(quiet[frame][ch] - reference[frame][ch]));
			}
		}
		QVERIFY2(worst < 1e-6f, qPrintable(QStringLiteral("max deviation %1").arg(worst)));
	}
};

QTEST_GUILESS_MAIN(DeEsserBypassTest)
#include "DeEsserBypassTest.moc"
