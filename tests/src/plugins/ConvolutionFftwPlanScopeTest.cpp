/*
 * ConvolutionFftwPlanScopeTest.cpp - cluster 409 regression.
 *
 * FFTW's planner API - creating a plan, destroying a plan, allocating and freeing the buffers a
 * plan works on - is not thread-safe: it may not run while any thread executes a plan (only
 * fftwf_execute is thread-safe, and only for different plans). The convolution kernel's plans
 * were made in its constructor and destroyed in its destructor, on the control thread, while the
 * audio thread could be executing another instance's plans - the engine's model-change guard
 * (which the render thread holds for every period it renders) was not taken around them.
 *
 * The plans and their buffers now live inside a ConvolutionFftwScope, which takes that guard,
 * so no FFTW planner call can overlap an audio thread's fftwf_execute. The scope counts the
 * planner calls it covered: this test drives a real kernel (and a real effect, through the
 * plugin's own load path) and asserts that no planner call ran outside a scope.
 *
 * The counters are the test seam - there is no timing and no race in this test: the kernel is
 * built and destroyed on this thread, and the scope's counters say where the planner calls ran.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <cmath>
#include <memory>
#include <vector>

#include <QtTest>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConvolutionFftw.h"
#include "ConvolutionKernel.h"
#include "ConvolutionReverb.h"
#include "EffectChain.h"
#include "Engine.h"

using namespace lmms;

class ConvolutionFftwPlanScopeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! A kernel's plans are made and destroyed inside the scope, never outside it.
	void thePlansLiveInsideTheScope()
	{
		const std::vector<float> impulse(ConvolutionKernel::Partition, 0.f);
		const auto guardedBefore = ConvolutionFftwScope::guardedPlannerCalls();
		const auto unguardedBefore = ConvolutionFftwScope::unguardedPlannerCalls();

		{
			std::unique_ptr<ConvolutionKernel> kernel =
				std::make_unique<ConvolutionKernel>(impulse, impulse);
			QVERIFY2(ConvolutionFftwScope::guardedPlannerCalls() > guardedBefore,
				"the kernel's FFTW plans were created outside the scope that quiesces the audio "
				"thread (cluster 409)");
		}

		QVERIFY2(ConvolutionFftwScope::unguardedPlannerCalls() == unguardedBefore,
			"an FFTW plan was created or destroyed while the audio thread could be executing a "
			"plan (cluster 409)");
		QVERIFY2(ConvolutionFftwScope::guardedPlannerCalls() > guardedBefore,
			"the kernel's FFTW plans were never covered by the scope");
	}

	//! The plugin's own load path (a new impulse response) keeps every planner call in a scope.
	void loadingAnImpulseKeepsThePlannerCallsInAScope()
	{
		EffectChain chain{nullptr};
		auto* effect = new ConvolutionReverbEffect(&chain, nullptr);
		const auto unguardedBefore = ConvolutionFftwScope::unguardedPlannerCalls();
		const auto guardedBefore = ConvolutionFftwScope::guardedPlannerCalls();

		QVERIFY(effect->loadImpulse(QString()));
		QVERIFY2(ConvolutionFftwScope::unguardedPlannerCalls() == unguardedBefore,
			"loading an impulse response ran an FFTW planner call outside the scope that "
			"quiesces the audio thread (cluster 409)");
		QVERIFY2(ConvolutionFftwScope::guardedPlannerCalls() > guardedBefore,
			"loading an impulse response ran no FFTW planner call inside a scope at all");

		delete effect;
		QVERIFY2(ConvolutionFftwScope::unguardedPlannerCalls() == unguardedBefore,
			"removing the effect destroyed an FFTW plan outside the scope that quiesces the audio "
			"thread (cluster 409)");
	}

	//! The kernel still convolves: a unit impulse leaves the block unchanged.
	void theKernelStillConvolves()
	{
		std::vector<float> impulse(ConvolutionKernel::Partition, 0.f);
		impulse[0] = 1.f;
		std::unique_ptr<ConvolutionKernel> kernel =
			std::make_unique<ConvolutionKernel>(impulse, impulse);

		float input[2][ConvolutionKernel::Partition];
		float output[2][ConvolutionKernel::Partition];
		for (int c = 0; c < 2; ++c)
		{
			for (int i = 0; i < ConvolutionKernel::Partition; ++i) { input[c][i] = 0.01f * (i + 1); }
		}
		const float* in[2] = {input[0], input[1]};
		float* out[2] = {output[0], output[1]};
		kernel->process(in, out);

		for (int c = 0; c < 2; ++c)
		{
			for (int i = 0; i < ConvolutionKernel::Partition; ++i)
			{
				QVERIFY2(std::abs(output[c][i] - input[c][i]) < 1e-5f,
					"the convolution with a unit impulse is not the identity any more");
			}
		}
	}
};

QTEST_GUILESS_MAIN(ConvolutionFftwPlanScopeTest)

#include "ConvolutionFftwPlanScopeTest.moc"
