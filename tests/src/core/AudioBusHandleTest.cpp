/*
 * AudioBusHandleTest.cpp - tests for the AudioBus-based audio path
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

#include <QtTest>

#include <limits>

#include "AudioBus.h"
#include "AudioBusHandle.h"
#include "BufferManager.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "PlayHandle.h"
#include "Plugin.h"
#include "SampleFrame.h"

namespace lmms
{

namespace
{

//! Play handle that fills its buffer with a deterministic ramp so that the
//! AudioBusHandle -> MixerChannel path can be verified sample-exactly.
class TestPlayHandle : public PlayHandle
{
public:
	TestPlayHandle() :
		PlayHandle{PlayHandle::Type::InstrumentPlayHandle, 0}
	{
	}

	void play(std::span<SampleFrame> buffer) override
	{
		for (f_cnt_t f = 0; f < buffer.size(); ++f)
		{
			buffer[f][0] = 0.25f;
			buffer[f][1] = -0.5f;
		}
	}

	bool isFinished() const override { return false; }
	bool isFromTrack(const Track*) const override { return false; }
};

//! Minimal effect that doubles its input. Used to exercise the
//! EffectChain/Effect processAudioBuffer(AudioBus&) entry point.
class TestEffect : public Effect
{
public:
	TestEffect(Model* parent) :
		Effect{&s_descriptor, parent, nullptr}
	{
	}

	EffectControls* controls() override { return nullptr; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			buf[f][0] *= 2.0f;
			buf[f][1] *= 2.0f;
		}
		return ProcessStatus::Continue;
	}

private:
	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor TestEffect::s_descriptor
{
	"audiopluginbustest",
	"AudioBusHandleTest effect",
	"Effect used to exercise Effect::processAudioBuffer(AudioBus&)",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

} // namespace

} // namespace lmms

class AudioBusHandleTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		lmms::Engine::init(true);
		// The dummy device thread renders in the background; this test drives
		// the audio path synchronously, so stop it to keep the buffers stable.
		lmms::Engine::audioEngine()->audioDev()->stopProcessing();
		lmms::Engine::audioEngine()->setSanitizationEnabled(true);
	}

	void cleanupTestCase()
	{
		lmms::Engine::destroy();
	}

	//! Mix a 48-frame stereo block through an AudioBusHandle and verify that
	//! it arrives sample-exactly in the destination MixerChannel.
	void audioBusHandle48FrameBlock()
	{
		using namespace lmms;

		constexpr f_cnt_t fpp = 48;
		// 48-frame period for this test: BufferManager backs both the
		// PlayHandle and the AudioBusHandle storage.
		BufferManager::init(fpp);

		AudioBusHandle handle{QStringLiteral("AudioBusHandleTest"), false};
		QCOMPARE(static_cast<f_cnt_t>(handle.buffer().size()), fpp);

		TestPlayHandle playHandle;
		handle.addPlayHandle(&playHandle);
		playHandle.doProcessing(); // fill the play handle buffer

		// Zero the destination channel so the assertion below is meaningful
		SampleFrame* masterBuffer = Engine::mixer()->mixerChannel(0)->m_buffer;
		const f_cnt_t engineFpp = Engine::audioEngine()->framesPerPeriod();
		for (f_cnt_t f = 0; f < engineFpp; ++f)
		{
			masterBuffer[f][0] = 0.0f;
			masterBuffer[f][1] = 0.0f;
		}

		handle.doProcessing(); // mix -> sanitize -> update -> mixToChannel

		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			QCOMPARE(masterBuffer[f][0], 0.25f);
			QCOMPARE(masterBuffer[f][1], -0.5f);
		}
		// nothing must bleed past the 48-frame block
		QCOMPARE(masterBuffer[fpp][0], 0.0f);
		QCOMPARE(masterBuffer[fpp][1], 0.0f);
		QVERIFY(!handle.isCorrupted());

		handle.removePlayHandle(&playHandle);
	}

	//! Process a 48-frame block through EffectChain::processAudioBuffer(AudioBus&)
	void effectChainProcessAudioBus()
	{
		using namespace lmms;

		constexpr f_cnt_t fpp = 48;
		BufferManager::init(fpp);

		SampleFrame storage[fpp];
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			storage[f][0] = 0.5f;
			storage[f][1] = -0.25f;
		}
		SampleFrame* busData[1] = { storage };
		AudioBus bus{busData, 1, fpp};

		EffectChain chain{nullptr};
		auto* effect = new TestEffect{&chain};
		chain.appendEffect(effect);

		QVERIFY(chain.processAudioBuffer(bus));

		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			QCOMPARE(storage[f][0], 1.0f);
			QCOMPARE(storage[f][1], -0.5f);
		}
		QVERIFY(!effect->isCorrupted());
		// non-silent output must clear the quiet flags of the bus
		QVERIFY(!bus.quietChannels()[0]);
		QVERIFY(!bus.quietChannels()[1]);
	}

	//! Inf/NaN input must be cleared and reported through isCorrupted()
	void effectSanitizesNonFiniteAudioBus()
	{
		using namespace lmms;

		constexpr f_cnt_t fpp = 48;
		BufferManager::init(fpp);

		SampleFrame storage[fpp];
		const float nan = std::numeric_limits<float>::quiet_NaN();
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			storage[f][0] = nan;
			storage[f][1] = nan;
		}
		SampleFrame* busData[1] = { storage };
		AudioBus bus{busData, 1, fpp};

		EffectChain chain{nullptr};
		auto* effect = new TestEffect{&chain};
		chain.appendEffect(effect);

		chain.processAudioBuffer(bus);

		QVERIFY(effect->isCorrupted());
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			QCOMPARE(storage[f][0], 0.0f);
			QCOMPARE(storage[f][1], 0.0f);
		}
	}
};

QTEST_GUILESS_MAIN(AudioBusHandleTest)
#include "AudioBusHandleTest.moc"
