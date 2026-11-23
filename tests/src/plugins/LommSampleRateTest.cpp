/*
 * LommSampleRateTest.cpp - regression for cluster new-p9-240:
 * a sample-rate change must re-anchor LOMM's lookahead rings
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

//! The finding (cluster new-p9-240) says that LOMM's sample-rate handler
//! resizes the lookahead buffers while the audio path uses them. The engine
//! side of that claim does not hold on this line: every production emitter of
//! `AudioEngine::sampleRateChanged()` runs after `stopProcessing()`, and
//! `AudioDevice::stopProcessing()` waits until no backend callback scope is
//! open (src/core/audio/AudioDevice.cpp:135-154), so `processImpl()` cannot be
//! running while the slot resizes a vector.
//!
//! The buffers themselves are still broken by the resize. `m_lookWrite` is the
//! lookahead ring's write cursor, and `processImpl()` indexes all six rings
//! with it. The handler rebuilt the rings to the new length but left the old
//! cursor in place: after a rate drop the new rings are shorter than the old
//! cursor, and the next period reads and writes past their end. The handler
//! resets every other piece of DSP state (m_yL, m_rms, m_gainResult,
//! m_displayIn/Out, m_prevOut and the crest followers), so the cursor and the
//! ring contents are the omission that makes the resize unsafe.
//!
//! The regression drives the real handler through the engine signal, exactly
//! as a device change does, and checks the invariant `processImpl()` depends
//! on: the cursor is inside the resized rings. A second slot checks that the
//! rings hold no audio captured at the old rate. Both fail on the historical
//! handler. There is no clock, no thread, no sleep and no plugin module in the
//! test; the plugin sources are compiled into this binary (the
//! DispersionNonFiniteTest / MultitapEchoBoundsTest pattern).
//!
//! The access specifiers are opened for this translation unit only; they
//! affect neither the layout nor the name mangling, so the plugin's own
//! translation units and the link are unaffected.

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include <QtTest>

#include "AudioBufferView.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioPlugin.h"
#include "BasicFilters.h"
#include "Engine.h"
#include "LOMMControlDialog.h"

// The controls' models and the effect's ring state are private; the macro is
// opened for this translation unit only (the DispersionNonFiniteTest pattern).
#define private public
#include "LOMMControls.h"
#include "LOMM.h"
#undef private

namespace
{

//! Frames in a short period; small enough that a rate drop of a few kHz makes
//! the cursor from the old ring fall past the new ring's end.
constexpr lmms::f_cnt_t ShortBlock = 16;

//! A nonzero input, so the lookahead rings are written with values that are
//! distinguishable from their reset contents.
constexpr float TestLevel = 0.5f;

//! The ring length the handler derives for a rate (its own expression,
//! plugins/LOMM/LOMM.cpp: the 20 ms maximum lookahead plus two entries).
int ringLength(lmms::sample_rate_t rate)
{
	return static_cast<int>(std::ceil((lmms::LOMM_MAX_LOOKAHEAD / 1000.f) * rate)) + 2;
}

//! The rate change a device swap performs: pin the device's rate, then deliver
//! the signal the engine emits after stopProcessing().
void setEngineRate(lmms::AudioEngine* engine, lmms::sample_rate_t rate)
{
	engine->audioDev()->setSampleRateForTesting(rate);
	emit engine->sampleRateChanged();
}

//! Renders one period through the effect and returns the interleaved result.
std::vector<float> render(lmms::LOMMEffect& effect, lmms::f_cnt_t frames)
{
	std::vector<float> samples(static_cast<std::size_t>(frames) * 2, 0.f);
	if (!samples.empty())
	{
		samples[0] = TestLevel;
		samples[1] = TestLevel;
	}
	effect.processImpl(lmms::InterleavedBufferView<float, 2>{samples.data(), frames});
	return samples;
}

int nonFiniteSamples(const std::vector<float>& samples)
{
	int count = 0;
	for (const float sample : samples)
	{
		if (!std::isfinite(sample)) { ++count; }
	}
	return count;
}

//! Every ring has the new length, and the write cursor is a valid index into
//! it. The cursor is what processImpl() subscripts the rings with; an index at
//! or past the end is an out-of-bounds access on the audio path.
void verifyRingIsConsistent(const lmms::LOMMEffect& effect)
{
	QVERIFY2(effect.m_lookBufLength > 0, "the ring length was not derived");
	QVERIFY2(effect.m_lookWrite >= 0 && effect.m_lookWrite < effect.m_lookBufLength,
		"the lookahead ring cursor was left outside the resized buffers");
	for (int channel = 0; channel < 2; ++channel)
	{
		for (int band = 0; band < 3; ++band)
		{
			QCOMPARE(static_cast<int>(effect.m_inLookBuf[band][channel].size()), effect.m_lookBufLength);
			QCOMPARE(static_cast<int>(effect.m_scLookBuf[band][channel].size()), effect.m_lookBufLength);
		}
	}
}

//! Entries that still hold a value from audio captured at the old rate. The
//! handler resets the rest of the DSP state on a rate change, so the rings
//! must not carry old-rate samples into the next period either.
int staleRingEntries(const lmms::LOMMEffect& effect)
{
	int stale = 0;
	for (int channel = 0; channel < 2; ++channel)
	{
		for (int band = 0; band < 3; ++band)
		{
			for (const float sample : effect.m_inLookBuf[band][channel])
			{
				if (sample != 0.f) { ++stale; }
			}
			for (const float sample : effect.m_scLookBuf[band][channel])
			{
				if (sample != lmms::LOMM_MIN_FLOOR) { ++stale; }
			}
		}
	}
	return stale;
}

//! Restores the device rate after a test; declared before the effect so the
//! effect is destroyed first and the restore emits to no live slot.
struct RestoreEngineRate
{
	lmms::AudioEngine* engine;
	lmms::sample_rate_t rate;
	~RestoreEngineRate() { setEngineRate(engine, rate); }
};

} // namespace

class LommSampleRateTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void theCursorStaysInsideTheResizedRingsWhenTheRateDrops();
	void theRingsCarryNoOldRateAudioAcrossTheChange();
	void theCursorStaysInsideTheGrownRingsWhenTheRateRises();
	void processingDoesNotResizeTheRings();
};

void LommSampleRateTest::initTestCase()
{
	// The effect reads the engine (output sample rate) when it is constructed.
	lmms::Engine::init(true);
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
	QVERIFY2(lmms::Engine::audioEngine()->audioDev() != nullptr, "the engine has no audio device");
}

void LommSampleRateTest::cleanupTestCase()
{
	lmms::Engine::destroy();
}

void LommSampleRateTest::theCursorStaysInsideTheResizedRingsWhenTheRateDrops()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 48000);
	lmms::LOMMEffect effect(nullptr, nullptr);
	effect.m_lommControls.m_lookaheadEnableModel.setValue(true);
	effect.m_lommControls.m_lookaheadModel.setValue(lmms::LOMM_MAX_LOOKAHEAD);
	QCOMPARE(effect.m_lookBufLength, ringLength(48000));

	// Play more than a whole ring, so the cursor wraps the way a long period
	// would. A ring of 962 entries ends the period with the cursor at 946.
	const auto firstPeriod = render(effect, effect.m_lookBufLength + ShortBlock);
	QCOMPARE(nonFiniteSamples(firstPeriod), 0);
	QVERIFY2(effect.m_lookWrite >= 0 && effect.m_lookWrite < effect.m_lookBufLength,
		"processing itself left the ring cursor outside the ring");

	// The scenario needs an old cursor past the shorter ring's end: 946 >= 884.
	QVERIFY2(effect.m_lookWrite >= ringLength(44100),
		"the cursor did not reach past the shorter ring; the scenario is not reproduced");

	setEngineRate(engine, 44100);
	QCOMPARE(effect.m_lookBufLength, ringLength(44100));

	// The resize must re-anchor the cursor. Without it the next period indexes
	// the 884-entry rings with 946 and reads and writes past their end.
	verifyRingIsConsistent(effect);

	const auto secondPeriod = render(effect, ShortBlock);
	QCOMPARE(nonFiniteSamples(secondPeriod), 0);
}

void LommSampleRateTest::theRingsCarryNoOldRateAudioAcrossTheChange()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 48000);
	lmms::LOMMEffect effect(nullptr, nullptr);
	effect.m_lommControls.m_lookaheadEnableModel.setValue(true);
	effect.m_lommControls.m_lookaheadModel.setValue(lmms::LOMM_MAX_LOOKAHEAD);

	// The first period writes the nonzero input and its filter tails into the
	// rings; without this the check below would be vacuous.
	render(effect, ShortBlock);
	QVERIFY2(staleRingEntries(effect) > 0, "the rendered period left no audio in the rings");

	// The handler resets every other piece of DSP state; the rings must not
	// replay audio captured at the old rate into the new rate's periods.
	setEngineRate(engine, 44100);
	QCOMPARE(staleRingEntries(effect), 0);
	verifyRingIsConsistent(effect);
}

void LommSampleRateTest::theCursorStaysInsideTheGrownRingsWhenTheRateRises()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 32000);
	lmms::LOMMEffect effect(nullptr, nullptr);
	effect.m_lommControls.m_lookaheadEnableModel.setValue(true);
	effect.m_lommControls.m_lookaheadModel.setValue(lmms::LOMM_MAX_LOOKAHEAD);
	render(effect, ShortBlock);
	QVERIFY2(effect.m_lookWrite >= 0, "processing left the ring cursor negative");

	setEngineRate(engine, 96000);
	QCOMPARE(effect.m_lookBufLength, ringLength(96000));

	verifyRingIsConsistent(effect);

	const auto period = render(effect, ShortBlock);
	QCOMPARE(nonFiniteSamples(period), 0);
}

void LommSampleRateTest::processingDoesNotResizeTheRings()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 48000);
	lmms::LOMMEffect effect(nullptr, nullptr);
	effect.m_lommControls.m_lookaheadEnableModel.setValue(true);
	effect.m_lommControls.m_lookaheadModel.setValue(lmms::LOMM_MAX_LOOKAHEAD);

	// The audio path must run on storage it was handed: a resize here would
	// allocate and free on the audio thread. The delay buffers are sized by the
	// control-thread sample-rate handler, not by processImpl().
	// Six rings (two channels, three bands) and twelve vectors (input and
	// sidechain per ring).
	std::array<std::size_t, 12> capacities{};
	std::array<const float*, 12> addresses{};
	int entry = 0;
	for (int channel = 0; channel < 2; ++channel)
	{
		for (int band = 0; band < 3; ++band)
		{
			capacities[entry] = effect.m_inLookBuf[band][channel].capacity();
			addresses[entry] = effect.m_inLookBuf[band][channel].data();
			++entry;
			capacities[entry] = effect.m_scLookBuf[band][channel].capacity();
			addresses[entry] = effect.m_scLookBuf[band][channel].data();
			++entry;
		}
	}

	render(effect, ShortBlock);

	entry = 0;
	for (int channel = 0; channel < 2; ++channel)
	{
		for (int band = 0; band < 3; ++band)
		{
			QCOMPARE(effect.m_inLookBuf[band][channel].capacity(), capacities[entry]);
			QCOMPARE(effect.m_inLookBuf[band][channel].data(), addresses[entry]);
			++entry;
			QCOMPARE(effect.m_scLookBuf[band][channel].capacity(), capacities[entry]);
			QCOMPARE(effect.m_scLookBuf[band][channel].data(), addresses[entry]);
			++entry;
		}
	}
}

QTEST_MAIN( LommSampleRateTest )

#include "LommSampleRateTest.moc"
