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
#include <map>
#include <memory>
#include <new>
#include <vector>

#include "AudioBus.h"
#include "AudioBusHandle.h"
#include "AudioDevice.h"
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

//! Audio device that never runs and counts the bus ports the engine registers and unregisters.
class CountingDevice : public AudioDevice
{
public:
	explicit CountingDevice(AudioEngine* engine) : AudioDevice(8, engine) {}
	void registerPort(AudioBusHandle* port) override { ++registrations[port]; }
	void unregisterPort(AudioBusHandle* port) override { ++unregistrations[port]; }

	std::map<AudioBusHandle*, int> registrations;
	std::map<AudioBusHandle*, int> unregistrations;

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}
};

//! Bus that counts its own destructions.
class CountedBus : public AudioBusHandle
{
public:
	CountedBus(const QString& name, bool hasEffectChain) : AudioBusHandle(name, hasEffectChain) {}
	~CountedBus() override { ++destructions; }

	inline static int destructions = 0;
};

//! True while the engine lists the bus. Compares pointer values only, so a freed bus may be passed.
bool isListed(const AudioBusHandle* bus)
{
	const auto& handles = Engine::audioEngine()->audioBusHandles();
	return std::find(handles.begin(), handles.end(), bus) != handles.end();
}

//! One control-thread period: service, render, service.
void flushOnControlThread()
{
	auto* engine = Engine::audioEngine();
	engine->serviceAudioBusHandlesOnControlThread();
	engine->renderNextPeriod();
	engine->serviceAudioBusHandlesOnControlThread();
}

//! Control-thread cleanup: the engine frees the bus, or holds it while a pending entry still names it.
void releaseOnControlThread(AudioBusHandle* bus)
{
	Engine::audioEngine()->destroyAudioBusHandle(bus);
}

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
	//! allocation on the audio path. The control-thread service registers the bus instead.
	void realtimeBusConstructorIsActivatedByControlService()
	{
		using namespace lmms;
		using State = AudioBusHandle::RegistrationState;

		auto* engine = Engine::audioEngine();
		// The service-prepared buffer is sized from BufferManager, so set it to the engine period.
		BufferManager::init(engine->framesPerPeriod());
		// Owns the bus from its construction onwards: every return path, including a failed
		// QVERIFY, hands it to the engine, so no pending entry is left behind.
		struct BusDeleter
		{
			void operator()(AudioBusHandle* busHandle) const
			{
				Engine::audioEngine()->destroyAudioBusHandle(busHandle);
			}
		};
		using BusPtr = std::unique_ptr<AudioBusHandle, BusDeleter>;

		BusPtr bus;
		bool threw = false;
		{
			// The realtime scope marks this thread as the audio path: the constructor
			// only marks the bus Pending and publishes it.
			const RealtimeScope realtime;
			try
			{
				bus.reset(new AudioBusHandle(QStringLiteral("RealtimeBus"), true));
			}
			catch (const std::bad_alloc&)
			{
				threw = true;
			}
		}
		if (threw)
		{
			QFAIL("the realtime constructor allocated an EffectChain before the control service");
		}
		AudioBusHandle* raw = bus.get();

		const auto& handles = engine->audioBusHandles();
		const auto listed = [&] { return std::find(handles.begin(), handles.end(), raw) != handles.end(); };
		QCOMPARE(bus->registrationState(), State::Pending);
		QVERIFY(bus->buffer().empty());
		QVERIFY(!listed());
		QVERIFY(bus->effects() == nullptr);

		// The render owner neither registers nor activates a pending bus.
		engine->renderNextPeriod();
		QCOMPARE(bus->registrationState(), State::Pending);
		QVERIFY(!listed());

		// The control-thread service is the only activation point.
		engine->serviceAudioBusHandlesOnControlThread();
		QCOMPARE(bus->registrationState(), State::Registered);
		QVERIFY(listed());
		QCOMPARE(static_cast<f_cnt_t>(bus->buffer().size()), engine->framesPerPeriod());
		QVERIFY(bus->effects() != nullptr);

		bus.reset();
		QVERIFY(!listed());
	}

	//! Cluster new-p12-313: the engine's registration path (addAudioBusHandle) must not
	//! lock the model nor grow the bus list when it runs on the audio path. The bus is
	//! registered by the control-thread service instead, not by the render owner.
	void engineBusRegistrationOnTheRealtimePathIsDeferred()
	{
		using namespace lmms;
		using State = AudioBusHandle::RegistrationState;

		auto* engine = Engine::audioEngine();
		BufferManager::init(engine->framesPerPeriod());
		const auto& handles = engine->audioBusHandles();
		// Owns the bus from its construction onwards: every return path hands it to the engine.
		struct BusDeleter
		{
			void operator()(AudioBusHandle* busHandle) const
			{
				Engine::audioEngine()->destroyAudioBusHandle(busHandle);
			}
		};
		using BusPtr = std::unique_ptr<AudioBusHandle, BusDeleter>;

		// Built on the control thread and taken out again, so the audio-path registration below
		// is the only one that can put it back.
		BusPtr bus{new AudioBusHandle(QStringLiteral("DeferredBus"), true)};
		AudioBusHandle* raw = bus.get();
		const auto listed = [&] { return std::find(handles.begin(), handles.end(), raw) != handles.end(); };
		engine->removeAudioBusHandle(raw);
		QVERIFY(!listed());

		const std::size_t before = handles.size();
		{
			// The audio path: the engine publishes the bus to its pending queue instead of registering it inline.
			const RealtimeScope realtime;
			engine->addAudioBusHandle(raw);
		}
		QCOMPARE(bus->registrationState(), State::Pending);
		QVERIFY(!listed());

		// The registration was deferred: the list did not grow on the audio path, and the
		// render owner does not register the pending bus.
		engine->renderNextPeriod();
		QCOMPARE(handles.size(), before);
		QVERIFY(!listed());

		// The control-thread service registers it, exactly once.
		engine->serviceAudioBusHandlesOnControlThread();
		QCOMPARE(handles.size(), before + 1);
		QVERIFY(listed());
		QCOMPARE(bus->registrationState(), State::Registered);
	}

	//! Cluster new-p15-253: a bus whose owner retires it while its registration is
	//! still pending must not be registered by the control-thread service afterwards. The
	//! control-thread service consumes the pending entry later - by then the bus is gone, and
	//! the engine may already be about to free it, so registering it would put a freed
	//! bus into the list the render stage walks.
	void busRetiredWhileItsRegistrationIsPendingIsNotRegistered()
	{
		using namespace lmms;

		auto* engine = Engine::audioEngine();
		struct BusDeleter
		{
			void operator()(AudioBusHandle* busHandle) const
			{
				Engine::audioEngine()->destroyAudioBusHandle(busHandle);
			}
		};
		using BusPtr = std::unique_ptr<AudioBusHandle, BusDeleter>;

		auto* bus = new AudioBusHandle(QStringLiteral("RetiredPendingBus"), false);
		BusPtr owner{bus};
		// The constructor registered it on the control thread; take it out again so the
		// registration below is the only one that can put it back.
		engine->removeAudioBusHandle(bus);
		const std::size_t before = engine->audioBusHandles().size();

		{
			// The audio path: the bus is published to the engine's pending queue.
			const RealtimeScope realtime;
			engine->addAudioBusHandle(bus);
		}
		// The owner retires the bus before the control-thread service consumed the entry.
		engine->removeAudioBusHandle(bus);

		// The control thread consumes the entry: a retired bus is not registered.
		engine->serviceAudioBusHandlesOnControlThread();
		QCOMPARE(engine->audioBusHandles().size(), before);
		QVERIFY(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), bus)
			== engine->audioBusHandles().end());

	}

	//! Cluster new-p15-253: a bus that is destroyed while its registration is still
	//! pending must not be registered by the control-thread service afterwards either. The engine
	//! owns the bus from that moment on: it frees it once the render path may no longer
	//! reach it, and until the control-thread service has consumed the pending entry it keeps the bus
	//! alive - the registration link still names it.
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

		// The control thread consumes the pending entry. The bus was retired, so it is
		// not registered - and the engine kept it alive until that entry was consumed,
		// so the render path never walks a freed bus.
		engine->serviceAudioBusHandlesOnControlThread();
		QCOMPARE(engine->audioBusHandles().size(), before);
		QVERIFY(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), bus)
			== engine->audioBusHandles().end());
	}

	//! Cluster new-p15-253: the registration states of a bus follow the control-thread
	//! handoff in order. A bus constructed on the audio path is Pending until the control
	//! service registers it: Pending→Registered. A bus retired while Pending is Retired,
	//! and the control service then consumes its entry: Pending→Retired→Handled. The proof
	//! manifest copies the two sequences listed here.
	void realtimeBusRegistrationStatesFollowTheControlThreadHandoff()
	{
		using namespace lmms;
		using State = AudioBusHandle::RegistrationState;

		// Each bus is owned by a unique_ptr from its construction onwards. The deleter hands
		// the bus to the engine on the control thread, so an early return from a failed
		// assertion still leaves the bus to the engine.
		struct BusDeleter
		{
			void operator()(AudioBusHandle* busHandle) const
			{
				Engine::audioEngine()->destroyAudioBusHandle(busHandle);
			}
		};
		using BusPtr = std::unique_ptr<AudioBusHandle, BusDeleter>;

		auto* engine = Engine::audioEngine();

		// The state sequence of each bus, recorded as the handoff proceeds.
		std::vector<State> registeredPath;
		std::vector<State> retiredPath;

		// Pending→Registered
		BusPtr bus;
		{
			const RealtimeScope realtime;
			bus.reset(new AudioBusHandle(QStringLiteral("StatesBus"), false));
		}
		QCOMPARE(bus->registrationState(), State::Pending);
		registeredPath.push_back(bus->registrationState());

		engine->serviceAudioBusHandlesOnControlThread();
		QCOMPARE(bus->registrationState(), State::Registered);
		registeredPath.push_back(bus->registrationState());
		QVERIFY(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), bus.get())
			!= engine->audioBusHandles().end());

		// Pending→Retired→Handled
		BusPtr bus2;
		{
			const RealtimeScope realtime;
			bus2.reset(new AudioBusHandle(QStringLiteral("StatesRetiredBus"), false));
		}
		QCOMPARE(bus2->registrationState(), State::Pending);
		retiredPath.push_back(bus2->registrationState());

		engine->removeAudioBusHandle(bus2.get());
		QCOMPARE(bus2->registrationState(), State::Retired);
		retiredPath.push_back(bus2->registrationState());

		engine->serviceAudioBusHandlesOnControlThread();
		QCOMPARE(bus2->registrationState(), State::Handled);
		retiredPath.push_back(bus2->registrationState());
		QVERIFY(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), bus2.get())
			== engine->audioBusHandles().end());

		QVERIFY(registeredPath == (std::vector<State>{State::Pending, State::Registered}));
		QVERIFY(retiredPath == (std::vector<State>{State::Pending, State::Retired, State::Handled}));

		bus2.reset();
		bus.reset();
		engine->serviceAudioBusHandlesOnControlThread();
		engine->serviceAudioBusHandlesOnControlThread();
		engine->serviceAudioBusHandlesOnControlThread();
	}

	//! Cluster new-p15-253: a burst of realtime registrations larger than the pending queue is registered in full.
	void realtimeRegistrationsBeyondQueueCapacityAreAllRegistered()
	{
		using namespace lmms;

		std::vector<AudioBusHandle*> buses;
		buses.reserve(200);
		{
			const RealtimeScope realtime;
			for (int i = 0; i < 200; ++i) { buses.push_back(new AudioBusHandle(QStringLiteral("BurstBus"), false)); }
		}
		flushOnControlThread();

		const int listed = static_cast<int>(std::count_if(buses.begin(), buses.end(), isListed));
		for (auto* bus : buses) { releaseOnControlThread(bus); }
		flushOnControlThread();
		QVERIFY2(listed == 200, qPrintable(QStringLiteral("listed %1 of 200").arg(listed)));
	}

	//! Cluster new-p15-253: destructions requested on the audio path beyond the queue's capacity are all freed.
	void realtimeDestructionsBeyondQueueCapacityAreAllFreed()
	{
		using namespace lmms;

		CountedBus::destructions = 0;
		std::vector<AudioBusHandle*> buses;
		buses.reserve(200);
		for (int i = 0; i < 200; ++i) { buses.push_back(new CountedBus(QStringLiteral("DoomedBus"), false)); }
		{
			const RealtimeScope realtime;
			for (auto* bus : buses) { Engine::audioEngine()->destroyAudioBusHandle(bus); }
		}
		flushOnControlThread();

		QCOMPARE(CountedBus::destructions, 200);
		QVERIFY2(std::none_of(buses.begin(), buses.end(), isListed), "a destroyed bus is still listed");
	}

	//! Cluster new-p15-253: removals requested on the audio path beyond the queue's capacity are all applied.
	void realtimeRemovalsBeyondQueueCapacityAreAllApplied()
	{
		using namespace lmms;

		std::vector<AudioBusHandle*> buses;
		buses.reserve(200);
		for (int i = 0; i < 200; ++i) { buses.push_back(new AudioBusHandle(QStringLiteral("RemovedBus"), false)); }
		{
			const RealtimeScope realtime;
			for (auto* bus : buses) { Engine::audioEngine()->removeAudioBusHandle(bus); }
		}
		Engine::audioEngine()->serviceAudioBusHandlesOnControlThread();

		const bool noneListed = std::none_of(buses.begin(), buses.end(), isListed);
		for (auto* bus : buses) { releaseOnControlThread(bus); }
		flushOnControlThread();
		QVERIFY2(noneListed, "a removal requested on the audio path was not applied");
	}

	//! Cluster new-p15-253: port enables requested on the audio path beyond the queue's capacity all reach the device.
	void realtimePortEnablesBeyondQueueCapacityAreAllApplied()
	{
		using namespace lmms;

		auto* engine = Engine::audioEngine();
		auto* device = new CountingDevice(engine);
		engine->setAudioDevice(device, false);
		std::vector<AudioBusHandle*> buses;
		buses.reserve(200);
		for (int i = 0; i < 200; ++i) { buses.push_back(new AudioBusHandle(QStringLiteral("PortBus"), false)); }
		for (auto* bus : buses) { bus->setExtOutputEnabled(false); }
		device->registrations.clear();
		device->unregistrations.clear();
		{
			const RealtimeScope realtime;
			for (auto* bus : buses) { bus->setExtOutputEnabled(true); }
		}
		engine->serviceAudioBusHandlesOnControlThread();

		const int exactlyOne = static_cast<int>(std::count_if(buses.begin(), buses.end(),
			[&](AudioBusHandle* bus) { return device->registrations[bus] == 1; }));
		for (auto* bus : buses) { releaseOnControlThread(bus); }
		flushOnControlThread();
		engine->restoreAudioDevice();
		QCOMPARE(exactlyOne, 200);
	}

	//! Cluster new-p15-253: a bus built on the audio path gets exactly one device port.
	void realtimeBusGetsExactlyOneDevicePort()
	{
		using namespace lmms;

		auto* engine = Engine::audioEngine();
		auto* device = new CountingDevice(engine);
		engine->setAudioDevice(device, false);
		AudioBusHandle* bus = nullptr;
		{
			const RealtimeScope realtime;
			bus = new AudioBusHandle(QStringLiteral("PortOnceBus"), false);
		}
		flushOnControlThread();
		const int registrations = device->registrations[bus];
		releaseOnControlThread(bus);
		flushOnControlThread();
		engine->restoreAudioDevice();
		QCOMPARE(registrations, 1);
	}
};

QTEST_GUILESS_MAIN(AudioBusHandleTest)
#include "AudioBusHandleTest.moc"
