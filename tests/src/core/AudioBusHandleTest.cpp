/*
 * AudioBusHandleTest.cpp - tests for the AudioBus-based audio path
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

#include <QtTest>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

#include "AudioBus.h"
#include "AudioBusHandle.h"
#include "BufferManager.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "PlayHandle.h"
#include "Plugin.h"
#include "RealtimeScope.h"
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

	//! A default-constructed play handle must have no bus until one is assigned.
	void unsetAudioBusHandleIsNull()
	{
		using namespace lmms;

		BufferManager::init(48);
		alignas(TestPlayHandle) unsigned char storage[sizeof(TestPlayHandle)];
		std::memset(storage, 0xA5, sizeof(storage));
		auto* handle = new (storage) TestPlayHandle{};

		QVERIFY(handle->audioBusHandle() == nullptr);

		handle->~TestPlayHandle();
	}

	void audioBusHandleDefaultsToNull()
	{
		using namespace lmms;

		BufferManager::init(48);
		alignas(TestPlayHandle) unsigned char storage[sizeof(TestPlayHandle)];
		std::memset(storage, 0xA5, sizeof(storage));
		auto* handle = ::new (static_cast<void*>(storage)) TestPlayHandle;
		const bool hasNoBusHandle = handle->audioBusHandle() == nullptr;
		handle->~TestPlayHandle();

		QVERIFY(hasNoBusHandle);
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

	//! Cluster new-p15-253: constructing an audio bus on the audio path must not register
	//! it with the engine inline. The registration takes the engine's model lock - a
	//! render worker would deadlock against the render owner, which holds that lock for
	//! the whole period and waits for the worker - and it grows the bus list, which is an
	//! allocation on the audio path. The render owner registers the bus instead.
	void busCreatedOnTheRealtimePathIsRegisteredByTheRenderOwner()
	{
		using namespace lmms;

		AudioBusHandle* bus = nullptr;
		{
			// The realtime scope marks this thread as the audio path: the constructor
			// must behave as it does on the render thread.
			const RealtimeScope realtime;
			bus = new AudioBusHandle(QStringLiteral("RealtimeBus"), false);
		}

		const auto& handles = Engine::audioEngine()->audioBusHandles();
		QVERIFY(std::find(handles.begin(), handles.end(), bus) == handles.end());

		// The render owner registers the published bus at the start of the next period.
		Engine::audioEngine()->renderNextPeriod();
		QVERIFY(std::find(handles.begin(), handles.end(), bus) != handles.end());

		delete bus;
		QVERIFY(std::find(handles.begin(), handles.end(), bus) == handles.end());
	}

	//! Cluster new-p12-313: the engine's registration path (addAudioBusHandle) must not
	//! lock the model nor grow the bus list when it runs on the audio path. The bus is
	//! registered by the render owner at the start of the next period instead.
	void engineBusRegistrationOnTheRealtimePathIsDeferred()
	{
		using namespace lmms;

		auto* engine = Engine::audioEngine();
		auto* bus = new AudioBusHandle(QStringLiteral("DeferredBus"), false);
		// The constructor registered it on the control thread; take it out again so the
		// registration below is the only one that can put it back.
		engine->removeAudioBusHandle(bus);
		const std::size_t before = engine->audioBusHandles().size();
		QVERIFY(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), bus)
			== engine->audioBusHandles().end());

		{
			const RealtimeScope realtime;
			engine->addAudioBusHandle(bus);
		}

		// The registration was deferred: the bus list did not grow on the audio path.
		QCOMPARE(engine->audioBusHandles().size(), before);
		QVERIFY(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), bus)
			== engine->audioBusHandles().end());

		// The render owner registers it at the start of the next period.
		engine->renderNextPeriod();
		QCOMPARE(engine->audioBusHandles().size(), before + 1);
		QVERIFY(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), bus)
			!= engine->audioBusHandles().end());

		delete bus;
	}

	//! Cluster new-p15-253: a bus whose owner retires it while its registration is
	//! still pending must not be registered by the render owner afterwards. The render
	//! owner consumes the pending entry one period later - by then the bus is gone, and
	//! the engine may already be about to free it, so registering it would put a freed
	//! bus into the list the render stage walks.
	void busRetiredWhileItsRegistrationIsPendingIsNotRegistered()
	{
		using namespace lmms;

		auto* engine = Engine::audioEngine();
		auto* bus = new AudioBusHandle(QStringLiteral("RetiredPendingBus"), false);
		// The constructor registered it on the control thread; take it out again so the
		// registration below is the only one that can put it back.
		engine->removeAudioBusHandle(bus);
		const std::size_t before = engine->audioBusHandles().size();

		{
			// The audio path: the bus is published to the engine's pending queue.
			const RealtimeScope realtime;
			engine->addAudioBusHandle(bus);
		}
		// The owner retires the bus before the render owner consumed the entry.
		engine->removeAudioBusHandle(bus);

		// The render owner consumes the entry: a retired bus is not registered.
		engine->renderNextPeriod();
		QCOMPARE(engine->audioBusHandles().size(), before);
		QVERIFY(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), bus)
			== engine->audioBusHandles().end());

		delete bus;
	}

	//! Cluster new-p15-253: a bus that is destroyed while its registration is still
	//! pending must not be registered by the render owner afterwards either. The engine
	//! owns the bus from that moment on: it frees it once the render path may no longer
	//! reach it, and until the render owner consumed the pending entry it keeps the bus
	//! alive - the queue still names it.
	void busDestroyedWhileItsRegistrationIsPendingIsNotRegistered()
	{
		using namespace lmms;

		auto* engine = Engine::audioEngine();
		const std::size_t before = engine->audioBusHandles().size();
		AudioBusHandle* bus = nullptr;
		{
			// The audio path: the bus is published to the engine's pending queue
			// instead of being registered inline.
			const RealtimeScope realtime;
			bus = new AudioBusHandle(QStringLiteral("DestroyedPendingBus"), false);
		}
		// The owner hands the bus to the engine, which frees it once the render path
		// may no longer reach it.
		engine->destroyAudioBusHandle(bus);

		// The render owner consumes the pending entry. The bus was retired, so it is
		// not registered - and the engine kept it alive until that entry was consumed,
		// so the render path never walks a freed bus.
		engine->renderNextPeriod();
		QCOMPARE(engine->audioBusHandles().size(), before);
		QVERIFY(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), bus)
			== engine->audioBusHandles().end());
	}
};

QTEST_GUILESS_MAIN(AudioBusHandleTest)
#include "AudioBusHandleTest.moc"
