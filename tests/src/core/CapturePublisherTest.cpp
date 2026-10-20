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
#include "AudioWideInputStage.h"
#include "ConfigManager.h"
#include "Engine.h"

using namespace lmms;

namespace
{

class FakeCaptureDevice : public AudioDevice
{
public:
	explicit FakeCaptureDevice(AudioEngine* engine) : AudioDevice(DEFAULT_CHANNELS, engine) {}
	void prepare(f_cnt_t frames, int left, int right) { prepareCapture(frames, left, right); }
	//! What JACK, SDL and PortAudio call: the block size only, no pair.
	void prepare(f_cnt_t frames) { prepareCapture(frames); }
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
		Engine::audioEngine()->audioDev()->stopProcessing();
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

	//! Luna cluster 4595: JACK, SDL and PortAudio call prepareCapture() with the block size
	//! only, so the stereo bus kept the hard-coded pair 0/1 and the configured capture
	//! channels never reached it. The default pair must come from the configuration.
	void aConfiguredPairReachesTheStereoBusWhenTheBackendPassesNone()
	{
		AudioEngine* engine = Engine::audioEngine();
		ConfigManager* config = ConfigManager::inst();
		config->setValue(QStringLiteral("audioinput"), QStringLiteral("channels"), QStringLiteral("4"));
		config->setValue(QStringLiteral("audioinput"), QStringLiteral("left"), QStringLiteral("2"));
		config->setValue(QStringLiteral("audioinput"), QStringLiteral("right"), QStringLiteral("3"));

		engine->renderNextPeriod();
		FakeCaptureDevice device(engine);
		device.prepare(64);  // no pair: exactly what JACK/SDL/PortAudio do
		device.publish(fourChannelBlock(64), 4);
		engine->renderNextPeriod();

		const SampleFrame* bus = engine->inputBuffer();
		const f_cnt_t read = engine->inputBufferFrames();
		QVERIFY(read > 0);
		for (f_cnt_t i = 0; i < read && i < 64; ++i)
		{
			QCOMPARE(bus[i][0], static_cast<float>(2000 + i));
			QCOMPARE(bus[i][1], static_cast<float>(3000 + i));
		}

		// The N-channel path still carries every channel, pair or no pair.
		QCOMPARE(engine->inputWideChannels(), 4);
		QCOMPARE(engine->inputWideBuffer()[1], 1000.0f);

		config->setValue(QStringLiteral("audioinput"), QStringLiteral("channels"), QString::number(AudioInputPath::DefaultChannels));
		config->setValue(QStringLiteral("audioinput"), QStringLiteral("left"), QStringLiteral("0"));
		config->setValue(QStringLiteral("audioinput"), QStringLiteral("right"), QStringLiteral("1"));
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

	//! Luna cluster 3705: the N-CHANNEL block and the stereo pair are TWO REPRESENTATIONS
	//! OF ONE CAPTURE BLOCK. The publisher pushes the wide block first and the stereo pair
	//! second, so a render that lands between the two pushes used to consume one
	//! representation now and the other in the next period: the recorder took the wide
	//! block in one period and fell back to the same stereo block in the next - the same
	//! audio twice, and the stereo bus a whole block behind the recorders. The two must
	//! be drained as one block.
	void theWideBlockAndItsStereoPairAreDrainedAsOneBlock()
	{
		AudioEngine* engine = Engine::audioEngine();
		engine->renderNextPeriod();  // drain whatever the Dummy left behind
		const f_cnt_t frames = 64;
		const std::vector<float> wide(static_cast<std::size_t>(frames) * 2, 0.25f);
		// The capture thread is preempted between the two pushes: the wide block is
		// published, its stereo pair is not.
		engine->pushInputFramesWide(wide.data(), 2, frames);
		engine->renderNextPeriod();
		// Neither representation may be consumed on its own.
		QCOMPARE(engine->inputWideFrames(), f_cnt_t{0});
		QCOMPARE(engine->inputBufferFrames(), f_cnt_t{0});
		// The stereo pair of the SAME block arrives...
		const std::vector<SampleFrame> pair(static_cast<std::size_t>(frames), SampleFrame(0.25f, 0.5f));
		engine->pushInputFrames(pair.data(), frames);
		engine->renderNextPeriod();
		// ... and now both representations of that one block are consumed, together.
		QCOMPARE(engine->inputWideFrames(), frames);
		QCOMPARE(engine->inputBufferFrames(), frames);
		QCOMPARE(engine->inputBuffer()[0][0], 0.25f);
		QCOMPARE(engine->inputBuffer()[0][1], 0.5f);
	}

	//! The two representations are one block: a block the wide path refuses whole (a
	//! width change while frames are still staged) must not be published to the stereo
	//! path ALONE - the two paths would then be one block apart, and the recorder would
	//! take the wide block in one period and this one in the next.
	void aBlockTheWidePathRefusesIsNotPublishedToTheStereoPathAlone()
	{
		AudioEngine* engine = Engine::audioEngine();
		engine->renderNextPeriod();
		// A four-channel block that is NOT drained: the wide ring still holds it, so
		// the next block's width cannot be adopted and the wide path refuses it.
		const std::vector<float> four(64 * 4, 0.5f);
		engine->pushInputFramesWide(four.data(), 4, 64);
		const std::size_t stereoBefore = engine->inputFramesStaged();
		FakeCaptureDevice device(engine);
		device.prepare(64, 0, 1);
		const std::vector<float> two(64 * 2, 0.25f);
		device.publish(two, 2);
		// The wide path refused the two-channel block, so the stereo path must not
		// take it alone.
		QCOMPARE(engine->inputFramesStaged(), stereoBefore);
	}

	//! BUGS_FOUND 11.9: the stage's width was fixed at its 128-channel storage, so a real
	//! device's block was always refused. It now carries the producer's width, changing it
	//! only once every block of the old width has been drained.
	void theWideStageCarriesTheDevicesWidthAndChangesItOnlyWhenEmpty()
	{
		AudioWideInputStage stage(AudioInputPath::MaxChannels, 256);
		const std::vector<float> stereo(64 * 2, 0.5f);
		const std::vector<float> eight(64 * 8, 0.25f);
		QCOMPARE(stage.push(stereo.data(), 2, 64), std::size_t{64});
		QCOMPARE(stage.push(eight.data(), 8, 64), std::size_t{0});  // stereo still unread
		stage.drain();
		QCOMPARE(stage.channels(), 2);
		QCOMPARE(stage.frames(), f_cnt_t{64});
		QCOMPARE(stage.push(eight.data(), 8, 64), std::size_t{64});  // empty: adopted
		stage.drain();
		QCOMPARE(stage.channels(), 8);
		QCOMPARE(stage.data()[7], 0.25f);
		QCOMPARE(stage.push(eight.data(), AudioInputPath::MaxChannels + 1, 1), std::size_t{0});
	}
};

QTEST_GUILESS_MAIN(CapturePublisherTest)
#include "CapturePublisherTest.moc"
