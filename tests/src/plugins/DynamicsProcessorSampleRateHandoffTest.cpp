/*
 * DynamicsProcessorSampleRateHandoffTest.cpp - regression for cluster new-p9-130:
 * the dynamics processor's sample-rate request and its RMS windows are handed to the
 * audio path, never built on it
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

//! The finding (cluster new-p9-130) says the dynamics processor shares its sample-rate
//! flag unsafely: `DynProcControls::sampleRateChanged()` writes `m_effect->m_needsUpdate`
//! on the control thread while `DynProcEffect::processImpl()` reads it and clears it on
//! the audio thread, with no synchronisation. The flag is now a std::atomic<bool>
//! consumed with one exchange; the request also carries a *prepared* RMS window, because
//! a rate increase used to resize the buffers inside processImpl()
//! (`RmsHelper::setSize()`: `delete[]` then `new[]` on the audio thread).
//!
//! The trigger is the second half of that: the period that applies a sample-rate increase
//! must not allocate or free. The regression drives the real slot through
//! `AudioEngine::sampleRateChanged()` with the allocation probe armed around the period
//! only. On the historical code the growth frees and allocates the RMS buffers there and
//! the measurement fails; with the fix the control thread builds the window and the audio
//! path installs it with one pointer exchange.
//!
//! The probe reads the installed window through `appliedWindow()`, which resolves the
//! member the compiled header actually declares (prepared bank, or the effect's own
//! helpers), so this file builds against both and the red run is a failed assertion
//! rather than a build error. There is no clock, no thread, no sleep and no plugin module
//! in the test: the plugin sources are compiled into this binary (the LommSampleRateTest /
//! DispersionNonFiniteTest pattern).
//!
//! The access specifiers are opened for this translation unit only; they affect neither
//! the layout nor the name mangling, so the plugin's own translation units and the link
//! are unaffected.

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

#include "../core/AllocationProbe.h"

// The effect's request flag, its coefficients and windows, the controls' models and
// RmsHelper's window size are private; the macro is opened for this translation unit only.
#define private public
#include "DynamicsProcessor.h"
#include "DynamicsProcessorControls.h"
#include "RmsHelper.h"
#undef private

// The audio thread consumes the request. The handoff has to stay lock-free on every
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
//! the engine emits after stopProcessing(). The slot that prepares the window runs here,
//! on this thread, exactly as it does on the control thread.
void setEngineRate(lmms::AudioEngine* engine, lmms::sample_rate_t rate)
{
	engine->audioDev()->setSampleRateForTesting(rate);
	emit engine->sampleRateChanged();
}

//! Renders one period through the effect and returns the interleaved result.
std::vector<float> render(lmms::DynProcEffect& effect, lmms::f_cnt_t frames)
{
	std::vector<float> samples(static_cast<std::size_t>(frames) * 2, TestLevel);
	effect.processImpl(lmms::InterleavedBufferView<float, 2>{samples.data(), frames});
	return samples;
}

//! What one render period cost on this thread: allocations and frees.
struct PeriodCost
{
	std::uint64_t allocations;
	std::uint64_t frees;
};

//! Renders one period with the allocation probe armed around processImpl() alone, so the
//! test's own buffer is not counted. This is the audio path's measurement.
PeriodCost renderCounted(lmms::DynProcEffect& effect, lmms::f_cnt_t frames)
{
	std::vector<float> samples(static_cast<std::size_t>(frames) * 2, TestLevel);
	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	effect.processImpl(lmms::InterleavedBufferView<float, 2>{samples.data(), frames});
	lmms::test::tlCountAllocations = false;
	return {lmms::test::tlAllocationCount, lmms::test::tlFreeCount};
}

//! True when the header this binary compiled against publishes prepared RMS banks (the
//! fix) rather than the effect's own two helpers (the historical layout).
template <typename T>
concept HasPreparedBank = requires(T& effect) { effect.m_currentBank; };

//! The RMS window the audio path is running on, read from whichever member the compiled
//! header declares, so this file builds against both.
template <typename T>
std::size_t appliedWindow(const T& effect)
{
	if constexpr (HasPreparedBank<T>) { return effect.m_currentBank->m_rms[0].m_size; }
	else { return effect.m_rms[0]->m_size; }
}

//! How many RMS windows the effect owns. The historical layout owns two helpers and never
//! replaces them (2); the fix recycles prepared banks, so the count must stay small.
template <typename T>
std::size_t bankCount(const T& effect)
{
	if constexpr (HasPreparedBank<T>) { return effect.m_banks.size(); }
	else { return 2; }
}

//! True only when the request is handed over through std::atomic<bool>. A plain bool
//! makes the trait false at runtime, so the check fails as an assertion without the
//! header having to change for the test to compile.
template <typename T>
bool isAtomicHandoff(const T&)
{
	return std::is_same_v<T, std::atomic<bool>>;
}

//! Whether a request is pending. Valid for the atomic declaration and for the plain bool
//! the historical code declared, so every slot compiles against both.
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
	void aRateIncreaseIsAppliedWithoutAllocatingOrFreeingOnTheAudioPath();
	void repeatedRateChangesReuseTheWindowsWithoutGrowing();
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
//! atomic is the fix.
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

//! Reconfiguration path, and the reviewer's acceptance criterion: a sample-rate *increase*
//! grows the RMS window, and that growth must happen on the control thread. The period
//! that installs the new window may not allocate or free anything.
void DynamicsProcessorSampleRateHandoffTest::aRateIncreaseIsAppliedWithoutAllocatingOrFreeingOnTheAudioPath()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 44100);
	lmms::DynProcEffect effect(nullptr, nullptr);
	QCOMPARE(appliedWindow(effect), rmsWindow(44100));

	// Baseline: a period with no request costs nothing on the audio path.
	const PeriodCost idle = renderCounted(effect, ShortBlock);
	QVERIFY2(idle.allocations == 0 && idle.frees == 0,
		qPrintable(QStringLiteral("an idle period allocated %1 and freed %2 times")
			.arg(idle.allocations).arg(idle.frees)));

	// The real slot, delivered the way a device change delivers it. The window is built
	// here, and that is the only place it may be built (this thread is the control thread).
	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	setEngineRate(engine, 96000);
	lmms::test::tlCountAllocations = false;
	const std::uint64_t controlSide = lmms::test::tlAllocationCount;
	QVERIFY2(requestPending(effect.m_needsUpdate), "the sample-rate change was not published");
	QVERIFY2(controlSide > 0,
		"the sample-rate slot did not prepare anything: the window growth has to be paid for "
		"on the control thread, not by the audio path");

	// The period that applies it.
	const PeriodCost applied = renderCounted(effect, ShortBlock);
	QVERIFY2(applied.allocations == 0,
		qPrintable(QStringLiteral("the audio path allocated %1 time(s) applying a sample-rate "
			"increase; the RMS window must be prepared outside the processing path "
			"(include/RmsHelper.h:84-90)").arg(applied.allocations)));
	QVERIFY2(applied.frees == 0,
		qPrintable(QStringLiteral("the audio path freed %1 time(s) applying a sample-rate "
			"increase; the replaced window must be retired, not released here")
			.arg(applied.frees)));

	// ... and it did apply: the new window and the new detector coefficients, once.
	QCOMPARE(appliedWindow(effect), rmsWindow(96000));
	QVERIFY2(!requestPending(effect.m_needsUpdate),
		"the render period did not consume the sample-rate request");
	QVERIFY2(std::abs(effect.m_attCoeff - attackCoefficient(effect, 96000)) < 1.0e-12,
		qPrintable(QStringLiteral("attack coefficient %1, expected %2")
			.arg(effect.m_attCoeff).arg(attackCoefficient(effect, 96000))));
	QVERIFY2(std::abs(effect.m_relCoeff - releaseCoefficient(effect, 96000)) < 1.0e-12,
		qPrintable(QStringLiteral("release coefficient %1, expected %2")
			.arg(effect.m_relCoeff).arg(releaseCoefficient(effect, 96000))));

	// Consumed means consumed: the next period changes nothing and still costs nothing.
	const PeriodCost again = renderCounted(effect, ShortBlock);
	QVERIFY2(again.allocations == 0 && again.frees == 0,
		"a period after the update allocated or freed on the audio path");
	QCOMPARE(appliedWindow(effect), rmsWindow(96000));
}

//! Exhaustion/lifetime path: many rate changes, including every direction and a rate that
//! needs a bigger window than any earlier one. Each period stays allocation-free and the
//! effect recycles its prepared windows instead of growing a list of them.
void DynamicsProcessorSampleRateHandoffTest::repeatedRateChangesReuseTheWindowsWithoutGrowing()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 44100);
	lmms::DynProcEffect effect(nullptr, nullptr);
	render(effect, ShortBlock);

	constexpr std::array<lmms::sample_rate_t, 12> rates{
		48000, 96000, 32000, 88200, 44100, 192000, 22050, 96000, 48000, 96000, 44100, 88200};
	std::size_t warmBanks = 0;
	for (std::size_t step = 0; step < rates.size(); ++step)
	{
		setEngineRate(engine, rates[step]);
		const PeriodCost cost = renderCounted(effect, ShortBlock);
		QVERIFY2(cost.allocations == 0 && cost.frees == 0,
			qPrintable(QStringLiteral("step %1 (rate %2): the audio path allocated %3 and "
				"freed %4").arg(step).arg(rates[step]).arg(cost.allocations).arg(cost.frees)));
		QCOMPARE(appliedWindow(effect), rmsWindow(rates[step]));
		if (step == 3) { warmBanks = bankCount(effect); }
	}

	// The windows are recycled: after the first few changes the count is stable, and it
	// never runs away.
	QVERIFY2(bankCount(effect) == warmBanks,
		qPrintable(QStringLiteral("the window list grew from %1 to %2 across the changes; the "
			"retired window must be reused, not accumulated").arg(warmBanks).arg(bankCount(effect))));
	QVERIFY2(bankCount(effect) <= 4,
		qPrintable(QStringLiteral("%1 RMS windows are alive; prepared, pending and retired is "
			"three").arg(bankCount(effect))));
}

//! Normal path: with no request, periods leave the window and the coefficients alone and
//! cost nothing.
void DynamicsProcessorSampleRateHandoffTest::processingWithoutARequestLeavesTheWindowAlone()
{
	auto* const engine = lmms::Engine::audioEngine();
	const RestoreEngineRate restore{engine, engine->outputSampleRate()};

	setEngineRate(engine, 48000);
	lmms::DynProcEffect effect(nullptr, nullptr);
	const std::size_t window = appliedWindow(effect);
	const double attack = effect.m_attCoeff;
	const double release = effect.m_relCoeff;

	for (int period = 0; period < 4; ++period)
	{
		const PeriodCost cost = renderCounted(effect, ShortBlock);
		QVERIFY2(cost.allocations == 0 && cost.frees == 0,
			"an idle period allocated or freed on the audio path");
		QVERIFY2(!requestPending(effect.m_needsUpdate),
			"a render period invented a sample-rate request");
		QCOMPARE(appliedWindow(effect), window);
		QCOMPARE(effect.m_attCoeff, attack);
		QCOMPARE(effect.m_relCoeff, release);
	}
}

QTEST_MAIN( DynamicsProcessorSampleRateHandoffTest )

#include "DynamicsProcessorSampleRateHandoffTest.moc"
