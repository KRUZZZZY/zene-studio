/*
 * GranularPitchShifterRealtimeTest.cpp - sample-rate handoff regression test
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDomDocument>
#include <QLibrary>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <thread>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Effect.h"
#include "GranularPitchShifterEffect.h"
#include "Plugin.h"
#include "RealtimeScope.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

constexpr sample_rate_t InitialRate = 44100;
constexpr sample_rate_t ChangedRate = 48000;
constexpr f_cnt_t RenderFrames = 128;
constexpr std::size_t CaptureFrames = 24000;
constexpr std::size_t IgnoreFrames = 2400;
constexpr double InputFrequency = 440.0;
constexpr double ExpectedShiftedFrequency = InputFrequency * 1.0594630943592953;

class FixedRateAudioDevice final : public AudioDevice
{
public:
	FixedRateAudioDevice(AudioEngine* engine, sample_rate_t sampleRate) :
		AudioDevice(DEFAULT_CHANNELS, engine)
	{
		setSampleRateForTesting(sampleRate);
	}

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}
};

struct RenderState
{
	std::array<float, CaptureFrames> rendered{};
	std::atomic<sample_rate_t> inputRate{InitialRate};
	std::atomic<std::uint32_t> blocksProcessed{0};
	std::atomic<bool> captureAfterRateChange{false};
	std::atomic<bool> captureComplete{false};
	std::atomic<bool> stopAudio{false};
	std::size_t capturedFrames = 0;
	std::uint64_t callbackAllocations = 0;
	std::uint64_t maxCallbackNanoseconds = 0;
};

void renderAudioCallbacks(Effect* effect, RenderState& state)
{
	std::array<SampleFrame, RenderFrames> samples{};
	SampleFrame* busData[1] = {samples.data()};
	AudioBus bus{busData, 1, RenderFrames};
	double phase = 0.0;
	test::resetAllocationCount();

	while (!state.stopAudio.load(std::memory_order_acquire))
	{
		const double rate = static_cast<double>(state.inputRate.load(std::memory_order_acquire));
		const double phaseStep = 2.0 * std::numbers::pi * InputFrequency / rate;
		for (f_cnt_t frame = 0; frame < RenderFrames; ++frame)
		{
			const float sample = static_cast<float>(0.5 * std::sin(phase));
			phase += phaseStep;
			if (phase >= 2.0 * std::numbers::pi) { phase -= 2.0 * std::numbers::pi; }
			samples[frame][0] = sample;
			samples[frame][1] = sample;
		}
		bus.quietChannels().reset();

		const auto callbackStart = std::chrono::steady_clock::now();
		test::tlCountAllocations = true;
		{
			RealtimeScope realtime;
			effect->processAudioBuffer(bus);
		}
		test::tlCountAllocations = false;
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - callbackStart).count();
		state.maxCallbackNanoseconds = std::max(state.maxCallbackNanoseconds,
			static_cast<std::uint64_t>(elapsed));
		state.blocksProcessed.fetch_add(1, std::memory_order_release);

		if (state.captureAfterRateChange.load(std::memory_order_acquire))
		{
			for (f_cnt_t frame = 0; frame < RenderFrames && state.capturedFrames < CaptureFrames; ++frame)
			{
				state.rendered[state.capturedFrames++] = samples[frame][0];
			}
			if (state.capturedFrames == CaptureFrames)
			{
				state.captureComplete.store(true, std::memory_order_release);
				state.stopAudio.store(true, std::memory_order_release);
			}
		}
	}

	state.callbackAllocations = test::tlAllocationCount;
}

} // namespace

class GranularPitchShifterRealtimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void sampleRateHandoffDoesNotAllocateAndKeepsPitchShift();
	void invalidRangeIsClamped();

private:
	QLibrary m_plugin;
	using PluginEntry = Plugin* (*)(Model*, void*);
	PluginEntry m_pluginEntry = nullptr;
};

void GranularPitchShifterRealtimeTest::initTestCase()
{
#ifdef Q_OS_WIN
	QSKIP("a test host cannot load plugin modules on Windows; the product loads them inside zene.exe");
#endif

	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
	Engine::audioEngine()->setAudioDevice(
		new FixedRateAudioDevice(Engine::audioEngine(), InitialRate), false);

	m_plugin.setFileName(QStringLiteral(GRANULAR_PLUGIN_PATH));
	m_plugin.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(m_plugin.load(), qPrintable(m_plugin.errorString()));
	m_pluginEntry = reinterpret_cast<PluginEntry>(m_plugin.resolve("lmms_plugin_main"));
	QVERIFY(m_pluginEntry != nullptr);
}

void GranularPitchShifterRealtimeTest::cleanupTestCase()
{
	Engine::destroy();
}

void GranularPitchShifterRealtimeTest::sampleRateHandoffDoesNotAllocateAndKeepsPitchShift()
{
	auto effect = std::unique_ptr<Effect>(
		static_cast<Effect*>(m_pluginEntry(nullptr, nullptr)));
	QVERIFY(effect != nullptr);

	RenderState state;
	std::thread audioThread([&] { renderAudioCallbacks(effect.get(), state); });

	const auto warmDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (state.blocksProcessed.load(std::memory_order_acquire) < 32 &&
		std::chrono::steady_clock::now() < warmDeadline)
	{
		std::this_thread::yield();
	}
	const bool warmed = state.blocksProcessed.load(std::memory_order_acquire) >= 32;

	Engine::audioEngine()->setAudioDevice(
		new FixedRateAudioDevice(Engine::audioEngine(), ChangedRate), false);
	state.inputRate.store(ChangedRate, std::memory_order_release);
	QCoreApplication::processEvents();
	// Changing the persisted range invokes the same control path as a UI change.
	// With the old implementation this sets the callback flag, so the next block
	// resizes the ring buffer on the audio thread and the allocation probe fails.
	QDomDocument settings;
	QVERIFY(settings.setContent(QStringLiteral(
		"<effect><GranularPitchShifterControls><range value=\"1\"/></GranularPitchShifterControls></effect>")));
	effect->loadSettings(settings.documentElement());
	state.captureAfterRateChange.store(true, std::memory_order_release);

	const auto captureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!state.captureComplete.load(std::memory_order_acquire) &&
		std::chrono::steady_clock::now() < captureDeadline)
	{
		std::this_thread::yield();
	}
	state.stopAudio.store(true, std::memory_order_release);
	audioThread.join();

	QVERIFY2(warmed, "the effect did not render its initial-rate blocks");
	QVERIFY2(state.captureComplete.load(std::memory_order_acquire), "the changed-rate render timed out");
	QCOMPARE(state.callbackAllocations, std::uint64_t{0});
	QVERIFY2(state.maxCallbackNanoseconds < 100000000,
		qPrintable(QStringLiteral("callback took %1 ns").arg(state.maxCallbackNanoseconds)));

	std::size_t risingCrossings = 0;
	for (std::size_t frame = IgnoreFrames + 1; frame < CaptureFrames; ++frame)
	{
		if (state.rendered[frame - 1] <= 0.0f && state.rendered[frame] > 0.0f) { ++risingCrossings; }
	}
	const double measuredFrequency = static_cast<double>(risingCrossings) * ChangedRate /
		static_cast<double>(CaptureFrames - IgnoreFrames);
	qInfo("Granular rate handoff: callback allocations=%llu, max callback=%llu ns, "
		"measured pitch=%g Hz (expected %g Hz)",
		static_cast<unsigned long long>(state.callbackAllocations),
		static_cast<unsigned long long>(state.maxCallbackNanoseconds), measuredFrequency,
		ExpectedShiftedFrequency);
	QVERIFY2(std::fabs(measuredFrequency - ExpectedShiftedFrequency) < 18.0,
		qPrintable(QStringLiteral("pitch render measured %1 Hz; expected near %2 Hz")
			.arg(measuredFrequency).arg(ExpectedShiftedFrequency)));
}

void GranularPitchShifterRealtimeTest::invalidRangeIsClamped()
{
	auto effect = std::unique_ptr<Effect>(
		static_cast<Effect*>(m_pluginEntry(nullptr, nullptr)));
	QVERIFY(effect != nullptr);

	QDomDocument settings;
	QVERIFY(settings.setContent(QStringLiteral(
		"<effect><GranularPitchShifterControls><range value=\"99\"/></GranularPitchShifterControls></effect>")));
	effect->loadSettings(settings.documentElement());

	std::array<SampleFrame, RenderFrames> samples{};
	SampleFrame* busData[1] = {samples.data()};
	AudioBus bus{busData, 1, RenderFrames};
	bus.quietChannels().reset();
	effect->processAudioBuffer(bus);
	QVERIFY(true);
}

QTEST_GUILESS_MAIN(GranularPitchShifterRealtimeTest)

#include "GranularPitchShifterRealtimeTest.moc"
