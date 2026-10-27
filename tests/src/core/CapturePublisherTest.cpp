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

#include <atomic>
#include <thread>
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

//! Q050-146: global frame g, channel c carries g * 4 + c (exact in a float below 2^22 frames),
//! so a channel-2/3 sample names the capture frame it came from.
std::vector<float> uniqueBlock(unsigned firstFrame, int frames)
{
	std::vector<float> block(static_cast<std::size_t>(frames) * 4);
	for (int f = 0; f < frames; ++f)
	{
		for (int c = 0; c < 4; ++c)
		{
			block[static_cast<std::size_t>(f * 4 + c)] = static_cast<float>((firstFrame + static_cast<unsigned>(f)) * 4 + static_cast<unsigned>(c));
		}
	}
	return block;
}

//! The period the engine just rendered: both views hold the same number of frames, the stereo
//! bus is the (2, 3) pair of the wide frames, and frame 0 is capture frame \a firstFrame.
//! Returns the number of frames compared, or -1 on the first disagreement (described in \a why).
int comparePeriod(AudioEngine* engine, unsigned firstFrame, QString* why)
{
	const f_cnt_t bus = engine->inputBufferFrames();
	const f_cnt_t wide = engine->inputWideFrames();
	if (bus != wide)
	{
		*why = QStringLiteral("stereo bus holds %1 frames, wide stage %2").arg(bus).arg(wide);
		return -1;
	}
	if (wide == 0) { return 0; }
	const float* w = engine->inputWideBuffer();
	const SampleFrame* b = engine->inputBuffer();
	for (f_cnt_t i = 0; i < wide; ++i)
	{
		const float wl = w[static_cast<std::size_t>(i) * 4 + 2];
		const float wr = w[static_cast<std::size_t>(i) * 4 + 3];
		const float expectedLeft = static_cast<float>((firstFrame + static_cast<unsigned>(i)) * 4 + 2);
		if (b[i][0] != wl || b[i][1] != wr || wl != expectedLeft)
		{
			*why = QStringLiteral("frame %1: bus (%2, %3), wide (%4, %5), expected left %6")
				.arg(i).arg(b[i][0]).arg(b[i][1]).arg(wl).arg(wr).arg(expectedLeft);
			return -1;
		}
	}
	return static_cast<int>(wide);
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

	//! A block published after the render's staged-frame snapshot belongs to the next
	//! drain. This is the capture/render interleaving where the mode flag can still read
	//! false; the first render must not drain newly arrived wide frames on its own.
	void aWideBlockPublishedAfterTheSnapshotWaitsForTheNextDrain()
	{
		AudioWideInputStage stage(AudioInputPath::MaxChannels, 128);
		const std::size_t observed = stage.stagedFrames();  // render's snapshot: empty
		const std::vector<float> wide(64 * 2, 0.25f);
		QCOMPARE(stage.push(wide.data(), 2, 64), std::size_t{64});
		stage.drain(static_cast<f_cnt_t>(observed));
		QCOMPARE(stage.frames(), f_cnt_t{0});
		QCOMPARE(stage.stagedFrames(), std::size_t{64});
		stage.drain(64);
		QCOMPARE(stage.frames(), f_cnt_t{64});
	}

	//! Q050-146: a render period that lands BETWEEN the wide push and the stereo push of one
	//! captured block used to take the block from the wide ring only; the stereo bus then ran one
	//! block behind the wide channels for every consumer. The schedule below is that interleaving,
	//! step for step what publishCaptured() does (wide push, then stereo push).
	void aDrainBetweenTheWideAndStereoPushesKeepsBothViewsOnOneBlock()
	{
		AudioEngine* engine = Engine::audioEngine();
		// Start from empty rings, as a device start does: a wide block an earlier case left
		// staged without its stereo pair (cluster 3705's refusal case) is drained on its own.
		engine->resetInputStream();
		engine->renderNextPeriod();
		engine->renderNextPeriod();  // empty both rings
		FakeCaptureDevice device(engine);
		device.prepare(64, 2, 3);
		QString why;

		device.publish(uniqueBlock(0, 64), 4);  // block A, whole
		const auto b = uniqueBlock(64, 64);  // block B, published in steps
		engine->pushInputFramesWide(b.data(), 4, 64);
		engine->renderNextPeriod();  // the drain lands between the two pushes
		const int midFrames = comparePeriod(engine, 0, &why);
		QVERIFY2(midFrames >= 0, qPrintable(why));
		QVERIFY2(midFrames == 0 || midFrames == 64, "a mid-publication period holds nothing or block A only");

		std::vector<SampleFrame> pair(64);
		for (std::size_t i = 0; i < 64; ++i) { pair[i] = SampleFrame(b[i * 4 + 2], b[i * 4 + 3]); }
		engine->pushInputFrames(pair.data(), 64);
		engine->renderNextPeriod();  // the next period takes the rest, from both views
		const int rest = comparePeriod(engine, static_cast<unsigned>(midFrames), &why);
		QVERIFY2(rest >= 0, qPrintable(why));
		QCOMPARE(midFrames + rest, 128);  // A and B, none lost, none repeated, none one block behind

		// A drain that follows a completed publication takes the block from both at once.
		device.publish(uniqueBlock(128, 64), 4);
		engine->renderNextPeriod();
		QCOMPARE(comparePeriod(engine, 128, &why), 64);
	}

	//! Q050-146: the same property under a real producer thread calling publishCaptured() while
	//! the test thread renders as fast as it can. Every period must hold the same capture frames
	//! in both views, and the periods together must be gap-free.
	void aRenderThreadDrainingWhileABlockIsPublishedNeverSkewsTheViews()
	{
		AudioEngine* engine = Engine::audioEngine();
		engine->resetInputStream();  // empty rings, as a device start
		engine->renderNextPeriod();
		engine->renderNextPeriod();
		FakeCaptureDevice device(engine);
		device.prepare(64, 2, 3);
		constexpr unsigned Blocks = 3000;
		std::atomic<bool> done{false};
		std::thread producer([&] {
			for (unsigned k = 0; k < Blocks; ++k)
			{
				// Pace on the consumer so neither ring overflows: the property is alignment, not drops.
				while (engine->inputWideFramesStaged() > 4096 || engine->inputFramesStaged() > 4096) { std::this_thread::yield(); }
				device.publish(uniqueBlock(k * 64, 64), 4);
			}
			done.store(true, std::memory_order_release);
		});
		unsigned next = 0;
		int bad = 0;
		QString first;
		bool last = false;
		while (!last)
		{
			last = done.load(std::memory_order_acquire);  // read first: one more period after the producer ends
			engine->renderNextPeriod();
			QString why;
			const int frames = comparePeriod(engine, next, &why);
			if (frames < 0)
			{
				// Keep draining so the paced producer can finish; only the first disagreement is reported.
				if (bad++ == 0) { first = why; }
				next += static_cast<unsigned>(engine->inputWideFrames());
				continue;
			}
			next += static_cast<unsigned>(frames);
		}
		producer.join();
		engine->renderNextPeriod();
		QString why;
		const int tail = comparePeriod(engine, next, &why);
		QVERIFY2(bad == 0 && tail >= 0, qPrintable(first.isEmpty() ? why : first));
		next += static_cast<unsigned>(tail);
		QCOMPARE(next, Blocks * 64u);
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
	//! Cluster new-p14-402: a block captured before the device stopped and never rendered
	//! must not survive the restart. The device start (resetInputStream) empties both staging
	//! rings, so the first period after it neither drains those frames nor reports the input
	//! stream as started, and the first block captured after the restart is what the engine
	//! hears first.
	void aRestartDropsTheFramesStagedBeforeTheStop()
	{
		AudioEngine* engine = Engine::audioEngine();
		// Start from empty rings: drain whatever an earlier case left behind.
		for (int i = 0; i < 8 && (engine->inputFramesStaged() > 0 || engine->inputWideFramesStaged() > 0); ++i)
		{
			engine->renderNextPeriod();
		}
		QCOMPARE(engine->inputFramesStaged(), std::size_t{0});
		QCOMPARE(engine->inputWideFramesStaged(), std::size_t{0});

		FakeCaptureDevice device(engine);
		device.prepare(64, 2, 3);
		device.publish(uniqueBlock(0, 64), 4);  // captured, staged, and the device stops before a render
		QCOMPARE(engine->inputFramesStaged(), std::size_t{64});
		QCOMPARE(engine->inputWideFramesStaged(), std::size_t{64});

		engine->resetInputStream();  // the device (re)start

		QCOMPARE(engine->inputFramesStaged(), std::size_t{0});
		QCOMPARE(engine->inputWideFramesStaged(), std::size_t{0});
		engine->renderNextPeriod();
		QVERIFY2(!engine->inputStreamStarted(), "the period after a restart took frames captured before the stop");
		QCOMPARE(engine->inputBufferFrames(), f_cnt_t{0});

		device.publish(uniqueBlock(64, 64), 4);  // the first block captured after the restart
		engine->renderNextPeriod();
		QString why;
		const int fresh = comparePeriod(engine, 64, &why);
		QVERIFY2(fresh >= 0, qPrintable(why));
		QCOMPARE(fresh, 64);
	}
};

QTEST_GUILESS_MAIN(CapturePublisherTest)
#include "CapturePublisherTest.moc"
