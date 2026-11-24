/*
 * LfoControllerUserWaveTest.cpp - user-wave buffer ownership across render updates
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "Oscillator.h"
#include "AutomatableModel.h"
#include "Controller.h"
#include "ControllerDialog.h"
#include "SampleBuffer.h"
#include "TempoSyncKnobModel.h"

#define private public
#include "LfoController.h"
#undef private

using namespace lmms;

template<typename T>
concept HasRenderSafeUserWaveHandoff = requires(T& lfo, std::shared_ptr<const SampleBuffer> buffer)
{
	lfo.setUserDefSampleBuffer(std::move(buffer));
	lfo.acquireUserDefSampleBufferForRender();
	lfo.releaseUserDefSampleBufferForRender();
	lfo.reclaimRetiredUserDefSampleBuffers();
};

class LfoControllerUserWaveTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void replacedWaveRemainsAliveWhileRenderLeaseIsHeld()
	{
		verifyReplacementLifetime<LfoController>();
	}

	//! new-p9-249: speed, multiplier and waveform slots run on the UI thread through
	//! Qt::DirectConnection while updateValueBuffer() reads the same fields on the
	//! render thread. Every field shared that way must be an atomic handoff.
	void parameterStateSharedWithRenderIsAtomic()
	{
		LfoController lfo(nullptr);
		const bool durationIsAtomic = [](auto& l) { return requires { l.m_duration.load(std::memory_order_acquire); }; }(lfo);
		const bool sampleFunctionIsAtomic =
			[](auto& l) { return requires { l.m_sampleFunction.load(std::memory_order_acquire); }; }(lfo);
		const bool heldSampleIsAtomic = [](auto& l) { return requires { l.m_heldSample.load(std::memory_order_relaxed); }; }(lfo);
		QVERIFY2(durationIsAtomic, "m_duration is a plain field written by the UI and read by render");
		QVERIFY2(sampleFunctionIsAtomic, "m_sampleFunction is a plain field written by the UI and read by render");
		QVERIFY2(heldSampleIsAtomic, "m_heldSample is a plain field written by the UI and by render");

		// Behaviour of the published duration stays the same: rate * speed, scaled by the multiplier.
		const float rate = static_cast<float>(Engine::audioEngine()->outputSampleRate());
		lfo.m_speedModel.setValue(4.f);
		lfo.m_multiplierModel.setValue(1);
		QCOMPARE(static_cast<float>(lfo.m_duration), rate * 4.f / 100.f);
		lfo.m_multiplierModel.setValue(2);
		QCOMPARE(static_cast<float>(lfo.m_duration), rate * 4.f * 100.f);
	}

	//! new-p9-249/new-p14-133: the wave shape is published by the model and the
	//! render boundary derives m_sampleFunction from it. Model the interleaving
	//! from the user wave (null function) to white noise.
	void renderBetweenWaveChangeAndSlotDoesNotCallNullFunction()
	{
		LfoController lfo(nullptr);
		lfo.m_waveModel.setValue(static_cast<int>(Oscillator::WaveShape::UserDefined));
		Controller::triggerFrameCounter();
		QVERIFY(lfo.valueBuffer() != nullptr);
		QVERIFY(static_cast<sample_t (*)(const float)>(lfo.m_sampleFunction.load()) == nullptr);
		{
			QSignalBlocker blockSlot(&lfo.m_waveModel);
			lfo.m_waveModel.setValue(static_cast<int>(Oscillator::WaveShape::WhiteNoise));
		}
		lfo.m_speedModel.setValue(0.01f);
		lfo.m_multiplierModel.setValue(1); // a few frames per cycle, so the noise phase wraps inside one buffer
		Controller::triggerFrameCounter();
		const ValueBuffer* values = lfo.valueBuffer();
		QVERIFY(values != nullptr);
		for (int i = 0; i < values->length(); ++i)
		{
			QVERIFY(std::isfinite(values->value(i)));
			QVERIFY(values->value(i) >= 0.0f && values->value(i) <= 1.0f);
		}
	}

	//! new-p14-133: the white-noise state belongs to the render thread. A GUI
	//! wave-shape change publishes the shape only; it must not write the
	//! render-owned sample function or held sample. The render boundary applies
	//! the shape at the start of the next period.
	void guiWaveShapeChangeLeavesRenderStateUntilTheBoundary()
	{
		LfoController lfo(nullptr);

		// A completed render owns the square wave.
		lfo.m_waveModel.setValue(static_cast<int>(Oscillator::WaveShape::Square));
		Controller::triggerFrameCounter();
		QVERIFY(lfo.valueBuffer() != nullptr);
		QVERIFY(lfo.m_sampleFunction.load() == &Oscillator::squareSample);

		// A NaN sentinel is never produced by the noise generator, so an
		// unchanged value is unambiguous.
		lfo.m_heldSample.store(std::numeric_limits<float>::quiet_NaN());

		// The GUI selects white noise. In the unfixed code this connection ran
		// updateSampleFunction() on the GUI thread and wrote both fields while
		// the render could be reading them.
		lfo.m_waveModel.setValue(static_cast<int>(Oscillator::WaveShape::WhiteNoise));
		QVERIFY2(lfo.m_sampleFunction.load() == &Oscillator::squareSample,
			"the GUI wrote the render-owned sample function");
		QVERIFY2(std::isnan(lfo.m_heldSample.load()),
			"the GUI seeded the render-owned white-noise sample");

		// The render boundary applies the published shape and seeds the held
		// sample on the render thread.
		Controller::triggerFrameCounter();
		QVERIFY(lfo.valueBuffer() != nullptr);
		QVERIFY(lfo.m_sampleFunction.load() == &Oscillator::noiseSample);
		QVERIFY(std::isfinite(lfo.m_heldSample.load()));
	}

	//! new-p14-133 normal path: once the boundary has applied white noise, one
	//! held sample drives the whole period (no phase wrap inside this buffer).
	void whiteNoiseHoldsOneSampleForThePeriod()
	{
		LfoController lfo(nullptr);
		lfo.m_waveModel.setValue(static_cast<int>(Oscillator::WaveShape::WhiteNoise));
		Controller::triggerFrameCounter();
		const ValueBuffer* values = lfo.valueBuffer();
		QVERIFY(values != nullptr);
		QVERIFY(values->length() > 1);
		for (int i = 1; i < values->length(); ++i)
		{
			QCOMPARE(values->value(i), values->value(0));
		}
	}

private:
	template<typename T>
	static void verifyReplacementLifetime()
	{
		T lfo(nullptr);
		auto original = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>{}, 44100);
		std::weak_ptr<const SampleBuffer> originalLifetime = original;
		const auto* originalBuffer = original.get();
		auto replacement = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>{}, 44100);

		if constexpr (HasRenderSafeUserWaveHandoff<T>)
		{
			lfo.setUserDefSampleBuffer(std::move(original));
			QVERIFY(lfo.acquireUserDefSampleBufferForRender() == originalBuffer);
			lfo.setUserDefSampleBuffer(replacement);
			QVERIFY2(!originalLifetime.expired(),
				"replacing the waveform freed a buffer while rendering held its lease");
			lfo.reclaimRetiredUserDefSampleBuffers();
			QVERIFY(!originalLifetime.expired());
			lfo.releaseUserDefSampleBufferForRender();
			lfo.reclaimRetiredUserDefSampleBuffers();
			QVERIFY2(originalLifetime.expired(), "released waveform was not reclaimed on the control thread");
			QCOMPARE(lfo.acquireUserDefSampleBufferForRender(), replacement.get());
			lfo.releaseUserDefSampleBufferForRender();
		}
		else
		{
			// Model the old render-side raw pointer and the dialog's direct shared_ptr assignment.
			lfo.m_userDefSampleBuffer = std::move(original);
			const auto* renderBuffer = lfo.m_userDefSampleBuffer.get();
			QVERIFY(renderBuffer == originalBuffer);
			lfo.m_userDefSampleBuffer = replacement;
			QVERIFY2(!originalLifetime.expired(),
				"replacing the waveform freed a buffer while rendering held its raw pointer");
		}
	}
};

QTEST_GUILESS_MAIN(LfoControllerUserWaveTest)
#include "LfoControllerUserWaveTest.moc"
