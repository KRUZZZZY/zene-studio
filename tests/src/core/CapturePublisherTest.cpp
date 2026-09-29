/*
 * CapturePublisherTest.cpp - R2.3: one capture publisher for every backend
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

/*! AudioDevice::publishCaptured is what ALSA, JACK, SDL and PortAudio all call now. A fake
 *  device drives it with a four-channel block and the test reads each consumer back from the
 *  engine: the N-channel path holds every frame, the stereo bus carries exactly the selected
 *  pair (channels 2 and 3 here, not the first two), a block longer than the prepared bus
 *  arrives whole (carried in pieces), and the capture counter moves by the block. The Dummy
 *  device's own render thread is stopped first, so the test is the only renderer.
 */

#include <QtTest>

#include <vector>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioInputPath.h"
#include "Engine.h"

using namespace lmms;

namespace
{

class FakeCaptureDevice : public AudioDevice
{
public:
	explicit FakeCaptureDevice(AudioEngine* engine) : AudioDevice(DEFAULT_CHANNELS, engine) {}
	void prepare(f_cnt_t frames, int left, int right) { prepareCapture(frames, left, right); }
	void publish(const std::vector<float>& block, int channels)
	{
		publishCaptured(block.data(), channels, static_cast<f_cnt_t>(block.size() / static_cast<std::size_t>(channels)));
	}

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}
};

//! Channel c of frame f carries c * 1000 + f, so every sample names where it came from.
std::vector<float> fourChannelBlock(int frames)
{
	std::vector<float> block(static_cast<std::size_t>(frames) * 4);
	for (int f = 0; f < frames; ++f)
	{
		for (int c = 0; c < 4; ++c) { block[static_cast<std::size_t>(f * 4 + c)] = static_cast<float>(c * 1000 + f); }
	}
	return block;
}

} // namespace

class CapturePublisherTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	void oneBlockFeedsEveryConsumerWithTheSelectedPair()
	{
		AudioEngine* engine = Engine::audioEngine();
		engine->renderNextPeriod();  // drain whatever the Dummy left behind
		FakeCaptureDevice device(engine);
		device.prepare(64, 2, 3);
		const int frames = 100;  // longer than the 64-frame bus: carried in two pieces
		const std::size_t stagedBefore = engine->inputFramesStaged();
		const std::size_t wideBefore = engine->inputWideFramesStaged();
		const std::uint64_t countedBefore = AudioInputPath::live().framesCaptured;

		device.publish(fourChannelBlock(frames), 4);
		QCOMPARE(engine->inputFramesStaged() - stagedBefore, std::size_t{frames});
		QCOMPARE(engine->inputWideFramesStaged() - wideBefore, std::size_t{frames});
		QCOMPARE(AudioInputPath::live().framesCaptured - countedBefore, std::uint64_t{frames});

		engine->renderNextPeriod();
		const SampleFrame* bus = engine->inputBuffer();
		const f_cnt_t read = engine->inputBufferFrames();
		QVERIFY(read > 0);
		for (f_cnt_t i = 0; i < read && i < frames; ++i)
		{
			QCOMPARE(bus[i][0], static_cast<float>(2000 + i));
			QCOMPARE(bus[i][1], static_cast<float>(3000 + i));
		}
		QCOMPARE(engine->inputWideChannels(), 4);
		QCOMPARE(engine->inputWideBuffer()[1], 1000.0f);  // frame 0, channel 1: the wide path holds all four
	}

	void aMonoDeviceFeedsItsChannelToBothSidesAndPublishingDoesNotAllocate()
	{
		AudioEngine* engine = Engine::audioEngine();
		FakeCaptureDevice device(engine);
		device.prepare(64, 0, 1);
		std::vector<float> mono(64);
		for (int f = 0; f < 64; ++f) { mono[static_cast<std::size_t>(f)] = static_cast<float>(f); }
		engine->renderNextPeriod();
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		device.publish(mono, 1);
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
		engine->renderNextPeriod();
		QCOMPARE(engine->inputBuffer()[5][0], 5.0f);
		QCOMPARE(engine->inputBuffer()[5][1], 5.0f);
	}
};

QTEST_GUILESS_MAIN(CapturePublisherTest)
#include "CapturePublisherTest.moc"
