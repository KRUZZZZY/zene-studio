/*
 * DynamicsProcessorSampleRateHandoffTest.cpp - regression for cluster new-p9-130:
 * the dynamics processor's sample-rate request is an atomic handoff
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

//! The finding (cluster new-p9-130) says the dynamics processor shares its
//! sample-rate flag unsafely: `DynProcControls::sampleRateChanged()` writes
//! `m_effect->m_needsUpdate` on the control thread while `DynProcEffect::processImpl()`
//! reads it and clears it on the audio thread, with no synchronisation.
//!
//! That is the code's shape. The engine happens to quiesce the audio thread before
//! every production emitter of `AudioEngine::sampleRateChanged()`
//! (`setAudioDevice()`/`restoreAudioDevice()` call `stopProcessing()` first,
//! `src/core/AudioEngine.cpp:1420-1456`, and `AudioDevice::stopProcessing()` waits for
//! open callbacks, `src/core/audio/AudioDevice.cpp:137-156`), and `initDevices()` emits
//! once at startup (`AudioEngine.cpp:812`). So today the two accesses are ordered by the
//! engine and the read-then-clear window is not reachable - but that ordering is an
//! undocumented engine invariant, not a contract the plugin states or can rely on, and
//! the plugin itself has none. An atomic handoff makes the flag independent of it, and
//! consuming it with one exchange means a request that arrives during a period is not
//! lost between the read and the clear.
//!
//! The regression trigger is the handoff itself: the flag's declared type is the fix, and
//! the type is probed at runtime through an overload-free trait, so this file compiles
//! against the unfixed header and reports the defect as a failed assertion rather than a
//! build error. The behavioural slots around it prove the handoff still works: a request
//! is published by the real slot, consumed exactly once by a render period, applied to the
//! RMS window and the attack/release coefficients at the new rate, repeated requests
//! coalesce, and a period with no request changes nothing. There is no clock, no thread,
//! no sleep and no plugin module in the test: the plugin sources are compiled into this
//! binary (the LommSampleRateTest / DispersionNonFiniteTest pattern).
//!
//! The access specifiers are opened for this translation unit only; they affect neither
//! the layout nor the name mangling, so the plugin's own translation units and the link
//! are unaffected.

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <QtTest>

#include "AudioBufferView.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"

// The effect's request flag and coefficients, the controls' models and RmsHelper's window
// size are private; the macro is opened for this translation unit only.
#define private public
#include "DynamicsProcessor.h"
#include "DynamicsProcessorControls.h"
#include "RmsHelper.h"
#undef private

// The render thread consumes the request. The handoff has to stay lock-free on every
// platform this product builds for, or the consume would be a lock on the audio thread.
static_assert(std::atomic<bool>::is_always_lock_free,
	"the sample-rate handoff must be lock-free to be consumed on the audio thread");

namespace
{

//! A short period: the effects below are measured through the request, not the signal.
constexpr lmms::f_cnt_t ShortBlock = 16;

//! A nonzero input, so the RMS windows are fed a signal rather than silence.
constexpr float TestLevel = 0.5f;

//! The RMS window the effect derives for a rate - the effect's own expression
//! (`64 * outputSampleRate() / 44100`).
std::size_t rmsWindow(lmms::sample_rate_t rate)
{
	return 64 * rate / 44100;
}

//! The attack coefficient calcAttack() derives for a rate, from the product's own
//! expression and its DNF_LOG constant (-1.0, DynamicsProcessor.cpp).
double attackCoefficient(const lmms::DynProcEffect& effect, lmms::sample_rate_t rate)
{
	const double attackMs = effect.m_dpControls.m_attackModel.value() * 0.001;
	return std::exp((-1.0 / attackMs) / rate);
}

//! The release coefficient calcRelease() derives for a rate, the same way.
double releaseCoefficient(const lmms::DynProcEffect& effect, lmms::sample_rate_t rate)
{
	const double releaseMs = effect.m_dpControls.m_releaseModel.value() * 0.001;
	return std::exp((-1.0 / releaseMs) / rate);
}

//! The rate change a device swap performs: pin the device's rate, then deliver the signal
//! the engine emits after stopProcessing(). The slot that writes the flag runs here, on
//! this thread, exactly as it does on the control thread.
void setEngineRate(lmms::AudioEngine* engine, lmms::sample_rate_t rate)
{
	engine->audioDev()->setSampleRateForTesting(rate);
	emit engine->sampleRateChanged();
}

//! Renders one period through the effect and returns the interleaved result.
std::vector<float> render(lmms::DynProcEffect& effect, lmms::f_cnt_t frames)
{
	std::vector<float> samples(static_cast<std::size_t>(frames) * 2, 0.f);
	for (std::size_t i = 0; i + 1 < samples.size(); i += 2)
	{
		samples[i] = TestLevel;
		samples[i + 1] = TestLevel;
	}
	effect.processImpl(lmms::InterleavedBufferView<float, 2>{samples.data(), frames});
	return samples;
}

//! True only when the request is handed over through std::atomic<bool>. A plain bool
//! makes the trait false at runtime, so the regression below fails as an assertion
//! without the header having to change for the test to compile.
template <typename T>
bool isAtomicHandoff(const T&)
{
	return std::is_same_v<T, std::atomic<bool>>;
}

//! Whether a request is pending. Valid for the atomic declaration and for the plain bool
//! the historical code declared, so the behavioural slots compile against both.
template <typename T>
bool requestPending(const T& flag)
{
	return static_cast<bool>(flag);
}

//! Restores the device rate after a test; declared before the effect so the effect is
//! destroyed first and the restore emits to no live slot.
struct RestoreEngineRate
{
	lmms::AudioEngine* engine;
	lmms::sample_rate_t rate;
	~RestoreEngineRate() { setEngineRate(engine, rate); }
};

} // namespace

class DynamicsProcessorSampleRateHandoffTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void theSampleRateRequestIsHandedOffThroughAnAtomicFlag();
	void aSampleRateChangeIsAppliedOnceAndConsumed();
	void repeatedRequestsCoalesceIntoOneUpdate();
	void processingWithoutARequestLeavesTheWindowAlone();
};

void DynamicsProcessorSampleRateHandoffTest::initTestCase()
{
	// The effect reads the engine (output sample rate) when it is constructed, and the
	// controls touch the song from their samplesChanged() slot.
	lmms::Engine::init(true);
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
	QVERIFY2(lmms::Engine::audioEngine()->audioDev() != nullptr, "the engine has no audio device");
}

void DynamicsProcessorSampleRateHandoffTest::cleanupTestCase()
{
	lmms::Engine::destroy();
}

//! BUG new-p9-130: the request is a flag shared between the control thread
//! (DynProcControls::sampleRateChanged()) and the audio thread (processImpl() reads it
//! and clears it). The declared type is the handoff: a plain bool is the defect, an
//! atomic is the fix. This is the regression trigger.
void DynamicsProcessorSampleRateHandoffTest::theSampleRateRequestIsHandedOffThroughAnAtomicFlag()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 48000);
	lmms::DynProcEffect effect(nullptr, nullptr);
	QVERIFY2(!requestPending(effect.m_needsUpdate),
		"a freshly constructed effect already requests a sample-rate update");

	QVERIFY2(isAtomicHandoff(effect.m_needsUpdate),
		"m_needsUpdate is a plain bool: DynProcControls::sampleRateChanged() writes it on the "
		"control thread while DynProcEffect::processImpl() reads and clears it on the audio "
		"thread, with no synchronisation (plugins/DynamicsProcessor/DynamicsProcessor.h:65, "
		"DynamicsProcessorControls.cpp:64, DynamicsProcessor.cpp:117-123)");

	// The request still has to reach the effect through the real slot.
	setEngineRate(engine, 96000);
	QVERIFY2(requestPending(effect.m_needsUpdate),
		"the sample-rate slot did not publish a request to the effect");
}

//! Reconfiguration path: the request is consumed by exactly one render period, and that
//! period re-derives the RMS window and the detector coefficients at the new rate - the
//! resources the handoff exists to refresh.
void DynamicsProcessorSampleRateHandoffTest::aSampleRateChangeIsAppliedOnceAndConsumed()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 48000);
	lmms::DynProcEffect effect(nullptr, nullptr);
	QCOMPARE(effect.m_rms[0]->m_size, rmsWindow(48000));
	QCOMPARE(effect.m_rms[1]->m_size, rmsWindow(48000));

	setEngineRate(engine, 96000);
	QVERIFY2(requestPending(effect.m_needsUpdate), "the sample-rate change was not published");

	render(effect, ShortBlock);

	QVERIFY2(!requestPending(effect.m_needsUpdate),
		"the render period did not consume the sample-rate request");
	QCOMPARE(effect.m_rms[0]->m_size, rmsWindow(96000));
	QCOMPARE(effect.m_rms[1]->m_size, rmsWindow(96000));
	QVERIFY2(std::abs(effect.m_attCoeff - attackCoefficient(effect, 96000)) < 1.0e-12,
		qPrintable(QStringLiteral("attack coefficient %1, expected %2")
			.arg(effect.m_attCoeff).arg(attackCoefficient(effect, 96000))));
	QVERIFY2(std::abs(effect.m_relCoeff - releaseCoefficient(effect, 96000)) < 1.0e-12,
		qPrintable(QStringLiteral("release coefficient %1, expected %2")
			.arg(effect.m_relCoeff).arg(releaseCoefficient(effect, 96000))));

	// Consumed means consumed: the next period does not redo the work or undo the state.
	render(effect, ShortBlock);
	QCOMPARE(effect.m_rms[0]->m_size, rmsWindow(96000));
	QVERIFY2(std::abs(effect.m_attCoeff - attackCoefficient(effect, 96000)) < 1.0e-12,
		"a second period changed the attack coefficient without a new request");
}

//! Exhaustion/timing path: several rate changes with no period in between. The request
//! coalesces, the last rate wins, and one period applies it exactly once.
void DynamicsProcessorSampleRateHandoffTest::repeatedRequestsCoalesceIntoOneUpdate()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 44100);
	lmms::DynProcEffect effect(nullptr, nullptr);
	QCOMPARE(effect.m_rms[0]->m_size, rmsWindow(44100));

	setEngineRate(engine, 48000);
	setEngineRate(engine, 96000);
	QVERIFY2(requestPending(effect.m_needsUpdate),
		"repeated sample-rate changes dropped the pending request");

	render(effect, ShortBlock);
	QVERIFY2(!requestPending(effect.m_needsUpdate),
		"one period left the sample-rate request pending");
	QCOMPARE(effect.m_rms[0]->m_size, rmsWindow(96000));
	QCOMPARE(effect.m_rms[1]->m_size, rmsWindow(96000));
}

//! Normal path: with no request, periods leave the window and the coefficients alone.
void DynamicsProcessorSampleRateHandoffTest::processingWithoutARequestLeavesTheWindowAlone()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 48000);
	lmms::DynProcEffect effect(nullptr, nullptr);
	const std::size_t window = effect.m_rms[0]->m_size;
	const double attack = effect.m_attCoeff;
	const double release = effect.m_relCoeff;

	for (int period = 0; period < 4; ++period)
	{
		render(effect, ShortBlock);
		QVERIFY2(!requestPending(effect.m_needsUpdate),
			"a render period invented a sample-rate request");
		QCOMPARE(effect.m_rms[0]->m_size, window);
		QCOMPARE(effect.m_rms[1]->m_size, window);
		QCOMPARE(effect.m_attCoeff, attack);
		QCOMPARE(effect.m_relCoeff, release);
	}
}

QTEST_MAIN( DynamicsProcessorSampleRateHandoffTest )

#include "DynamicsProcessorSampleRateHandoffTest.moc"
