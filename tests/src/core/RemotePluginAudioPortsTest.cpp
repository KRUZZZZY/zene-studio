/*
 * RemotePluginAudioPortsTest.cpp - buffer attach/detach lifecycle of the
 *                                  remote plugin audio-ports controller, and
 *                                  the layout contract of the shared audio
 *                                  block it allocates
 *
 * Copyright (c) 2026 LMMS developers
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

#include "RemotePluginAudioPorts.h"

#include <QtTest>

#include <algorithm>

#include "AudioEngine.h"
#include "AudioPortsModel.h"
#include "Engine.h"
#include "RemotePlugin.h"

using namespace lmms;

namespace
{

//! AudioPortsModel is abstract (bufferPropertiesChanging)
class TestAudioPortsModel : public AudioPortsModel
{
public:
	using AudioPortsModel::AudioPortsModel;

protected:
	void bufferPropertiesChanging(ch_cnt_t, ch_cnt_t, f_cnt_t) override {}
};

//! Concrete controller; exposes the protected buffer pointer for assertions
class TestAudioPortsController : public RemotePluginAudioPortsController
{
public:
	explicit TestAudioPortsController(AudioPortsModel& model)
		: RemotePluginAudioPortsController{model}
	{
	}

	void activate(f_cnt_t /*frames*/) override { ++m_activateCalls; }

	auto buffers() const -> RemotePlugin* { return m_buffers; }
	auto activateCalls() const -> int { return m_activateCalls; }

private:
	int m_activateCalls = 0;
};

//! Channel counts that are only known once the remote plugin reports them, i.e.
//! the shape `VestigeAudioPorts`/`VstEffectAudioPorts` use (#589).
constexpr auto DynamicPlanarSettings = AudioPortsSettings {
	.kind = AudioDataKind::F32,
	.interleaved = false
};

//! Real remote-ports implementation, so the shared audio block of a
//! `RemotePlugin` is allocated by exactly the code the plugins use.
class TestRemotePluginAudioPorts final : public RemotePluginAudioPorts<DynamicPlanarSettings>
{
public:
	using RemotePluginAudioPorts<DynamicPlanarSettings>::RemotePluginAudioPorts;

	auto channelName(ch_cnt_t, bool) const -> QString override { return {}; }
};

//! Offset of plane `channel` from the block's base pointer, in samples
constexpr auto planeOffset(ch_cnt_t channel, f_cnt_t frames) -> std::ptrdiff_t
{
	return static_cast<std::ptrdiff_t>(channel) * frames;
}

} // namespace

class RemotePluginAudioPortsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// The ports model takes its frame count from the audio engine, and the
		// device buffers it allocates are sized from it.
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! RemotePlugin's ctor attaches its buffers to the ports and its dtor
	//! detaches them again; both are the controller's documented entry points.
	void ConnectAndDisconnectBuffers()
	{
		TestAudioPortsModel model{false};
		TestAudioPortsController controller{model};

		QCOMPARE(controller.buffers(), nullptr);
		QCOMPARE(&controller.audioPortsModel(), static_cast<AudioPortsModel*>(&model));

		{
			RemotePlugin plugin{controller};
			QCOMPARE(controller.buffers(), &plugin);
		}
		QCOMPARE(controller.buffers(), nullptr);

		controller.activate(512);
		QCOMPARE(controller.activateCalls(), 1);
	}

	/**
	 * The shared audio block layout `RemotePlugin::updateAudioBuffer()` and
	 * `RemotePluginClient::doProcessing()` both speak (#589):
	 *
	 *   - it is one flat array of exactly (channelsIn + channelsOut) * frames floats;
	 *   - it is channel-major planar: `channelsIn` blocks of `frames` floats,
	 *     then `channelsOut` blocks of `frames` floats;
	 *   - the ports model's channel counts are the counts it was allocated for;
	 *   - the input and output views are disjoint.
	 *
	 * Reading that block as interleaved sample frames - what the retired
	 * pre-migration code did - is the regression this test exists to catch.
	 */
	void updateAudioBufferLayout()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		constexpr auto channelsIn = ch_cnt_t{3};
		constexpr auto channelsOut = ch_cnt_t{2};

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		// Nothing is allocated until the ports learn the channel counts and are
		// activated (see RemotePlugin::waitForInitDone()).
		QCOMPARE(ports.buffers()->initialized(), false);
		QCOMPARE(ports.active(), false);

		ports.audioPortsModel().setChannelCounts(channelsIn, channelsOut);

		// The model reports the counts the block was allocated for
		QCOMPARE(ports.audioPortsModel().in().channelCount(), channelsIn);
		QCOMPARE(ports.audioPortsModel().out().channelCount(), channelsOut);
		QCOMPARE(ports.active(), true);
		QCOMPARE(ports.buffers()->initialized(), true);
		QCOMPARE(ports.buffers()->frames(), frames);

		auto in = ports.buffers()->input();
		auto out = ports.buffers()->output();
		QCOMPARE(in.channels(), channelsIn);
		QCOMPARE(out.channels(), channelsOut);
		QCOMPARE(in.frames(), frames);
		QCOMPARE(out.frames(), frames);

		// Every plane of a side is exactly `frames` floats after the previous one
		for (ch_cnt_t channel = 1; channel < channelsIn; ++channel)
		{
			QCOMPARE(in.bufferPtr(channel) - in.bufferPtr(channel - 1), frames);
		}
		for (ch_cnt_t channel = 1; channel < channelsOut; ++channel)
		{
			QCOMPARE(out.bufferPtr(channel) - out.bufferPtr(channel - 1), frames);
		}

		// The output planes start directly after all input planes: no gap, no overlap
		QCOMPARE(out.bufferPtr(0) - in.bufferPtr(0), planeOffset(channelsIn, frames));
		QVERIFY(in.bufferPtr(channelsIn - 1) + frames <= out.bufferPtr(0));

		// A repeat call with the same arguments reuses the block and hands back
		// its base pointer, which is where the input planes start
		auto* base = plugin.updateAudioBuffer(channelsIn, channelsOut, frames);
		QCOMPARE(base, in.bufferPtr(0));
		QCOMPARE(base + planeOffset(channelsIn, frames), out.bufferPtr(0));

		// The last output plane ends exactly where the block ends
		QCOMPARE(out.bufferPtr(channelsOut - 1) + frames,
			base + planeOffset(channelsIn + channelsOut, frames));

		// A write at a known offset lands in the expected channel and frame
		in.bufferPtr(2)[5] = 42.0f;
		out.bufferPtr(1)[7] = -7.0f;
		QCOMPARE(in.sample(2, 5), 42.0f);
		QCOMPARE(out.sample(1, 7), -7.0f);

		// ... and the views alias the shared block, not a copy of it
		QCOMPARE(base[2 * frames + 5], 42.0f);
		QCOMPARE(base[planeOffset(channelsIn + 1, frames) + 7], -7.0f);

		// The client side computes its output pointer the same way that the host
		// laid the block out (RemotePluginClient::doProcessing()):
		//     out = shm + inputCount * bufferSize
		const auto clientOutputOffset = planeOffset(channelsIn, frames);
		QCOMPARE(out.bufferPtr(0) - base, clientOutputOffset);
	}

	//! A channel-count change reallocates the block at the new size and the
	//! ports' views follow it; a stale view would read freed shared memory.
	void updateAudioBufferFollowsChannelCountChange()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		ports.audioPortsModel().setChannelCounts(4, 2);

		auto in = ports.buffers()->input();
		auto out = ports.buffers()->output();
		QCOMPARE(in.channels(), ch_cnt_t{4});
		QCOMPARE(out.channels(), ch_cnt_t{2});
		QCOMPARE(out.bufferPtr(0) - in.bufferPtr(0), planeOffset(4, frames));

		ports.audioPortsModel().setChannelCounts(1, 1);

		auto newIn = ports.buffers()->input();
		auto newOut = ports.buffers()->output();
		QCOMPARE(ports.audioPortsModel().in().channelCount(), ch_cnt_t{1});
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{1});
		QCOMPARE(newIn.channels(), ch_cnt_t{1});
		QCOMPARE(newOut.channels(), ch_cnt_t{1});

		// 1 input plane + 1 output plane, still channel-major planar
		QCOMPARE(newOut.bufferPtr(0) - newIn.bufferPtr(0), planeOffset(1, frames));

		// ... and the new block is really writable
		newOut.bufferPtr(0)[frames - 1] = 1.5f;
		QCOMPARE(newOut.sample(0, frames - 1), 1.5f);
	}

	//! A plugin that never initialized has no remote client, so process() must
	//! report failure and render silence: it zeroes the shared output planes,
	//! which is what the pre-migration path did to the caller's output buffer.
	void processZeroesOutputsWhenNotRunning()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		constexpr auto channelsOut = ch_cnt_t{2};

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		ports.audioPortsModel().setChannelCounts(channelsOut, channelsOut);

		auto out = ports.buffers()->output();
		QCOMPARE(out.channels(), channelsOut);
		std::fill(out.bufferPtr(0), out.bufferPtr(0) + frames, 1.0f);
		std::fill(out.bufferPtr(1), out.bufferPtr(1) + frames, 1.0f);

		QCOMPARE(plugin.process(), false);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(out.sample(0, frame), 0.0f);
			QCOMPARE(out.sample(1, frame), 0.0f);
		}
	}

	//! The message a real client sends resolves to a resized shared block: this
	//! is the path RemotePluginClient::setInputOutputCount() drives over the
	//! socket, and the one that used to resize the legacy interleaved buffer.
	void clientChannelCountMessageResizesSharedBuffer()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		QCOMPARE(ports.buffers()->initialized(), false);

		plugin.processMessage(RemotePlugin::message(IdChangeInputOutputCount).addInt(3).addInt(2));

		// The ports model got the counts and the block was allocated for them
		QCOMPARE(ports.audioPortsModel().in().channelCount(), ch_cnt_t{3});
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});
		QCOMPARE(ports.buffers()->initialized(), true);
		QCOMPARE(ports.buffers()->frames(), frames);

		auto in = ports.buffers()->input();
		auto out = ports.buffers()->output();
		QCOMPARE(in.channels(), ch_cnt_t{3});
		QCOMPARE(out.channels(), ch_cnt_t{2});
		QCOMPARE(out.bufferPtr(0) - in.bufferPtr(0), planeOffset(3, frames));
	}

	//! Version skew: the two retired single-count ids (#589) must be refused, not
	//! honoured. A client old enough to send them speaks the interleaved layout,
	//! so acting on the request would leave the two sides disagreeing about the
	//! shared block - the buffer must stay exactly as it was.
	//! NOTE: the plugin cannot be put into the "not failed" state without a real
	//! client process, so what this pins is the refusal (no resize, no state
	//! change) and the silence that process() then renders; the qCritical() the
	//! host logs on this path is the diagnostic.
	void removedCountMessagesAreRefused()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		constexpr auto channelsIn = ch_cnt_t{2};
		constexpr auto channelsOut = ch_cnt_t{2};

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		ports.audioPortsModel().setChannelCounts(channelsIn, channelsOut);

		auto inBefore = ports.buffers()->input();
		auto outBefore = ports.buffers()->output();
		QCOMPARE(ports.buffers()->frames(), frames);
		std::fill(outBefore.bufferPtr(0), outBefore.bufferPtr(0) + frames, 1.0f);

		plugin.processMessage(RemotePlugin::message(IdChangeInputCount).addInt(7));

		// Refused: neither the counts nor the allocation moved
		QCOMPARE(ports.audioPortsModel().in().channelCount(), channelsIn);
		QCOMPARE(ports.audioPortsModel().out().channelCount(), channelsOut);
		QCOMPARE(ports.buffers()->frames(), frames);
		QCOMPARE(ports.buffers()->input().bufferPtr(0), inBefore.bufferPtr(0));
		QCOMPARE(ports.buffers()->output().bufferPtr(0), outBefore.bufferPtr(0));

		// And the plugin renders silence instead of reading the block with the
		// wrong layout
		QCOMPARE(plugin.process(), false);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(outBefore.sample(0, frame), 0.0f);
		}
	}
};

QTEST_GUILESS_MAIN(RemotePluginAudioPortsTest)

#include "RemotePluginAudioPortsTest.moc"
