/*
 * AutomationPrepassProofTest.cpp - cluster 2647 stage-2 render and teardown proof
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <atomic>
#include <bit>
#include <cstdint>
#include <thread>
#include <vector>

#include "PhaseDMixerTestSupport.h"

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Controller.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "LfoController.h"
#include "Mixer.h"
#include "RealtimeScope.h"

using namespace lmms;
using namespace partd;

namespace
{

void feedDc(PeriodHarness& harness, mix_ch_t channel, float value)
{
	for (f_cnt_t frame = 0; frame < harness.fpp(); ++frame)
	{
		harness.in()[frame][0] = value;
		harness.in()[frame][1] = value;
	}
	harness.feed(channel);
}

void resetMixer(Mixer* mixer)
{
	mixer->clear();
	while (mixer->numChannels() < 3) { mixer->createChannel(); }
}

class VaryingNonLfoController final : public Controller
{
public:
	VaryingNonLfoController() : Controller(ControllerType::Peak, nullptr, QStringLiteral("proof controller"))
	{
		setSampleExact(true);
	}

protected:
	void updateValueBuffer() override
	{
		const int length = m_valueBuffer.length();
		for (int frame = 0; frame < length; ++frame)
		{
			m_valueBuffer.values()[frame] = 0.25f
				+ 0.5f * static_cast<float>(frame) / static_cast<float>(length - 1);
		}
		m_bufferLastUpdated = runningPeriods();
	}
};

QByteArray hash(const std::vector<SampleFrame>& output)
{
	return sha256(output);
}

} // namespace

class AutomationPrepassProofTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		initEngine();
		Engine::audioEngine()->audioDev()->setSampleRateForTesting(48000);
		QCOMPARE(Engine::audioEngine()->outputSampleRate(), 48000u);
	}

	void cleanupTestCase()
	{
		destroyEngine();
	}

	void fullMixerOutputMatchesBaseCommitRenders()
	{
		Mixer* const mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);
		QByteArray rampHash;
		QByteArray controllerHash;
		QByteArray lfoHash;
		QByteArray manualHash;
		QByteArray linkedHash;

		resetMixer(mixer);
		FloatModel* const rampGain = &mixer->mixerChannel(1)->m_volumeModel;
		rampGain->setValue(0.75f);
		{
			PeriodHarness harness(mixer);
			AutomationRamp ramp;
			QVERIFY(ramp.addKnot(0, 0.125f));
			QVERIFY(ramp.addKnot(harness.fpp() - 1, 0.875f));
			rampGain->publishAutomationRamp(ramp);
			feedDc(harness, 1, 0.25f);
			rampHash = hash(harness.render());
		}

		resetMixer(mixer);
		FloatModel* const controllerGain = &mixer->mixerChannel(1)->m_volumeModel;
		auto* const controller = new VaryingNonLfoController;
		auto* const controllerConnection = new ControllerConnection(controller);
		controllerGain->setControllerConnection(controllerConnection);
		controllerGain->setUseControllerValue(true);
		{
			PeriodHarness harness(mixer);
			feedDc(harness, 1, 0.25f);
			controllerHash = hash(harness.render());
		}
		controllerGain->setControllerConnection(nullptr);
		delete controller;

		resetMixer(mixer);
		FloatModel* const lfoGain = &mixer->mixerChannel(1)->m_volumeModel;
		lfoGain->setValue(0.5f);
		auto* const lfo = new LfoController(nullptr);
		auto* const lfoConnection = new ControllerConnection(lfo);
		lfoGain->setControllerConnection(lfoConnection);
		lfoGain->setUseControllerValue(true);
		{
			PeriodHarness harness(mixer);
			feedDc(harness, 1, 0.25f);
			lfoHash = hash(harness.render());
		}
		lfoGain->setControllerConnection(nullptr);
		delete lfo;

		resetMixer(mixer);
		FloatModel* const manualGain = &mixer->mixerChannel(1)->m_volumeModel;
		manualGain->setValue(0.2f);
		{
			PeriodHarness harness(mixer);
			feedDc(harness, 1, 0.75f);
			const std::vector<SampleFrame> renderedAtPointTwo = harness.render();
			QVERIFY(std::fabs(renderedAtPointTwo.front()[0] - 0.15f) < 1.0e-6f);

			// This is the legacy setValueInternal sequence: the previous setter
			// value is 0.5 even though the last rendered value was 0.2.
			manualGain->setValue(0.5f);
			manualGain->setValue(0.9f);
			ValueBuffer oldAlgorithm(harness.fpp());
			oldAlgorithm.interpolate(0.5f, 0.9f);
			ValueBuffer lastRenderedAlternative(harness.fpp());
			lastRenderedAlternative.interpolate(0.2f, 0.9f);
			QVERIFY(std::bit_cast<std::uint32_t>(oldAlgorithm.values()[0])
				!= std::bit_cast<std::uint32_t>(lastRenderedAlternative.values()[0]));

			feedDc(harness, 1, 0.75f);
			const std::vector<SampleFrame> output = harness.render();
			for (f_cnt_t frame = 0; frame < harness.fpp(); ++frame)
			{
				const float expected = 0.75f * oldAlgorithm.values()[frame];
				QCOMPARE(std::bit_cast<std::uint32_t>(output[frame][0]),
					std::bit_cast<std::uint32_t>(expected));
				QCOMPARE(std::bit_cast<std::uint32_t>(output[frame][1]),
					std::bit_cast<std::uint32_t>(expected));
			}
			const float wrongLastRenderedStart = 0.75f * lastRenderedAlternative.values()[0];
			QVERIFY(std::bit_cast<std::uint32_t>(output.front()[0])
				!= std::bit_cast<std::uint32_t>(wrongLastRenderedStart));
			manualHash = hash(output);
		}

		resetMixer(mixer);
		FloatModel* const first = &mixer->mixerChannel(1)->m_volumeModel;
		FloatModel* const second = &mixer->mixerChannel(2)->m_volumeModel;
		first->setValue(0.2f);
		second->setValue(0.2f);
		first->linkToModel(second);
		{
			PeriodHarness harness(mixer);
			feedDc(harness, 1, 0.25f);
			feedDc(harness, 2, 0.5f);
			harness.render();
			first->setValue(0.5f);
			first->setValue(0.9f);
			feedDc(harness, 1, 0.25f);
			feedDc(harness, 2, 0.5f);
			linkedHash = hash(harness.render());
		}

		// Hashes cover every interleaved stereo frame from the full master mix.
		// They were captured twice from a BUILD_TESTING build of base c405233f8;
		// the reproduction command is recorded in automation-prepass.md.
		QCOMPARE(rampHash,
			QByteArrayLiteral("a7348883c00f4f81640c7f0f022a02d4ed706e60312431da5fcf5024d18c5a69"));
		QCOMPARE(controllerHash,
			QByteArrayLiteral("2c66cf0aa6d576d4259b58ea033b76717707f45abfa31e80d0c425c98cf3ec09"));
		QCOMPARE(lfoHash,
			QByteArrayLiteral("bc60bfe085f932a2d312bcf9ef4a7988aebb26059316d904d005e58b93488a15"));
		QCOMPARE(manualHash,
			QByteArrayLiteral("ad70d7f35245e58080ca946254d2d49c17d3834eb2d1494acffe354dde3012d8"));
		QCOMPARE(linkedHash,
			QByteArrayLiteral("ad70d7f35245e58080ca946254d2d49c17d3834eb2d1494acffe354dde3012d8"));

		evidence("AUTOMATION_PREPASS_BASELINE source=ramp sha256=%s", rampHash.constData());
		evidence("AUTOMATION_PREPASS_BASELINE source=controller sha256=%s", controllerHash.constData());
		evidence("AUTOMATION_PREPASS_BASELINE source=lfo sha256=%s", lfoHash.constData());
		evidence("AUTOMATION_PREPASS_BASELINE source=manual-0.2-0.5-0.9 sha256=%s", manualHash.constData());
		evidence("AUTOMATION_PREPASS_BASELINE source=linked-models sha256=%s", linkedHash.constData());
		QVERIFY(!rampHash.isEmpty());
		QVERIFY(!controllerHash.isEmpty());
		QVERIFY(!lfoHash.isEmpty());
		QVERIFY(!manualHash.isEmpty());
		QVERIFY(!linkedHash.isEmpty());
	}

#ifdef LMMS_AUTOMATION_PREPASS_DESTRUCTOR_TEST
	void workerRealtimeContextIsRejectedByModelDestructorGuard()
	{
		QVERIFY(!AutomatableModel::destructorWouldRequestBlockingChangeGuard());
		std::atomic<bool> workerWasRealtime{false};
		std::atomic<bool> workerWouldBlock{false};
		std::thread worker([&] {
			const RealtimeScope realtime;
			workerWasRealtime.store(RealtimeScope::isActive(), std::memory_order_relaxed);
			workerWouldBlock.store(
				AutomatableModel::destructorWouldRequestBlockingChangeGuard(),
				std::memory_order_relaxed);
		});
		worker.join();
		QVERIFY(workerWasRealtime.load(std::memory_order_relaxed));
		QVERIFY(workerWouldBlock.load(std::memory_order_relaxed));
	}
#endif
};

QTEST_GUILESS_MAIN(AutomationPrepassProofTest)
#include "AutomationPrepassProofTest.moc"
