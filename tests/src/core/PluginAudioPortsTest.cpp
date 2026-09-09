/*
 * PluginAudioPortsTest.cpp
 *
 * Copyright (c) 2026 LMMS contributors
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

#include "AudioBus.h"
#include "AudioPorts.h"
#include "AudioPortsSettings.h"
#include "PluginAudioPorts.h"

#include <QtTest>

#include <array>
#include <cstdint>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

//! Stereo 2x2 processor with interleaved, in-place buffers. This is the only
//! combination for which `sampleFrameCompatible()` is true.
constexpr auto InterleavedStereo = AudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = true,
	.inputs = 2,
	.outputs = 2,
	.inplace = true,
	.buffered = true,
};

static_assert(Validate<InterleavedStereo>{}(), "test settings must be valid");
static_assert(InterleavedStereo.sampleFrameCompatible());

//! Mono in-place processor, used to reach the 1-channel `channelName` cases.
constexpr auto MonoInplace = AudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = 1,
	.outputs = 1,
	.inplace = true,
	.buffered = true,
};

//! 4x4 in-place processor, used to reach the generic `channelName` cases.
constexpr auto QuadInplace = AudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = 4,
	.outputs = 4,
	.inplace = true,
	.buffered = true,
};

//! Dynamic-channel-count planar processor, used to exercise the buffer update
//! notification path (`bufferPropertiesChanging`).
constexpr auto DynamicPlanar = AudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = DynamicChannelCount,
	.outputs = DynamicChannelCount,
	.inplace = false,
	.buffered = true,
};

static_assert(Validate<MonoInplace>{}(), "test settings must be valid");
static_assert(Validate<QuadInplace>{}(), "test settings must be valid");
static_assert(Validate<DynamicPlanar>{}(), "test settings must be valid");

using InterleavedStereoPorts = PluginAudioPorts<InterleavedStereo>;

//! Single stereo track-channel pair, interleaved, backed by owned storage.
class TestBus
{
public:
	explicit TestBus(f_cnt_t frames)
		: m_storage(static_cast<std::size_t>(frames))
		, m_pointers{m_storage.data()}
		, m_bus{m_pointers.data(), 1, frames}
	{
	}

	auto audioBus() -> AudioBus& { return m_bus; }

	void setAll(float left, float right)
	{
		for (auto& frame : m_storage)
		{
			frame = SampleFrame{left, right};
		}
	}

	auto left(f_cnt_t frame) const -> float { return m_storage[frame].left(); }
	auto right(f_cnt_t frame) const -> float { return m_storage[frame].right(); }

private:
	std::vector<SampleFrame> m_storage;
	std::array<SampleFrame*, 1> m_pointers{};
	AudioBus m_bus;
};

//! Pins of the interleaved input matrix, addressed the way `send()` reads them.
void setInputPins(InterleavedStereoPorts& ports, std::uint8_t pins)
{
	ports.in().setPin(0, 0, (pins & 0b1000) != 0); // track 0 -> processor 0
	ports.in().setPin(1, 0, (pins & 0b0100) != 0); // track 1 -> processor 0
	ports.in().setPin(0, 1, (pins & 0b0010) != 0); // track 0 -> processor 1
	ports.in().setPin(1, 1, (pins & 0b0001) != 0); // track 1 -> processor 1
}

//! Pins of the interleaved output matrix, addressed the way `receive()` reads them.
void setOutputPins(InterleavedStereoPorts& ports, std::uint8_t pins)
{
	ports.out().setPin(0, 0, (pins & 0b1000) != 0); // processor 0 -> track 0
	ports.out().setPin(0, 1, (pins & 0b0100) != 0); // processor 1 -> track 0
	ports.out().setPin(1, 0, (pins & 0b0010) != 0); // processor 0 -> track 1
	ports.out().setPin(1, 1, (pins & 0b0001) != 0); // processor 1 -> track 1
}

//! An `AudioPorts` implementation without buffers, to reach the "no buffers" path.
template<AudioPortsSettings settings>
class NullBufferPorts : public AudioPorts<settings>
{
public:
	using AudioPorts<settings>::AudioPorts;

	auto buffers() -> typename AudioPorts<settings>::Buffer* override { return nullptr; }

	auto channelName(ch_cnt_t, bool) const -> QString override { return QStringLiteral("null"); }

private:
	void bufferPropertiesChanging(ch_cnt_t, ch_cnt_t, f_cnt_t) override {}
};

} // namespace

class PluginAudioPortsTest : public QObject
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

	void interleavedBufferLifecycle()
	{
		InterleavedStereoPorts ports{false};

		// Buffers exist but are not initialized until init() runs
		QVERIFY(ports.buffers() != nullptr);
		QCOMPARE(ports.buffers()->initialized(), false);
		QCOMPARE(ports.active(), false);

		// Const accessors (constBuffers() and model() const)
		const InterleavedStereoPorts& constPorts = ports;
		QCOMPARE(constPorts.constBuffers()->initialized(), false);
		QCOMPARE(constPorts.active(), false);
		QCOMPARE(constPorts.model().in().channelCount(), ch_cnt_t{2});
		QCOMPARE(constPorts.model().out().channelCount(), ch_cnt_t{2});

		ports.init();

		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);
		QCOMPARE(ports.buffers()->initialized(), true);
		QCOMPARE(ports.buffers()->frames(), frames);
		QCOMPARE(ports.active(), true);

		auto inOut = ports.buffers()->inputOutput();
		QCOMPARE(inOut.channels(), ch_cnt_t{2});
		QCOMPARE(inOut.frames(), frames);

		// The interleaved view really points at the buffer
		inOut.data()[0] = 0.5f;
		QCOMPARE(inOut.data()[0], 0.5f);
	}

	void channelNamesFollowChannelCount()
	{
		// 2 channels: per-side names
		InterleavedStereoPorts stereo{false};
		QCOMPARE(stereo.channelName(0, true), QStringLiteral("Plugin Out L"));
		QCOMPARE(stereo.channelName(1, true), QStringLiteral("Plugin Out R"));
		QCOMPARE(stereo.channelName(0, false), QStringLiteral("Plugin In L"));
		QCOMPARE(stereo.channelName(1, false), QStringLiteral("Plugin In R"));

		// 1 channel: singular names
		PluginAudioPorts<MonoInplace> mono{false};
		QCOMPARE(mono.channelName(0, true), QStringLiteral("Plugin Out"));
		QCOMPARE(mono.channelName(0, false), QStringLiteral("Plugin In"));

		// More than 2 channels: numbered names
		PluginAudioPorts<QuadInplace> quad{false};
		QCOMPARE(quad.channelName(0, true), QStringLiteral("Plugin Out 1"));
		QCOMPARE(quad.channelName(2, true), QStringLiteral("Plugin Out 3"));
		QCOMPARE(quad.channelName(0, false), QStringLiteral("Plugin In 1"));
		QCOMPARE(quad.channelName(3, false), QStringLiteral("Plugin In 4"));
	}

	void sendRoutesAllPinCombinations()
	{
		InterleavedStereoPorts ports{false};
		ports.init();
		QCOMPARE(ports.active(), true);

		const auto frames = Engine::audioEngine()->framesPerPeriod();
		constexpr auto busL = 1.0f;
		constexpr auto busR = 2.0f;

		// No output pins: the receive step must not modify the bus
		setOutputPins(ports, 0);

		for (std::uint8_t pins = 0; pins < 16; ++pins)
		{
			setInputPins(ports, pins);

			TestBus bus{frames};
			bus.setAll(busL, busR);

			std::array<float, 2> processorInput{-1.0f, -1.0f};
			auto router = ports.getRouter();
			const auto status = router.process(bus.audioBus(), [&](auto inOut) {
				processorInput[0] = inOut.data()[0];
				processorInput[1] = inOut.data()[1];
				return ProcessStatus::Continue;
			});

			QCOMPARE(status, ProcessStatus::Continue);

			// Processor 0 sums the track channels whose pins are in the high
			// nibble, processor 1 those in the low nibble
			const auto expectedProcessor0 = (pins & 0b1000 ? busL : 0.0f) + (pins & 0b0100 ? busR : 0.0f);
			const auto expectedProcessor1 = (pins & 0b0010 ? busL : 0.0f) + (pins & 0b0001 ? busR : 0.0f);
			QCOMPARE(processorInput[0], expectedProcessor0);
			QCOMPARE(processorInput[1], expectedProcessor1);

			// The bus is untouched because no output pins are set
			QCOMPARE(bus.left(0), busL);
			QCOMPARE(bus.right(0), busR);
		}
	}

	void receiveRoutesAllPinCombinations()
	{
		InterleavedStereoPorts ports{false};
		ports.init();
		QCOMPARE(ports.active(), true);

		const auto frames = Engine::audioEngine()->framesPerPeriod();
		constexpr auto busL = 1.0f;
		constexpr auto busR = 2.0f;
		constexpr auto processorOutL = 10.0f;
		constexpr auto processorOutR = 20.0f;

		// No input pins: the processor inputs are all zero, so the buffer that
		// the process function writes is fully under test control
		setInputPins(ports, 0);

		for (std::uint8_t pins = 0; pins < 16; ++pins)
		{
			setOutputPins(ports, pins);

			TestBus bus{frames};
			bus.setAll(busL, busR);

			std::array<float, 2> processorInput{-1.0f, -1.0f};
			auto router = ports.getRouter();
			const auto status = router.process(bus.audioBus(), [&](auto inOut) {
				processorInput[0] = inOut.data()[0];
				processorInput[1] = inOut.data()[1];
				for (f_cnt_t frame = 0; frame < inOut.frames(); ++frame)
				{
					inOut.data()[frame * 2] = processorOutL;
					inOut.data()[frame * 2 + 1] = processorOutR;
				}
				return ProcessStatus::Continue;
			});

			QCOMPARE(status, ProcessStatus::Continue);

			// No input pins: the processor inputs are all zero
			QCOMPARE(processorInput[0], 0.0f);
			QCOMPARE(processorInput[1], 0.0f);

			// Track channel 0 is fed by the pins in bits 3 and 2, track channel
			// 1 by the pins in bits 1 and 0; an unconnected track channel keeps
			// the value it had before processing
			const auto leftPins = static_cast<std::uint8_t>(pins >> 2);
			const auto rightPins = static_cast<std::uint8_t>(pins & 0b0011);

			const auto expectedLeft = leftPins == 0b11 ? processorOutL + processorOutR
				: leftPins == 0b01                   ? processorOutR
				: leftPins == 0b10                   ? processorOutL
													 : busL;
			const auto expectedRight = rightPins == 0b11 ? processorOutL + processorOutR
				: rightPins == 0b01                     ? processorOutR
				: rightPins == 0b10                     ? processorOutL
														: busR;

			QCOMPARE(bus.left(0), expectedLeft);
			QCOMPARE(bus.right(0), expectedRight);
		}
	}

	void bufferPropertiesChangingUpdatesBuffers()
	{
		PluginAudioPorts<DynamicPlanar> ports{false};
		// Dynamic channel counts start out unset (0) rather than at a fixed value
		QCOMPARE(ports.in().channelCount(), ch_cnt_t{0});
		QCOMPARE(ports.out().channelCount(), ch_cnt_t{0});

		// With dynamic channel counts, init() cannot size the buffers
		ports.init();
		QCOMPARE(ports.active(), false);

		ports.setChannelCounts(2, 2);

		// setChannelCounts() notifies the buffers through bufferPropertiesChanging()
		QCOMPARE(ports.in().channelCount(), ch_cnt_t{2});
		QCOMPARE(ports.out().channelCount(), ch_cnt_t{2});
		QCOMPARE(ports.active(), true);
		QCOMPARE(ports.buffers()->initialized(), true);
		QCOMPARE(ports.buffers()->frames(), Engine::audioEngine()->framesPerPeriod());

		auto input = ports.buffers()->input();
		auto output = ports.buffers()->output();
		QCOMPARE(input.channels(), ch_cnt_t{2});
		QCOMPARE(output.channels(), ch_cnt_t{2});
		QCOMPARE(input.frames(), Engine::audioEngine()->framesPerPeriod());
	}

	void portsWithoutBuffersAreInactive()
	{
		NullBufferPorts<InterleavedStereo> ports{false};
		QCOMPARE(ports.in().channelCount(), ch_cnt_t{2});
		QCOMPARE(ports.buffers(), nullptr);
		QCOMPARE(ports.active(), false);

		const NullBufferPorts<InterleavedStereo>& constPorts = ports;
		QCOMPARE(constPorts.constBuffers(), nullptr);
		QCOMPARE(constPorts.active(), false);
	}
};

QTEST_GUILESS_MAIN(PluginAudioPortsTest)
#include "PluginAudioPortsTest.moc"
