/*
 * AudioEngineTeardownTest.cpp - the engine's worker pool must not outlive its
 * QThread objects (test hygiene: a test must not abort because of teardown)
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

//! Regression test for the teardown abort that made the suite unreadable under
//! load. Observed as
//!
//!   QFATAL : <AnyEngineTest>::cleanupTestCase() QThread: Destroyed while thread
//!            is still running
//!   Received signal 6 (SIGABRT)
//!
//! i.e. exit code 134, on whichever test happened to be running when it landed -
//! 13 of 64 PdcMixerTest runs, and three different tests across a suite sweep,
//! with no change to the code under test. docs/TEST-HYGIENE.md carries the
//! reproduction, the gdb backtrace and the load measurements.
//!
//! The defect, in two halves:
//!
//!  1. `AudioEngineWorkerThread::run()` reads `m_quit` at the top of its loop,
//!     outside the mutex, and then blocks in `QWaitCondition::wait()`. Setting the
//!     flag (`quit()`) and waking the condition (`startAndWaitForJobs()`) are two
//!     separate steps, so a worker preempted between the read and the wait misses
//!     the wake that was meant to release it and sleeps for good.
//!  2. `~AudioEngine` joined each worker with a fixed `wait(500)`. When that budget
//!     expired the worker was still running, and because the workers are QObject
//!     children of the engine (~QObject deletes them) the QThread object was
//!     destroyed while its thread ran - which Qt answers with `qFatal`, i.e. an
//!     unconditional SIGABRT of the whole test process.
//!
//! Case `quitWithoutAWakeupStopsAParkedWorker` is the red/green pair: it fails
//! (deterministically) on the tree without half 1, because nobody ever wakes the
//! parked worker. Case `quitThenWakeAndJoinStopsEveryWorker` pins the product's own
//! destroy sequence.

#include <QtTest>

#include <QFile>
#include <QRegularExpression>
#include <QFile>
#include <QSemaphore>
#include <QThread>

#include <atomic>
#include <memory>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "Engine.h"
#include "AudioPulseAudio.h"
#ifdef Q_OS_WIN
#include "LmmsSemaphore.h"
#endif
#include "LocklessList.h"

#include <cstddef>
#include <type_traits>

#include "AudioPulseAudio.h"

using namespace lmms;

namespace
{

//! Long enough that a worker started by this process has certainly reached the
//! wait condition even on a loaded box - its run() does nothing else before it
//! blocks - and short enough to keep the suite fast.
constexpr int kParkSettleMs = 750;

//! Budget for a worker to leave its loop. The product's quit-recheck interval is
//! 100 ms, so this is 20x that: the case is about the contract ("a quit ends the
//! loop"), not about scheduler luck.
constexpr int kExitBudgetMs = 2000;

//! Parked workers per case. More than one so a case does not hinge on a single
//! thread's scheduling.
constexpr int kWorkers = 8;
constexpr int kListCapacity = sizeof(int) * 8;

template<typename List>
bool pushSucceeded(List& list, int value)
{
	// Keep this test compilable against the original void push() for the red run.
	if constexpr (std::is_same_v<decltype(list.push(value)), bool>)
	{
		return list.push(value);
	}
	else
	{
		list.push(value);
		return true;
	}
}

//! Budget for the fake PulseAudio handshake: the device thread answers the
//! case's event (a refused connect, a failed stream, a rendered period) and
//! the case waits for it. It is a FAILURE BOUND, not a timing assertion - the
//! events themselves are released by the fake, never by the clock.
#if defined(LMMS_HAVE_PULSEAUDIO) && defined(Q_OS_LINUX)
constexpr int kPulseHandshakeMs = 5000;
#endif

struct LifetimeProbe
{
	static inline int liveObjects = 0;
	static inline int destructions = 0;
	static inline int assignments = 0;

	static void reset()
	{
		liveObjects = 0;
		destructions = 0;
		assignments = 0;
	}

	explicit LifetimeProbe(int value = 0) : value(value) { ++liveObjects; }
	LifetimeProbe(const LifetimeProbe& other) : value(other.value) { ++liveObjects; }
	LifetimeProbe& operator=(const LifetimeProbe& other)
	{
		value = other.value;
		++assignments;
		return *this;
	}
	~LifetimeProbe()
	{
		--liveObjects;
		++destructions;
	}

	int value;
};

} // namespace

#if defined(LMMS_HAVE_PULSEAUDIO) && defined(Q_OS_LINUX)
//! The fake PulseAudio server the PulseAudio cases drive.
//!
//! ONE set of test-only symbols serves every case. The mainloop/context
//! creation failure (cluster new-p12-319) and the stream failure cases
//! (cluster new-p9-568) used to define the SAME libpulse entry points
//! separately, and two definitions of one symbol cannot be linked into one
//! binary - so the scenarios are merged here instead: each case says which
//! answer the fake gives, and the case order does not matter.

//! Which answer the fake server gives.
enum class PulseScenario
{
	//! pa_context_new() answers null: the device must release the mainloop.
	ContextCreationFails,
	//! pa_stream_connect_playback() answers an error: the device must not
	//! report the connection as established.
	PlaybackConnectRejected,
	//! The connect succeeds and the stream enters PA_STREAM_FAILED after it.
	StreamFailsLater,
};

struct FakePulseState
{
	//! How often the device iterated the mainloop.
	std::atomic<int> mainloopIterations{0};
	//! The device asked for a playback connection.
	std::atomic<bool> playbackConnectionCalled{false};
	//! The fake handed the stream its PA_STREAM_FAILED state.
	std::atomic<bool> streamFailureDelivered{false};
	//! How often the mainloop was released.
	std::atomic<unsigned> mainloopFreeCalls{0};
};

//! The opaque libpulse types, completed so the fake can carry the callbacks
//! the product registers on them.
struct pa_mainloop
{
	int unused = 0;
};

struct pa_context
{
	pa_context_notify_cb_t stateCallback = nullptr;
	void* stateUserdata = nullptr;
	pa_context_state_t state = PA_CONTEXT_UNCONNECTED;
};

struct pa_stream
{
	pa_context* context = nullptr;
	pa_stream_notify_cb_t stateCallback = nullptr;
	void* stateUserdata = nullptr;
	pa_stream_state_t state = PA_STREAM_UNCONNECTED;
};

FakePulseState gFakePulse;
PulseScenario gPulseScenario = PulseScenario::ContextCreationFails;
pa_context gFakeContext;
pa_stream gFakeStream;

//! Selects the scenario and clears the recorded events, so a case never sees
//! the previous case's handshake.
void setPulseScenario(PulseScenario scenario)
{
	gPulseScenario = scenario;
	gFakePulse.mainloopIterations.store(0, std::memory_order_relaxed);
	gFakePulse.playbackConnectionCalled.store(false, std::memory_order_relaxed);
	gFakePulse.streamFailureDelivered.store(false, std::memory_order_relaxed);
	gFakePulse.mainloopFreeCalls.store(0, std::memory_order_relaxed);
}

// These test-only PulseAudio symbols replace the libpulse entry points the
// device uses, so the cases decide what the server answers.
extern "C" pa_mainloop* pa_mainloop_new(void)
{
	static pa_mainloop mainloop;
	return &mainloop;
}

extern "C" pa_mainloop_api* pa_mainloop_get_api(pa_mainloop* mainloop)
{
	return reinterpret_cast<pa_mainloop_api*>(mainloop);
}

extern "C" void pa_mainloop_free(pa_mainloop*)
{
	gFakePulse.mainloopFreeCalls.fetch_add(1, std::memory_order_relaxed);
}

//! One iteration of the fake mainloop. In the StreamFailsLater scenario it
//! hands the stream the PA_STREAM_FAILED state a server hands out when the
//! sink goes away - the event the device has to react to.
extern "C" int pa_mainloop_iterate(pa_mainloop*, int, int*)
{
	gFakePulse.mainloopIterations.fetch_add(1, std::memory_order_relaxed);
	if (gPulseScenario == PulseScenario::StreamFailsLater
		&& !gFakePulse.streamFailureDelivered.load(std::memory_order_relaxed)
		&& gFakeStream.stateCallback != nullptr)
	{
		gFakeStream.state = PA_STREAM_FAILED;
		gFakeStream.stateCallback(&gFakeStream, gFakeStream.stateUserdata);
		gFakePulse.streamFailureDelivered.store(true, std::memory_order_release);
	}
	return 0;
}

extern "C" pa_context* pa_context_new(pa_mainloop_api*, const char*)
{
	if (gPulseScenario == PulseScenario::ContextCreationFails) { return nullptr; }
	gFakeContext = {};
	return &gFakeContext;
}

extern "C" void pa_context_set_state_callback(pa_context* context, pa_context_notify_cb_t callback, void* userdata)
{
	context->stateCallback = callback;
	context->stateUserdata = userdata;
}

//! The fake context reaches PA_CONTEXT_READY as soon as it is asked to
//! connect - the state change the product's callback reacts to.
extern "C" int pa_context_connect(pa_context* context, const char*, pa_context_flags_t, const pa_spawn_api*)
{
	context->state = PA_CONTEXT_READY;
	if (context->stateCallback != nullptr) { context->stateCallback(context, context->stateUserdata); }
	return 0;
}

extern "C" pa_context_state_t pa_context_get_state(const pa_context* context)
{
	return context->state;
}

extern "C" int pa_context_errno(const pa_context*)
{
	return 1;
}

extern "C" const char* pa_strerror(int)
{
	return "fake PulseAudio error";
}

extern "C" void pa_context_disconnect(pa_context*) {}
extern "C" void pa_context_unref(pa_context*) {}

extern "C" pa_stream* pa_stream_new(pa_context* context, const char*, const pa_sample_spec*, const pa_channel_map*)
{
	gFakeStream = {};
	gFakeStream.context = context;
	gFakeStream.state = PA_STREAM_CREATING;
	return &gFakeStream;
}

extern "C" void pa_stream_set_state_callback(pa_stream* stream, pa_stream_notify_cb_t callback, void* userdata)
{
	stream->stateCallback = callback;
	stream->stateUserdata = userdata;
}

extern "C" void pa_stream_set_write_callback(pa_stream*, pa_stream_request_cb_t, void*) {}

//! The connect answer the scenario asks for: an error in
//! PlaybackConnectRejected, success otherwise.
extern "C" int pa_stream_connect_playback(pa_stream*, const char*, const pa_buffer_attr*, pa_stream_flags_t,
	const pa_cvolume*, pa_stream*)
{
	gFakePulse.playbackConnectionCalled.store(true, std::memory_order_release);
	return gPulseScenario == PulseScenario::PlaybackConnectRejected ? -1 : 0;
}

extern "C" pa_stream_state_t pa_stream_get_state(const pa_stream* stream)
{
	return stream->state;
}

extern "C" pa_context* pa_stream_get_context(const pa_stream* stream)
{
	return stream->context;
}

extern "C" int pa_stream_disconnect(pa_stream*) { return 0; }
extern "C" void pa_stream_unref(pa_stream*) {}

//! Stops the device and drops the render tap when the case ends - also when
//! an assertion returns early, so a failing case never leaves a running
//! device behind (the AudioDevice destructor asserts it was stopped).
struct PulseTestCleanup
{
	AudioPulseAudio& device;
	QMetaObject::Connection renderConnection;

	~PulseTestCleanup()
	{
		if (static_cast<AudioDevice&>(device).isRunning()) { device.stopProcessing(); }
		QObject::disconnect(renderConnection);
	}
};
#endif

class AudioEngineTeardownTest : public QObject
{
	Q_OBJECT

private slots:
	void locklessListConstructsAndDestroysFreedValues()
	{
		LifetimeProbe::reset();
		lmms::LocklessList<LifetimeProbe> list(1);
		LifetimeProbe source{42};

		const int liveBeforePush = LifetimeProbe::liveObjects;
		list.push(source);
		const int liveAfterPush = LifetimeProbe::liveObjects;
		const int assignmentsAfterPush = LifetimeProbe::assignments;
		auto* element = list.popList();
		QVERIFY(element != nullptr);

		const int destructionsBeforeFree = LifetimeProbe::destructions;
		const int liveBeforeFree = LifetimeProbe::liveObjects;
		list.free(element);
		const int destructionsAfterFree = LifetimeProbe::destructions;
		const int liveAfterFree = LifetimeProbe::liveObjects;
		QCOMPARE(liveAfterPush, liveBeforePush + 1);
		QCOMPARE(assignmentsAfterPush, 0);
		QCOMPARE(destructionsAfterFree, destructionsBeforeFree + 1);
		QCOMPARE(liveAfterFree, liveBeforeFree - 1);
	}

	void locklessListDestroysValuesStillLinkedAtTeardown()
	{
		LifetimeProbe::reset();
		int destructionsBeforeListTeardown = 0;
		{
			lmms::LocklessList<LifetimeProbe> list(1);
			list.push(LifetimeProbe{73});
			destructionsBeforeListTeardown = LifetimeProbe::destructions;
		}
		QCOMPARE(LifetimeProbe::destructions, destructionsBeforeListTeardown + 1);
	}

	void exhaustedListReportsFailureWithoutLosingEntries()
	{
		lmms::LocklessList<int> list(kListCapacity);
		for (int value = 0; value < kListCapacity; ++value)
		{
			QVERIFY(pushSucceeded(list, value));
		}
		QVERIFY(!pushSucceeded(list, kListCapacity));

		auto* element = list.popList();
		for (int expected = kListCapacity - 1; expected >= 0; --expected)
		{
			QVERIFY(element != nullptr);
			QCOMPARE(element->value, expected);
			auto* next = element->next;
			list.free(element);
			element = next;
		}
		QVERIFY(element == nullptr);

		QVERIFY(pushSucceeded(list, 17));
		auto* reused = list.popList();
		QVERIFY(reused != nullptr);
		QCOMPARE(reused->value, 17);
		list.free(reused);
	}

	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		QVERIFY(Engine::audioEngine()->audioDev() != nullptr);

		// Stop the dummy device before the cases: each case observes workers in
		// their parked state, and a device that renders periods wakes them ~350
		// times a second (which is also how the same defect hides in a test that
		// leaves the device running - see docs/TEST-HYGIENE.md).
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		// The defect's original site. It must not abort here any more.
		Engine::destroy();
	}

	//! The Windows semaphore must keep the handle returned by CreateSemaphore.
	//! On Windows, exercise the public operations against that handle. Other
	//! hosts verify the Windows-specific constructor contract directly.
	void windowsSemaphoreUsesCreatedHandle()
	{
#ifdef Q_OS_WIN
		lmms::Semaphore semaphore(1);
		semaphore.wait();
		QVERIFY(!semaphore.tryWait());
		semaphore.post();
		QVERIFY(semaphore.tryWait());
#else
		QFile implementation(QStringLiteral("../../src/core/LmmsSemaphore.cpp"));
		QVERIFY2(implementation.open(QIODevice::ReadOnly),
			qPrintable(implementation.fileName()));
		const QByteArray source = implementation.readAll();
		const int windowsBranch = source.indexOf("#elif defined(LMMS_BUILD_WIN32)");
		QVERIFY(windowsBranch >= 0);
		const int otherBranch = source.indexOf("#else", windowsBranch);
		QVERIFY(otherBranch > windowsBranch);
		const QByteArray branch = source.mid(windowsBranch, otherBranch - windowsBranch);
		QVERIFY2(QRegularExpression(R"(\bm_sem\s*=\s*CreateSemaphore\s*\()").match(
			QString::fromUtf8(branch)).hasMatch(),
			"Windows constructor must store CreateSemaphore's returned handle in m_sem");
#endif
	}

	//! When PulseAudio cannot allocate a stream, the READY callback must report
	//! the failure before it attempts to register callbacks on the null stream.
	void pulseStreamCreationFailureIsHandledBeforeCallbacks()
	{
		QFile sourceFile(QStringLiteral(LMMS_TEST_DATA_DIR "/../../src/core/audio/AudioPulseAudio.cpp"));
		QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
			"Could not read AudioPulseAudio.cpp");
		const auto source = QString::fromUtf8(sourceFile.readAll());
		const auto streamCreation = source.indexOf(QStringLiteral("m_s = pa_stream_new"));
		const auto stateCallback = source.indexOf(QStringLiteral("pa_stream_set_state_callback"), streamCreation);
		QVERIFY(streamCreation >= 0);
		QVERIFY(stateCallback > streamCreation);

		const auto nullCheck = source.indexOf(QStringLiteral("if ( _this->m_s == nullptr )"), streamCreation);
		const auto failureSignal = source.indexOf(QStringLiteral("signalConnected( false )"), nullCheck);
		const auto stopSwitchCase = source.indexOf(QStringLiteral("break;"), failureSignal);
		QVERIFY2(nullCheck > streamCreation && nullCheck < stateCallback,
			"the stream result must be checked before callback registration");
		QVERIFY2(failureSignal > nullCheck && failureSignal < stateCallback,
			"a null stream must report connection failure before callback registration");
		QVERIFY2(stopSwitchCase > failureSignal && stopSwitchCase < stateCallback,
			"the READY handler must stop after reporting stream creation failure");
	}

	//! A worker parked in the wait condition must end its loop when it is asked to
	//! quit, without anyone calling startAndWaitForJobs() to wake it. On the tree
	//! without the bounded re-check it never does: it blocks forever and the QThread
	//! object is then destroyed while running.
	void quitWithoutAWakeupStopsAParkedWorker()
	{
		std::vector<std::unique_ptr<AudioEngineWorkerThread>> workers;
		for (int i = 0; i < kWorkers; ++i)
		{
			workers.emplace_back(new AudioEngineWorkerThread(Engine::audioEngine()));
			workers.back()->start();
		}

		// Nothing wakes a worker of this pool: the engine's own device is stopped and
		// no render is in flight, so a started worker is parked by now.
		QThread::msleep(kParkSettleMs);

		for (auto& w : workers)
		{
			w->quit();
		}

		int stranded = 0;
		for (auto& w : workers)
		{
			if (!w->wait(kExitBudgetMs))
			{
				// Deliberately leak the running QThread rather than destroying it:
				// destroying a running QThread is the very thing under test (Qt
				// aborts the process for it) and an abort would hide the assertion
				// that has already failed.
				w.release();
				++stranded;
			}
		}
		QCOMPARE(stranded, 0);
	}

	//! The product's own destroy sequence, in its own order: quit every worker,
	//! wake the condition once (as ~AudioEngine does through
	//! startAndWaitForJobs()), then join. Every worker must be joined.
	void quitThenWakeAndJoinStopsEveryWorker()
	{
		std::vector<std::unique_ptr<AudioEngineWorkerThread>> workers;
		for (int i = 0; i < kWorkers; ++i)
		{
			workers.emplace_back(new AudioEngineWorkerThread(Engine::audioEngine()));
			workers.back()->start();
		}
		QThread::msleep(kParkSettleMs);

		for (auto& w : workers)
		{
			w->quit();
		}
		AudioEngineWorkerThread::startAndWaitForJobs();

		int stranded = 0;
		for (auto& w : workers)
		{
			if (!w->wait(kExitBudgetMs))
			{
				w.release();
				++stranded;
			}
		}
		QCOMPARE(stranded, 0);
	}

	//! A playback connection the server REFUSES must not leave the device
	//! reporting itself connected. The device has to take the engine's own
	//! render fallback instead of running on a stream that never played: the
	//! engine renders a period, and the PulseAudio mainloop is never entered.
	void rejectedPulsePlaybackConnectionFallsBackToRendering()
	{
#if defined(LMMS_HAVE_PULSEAUDIO) && defined(Q_OS_LINUX)
		setPulseScenario(PulseScenario::PlaybackConnectRejected);
		bool success = false;
		AudioPulseAudio device(success, Engine::audioEngine());
		QVERIFY(success);

		// The tap: the engine emits nextAudioBuffer once per rendered period,
		// and the fallback renders the periods the PulseAudio path would have
		// written into the stream. The semaphore is the handshake - the case
		// waits for the device's own thread to report a rendered period.
		std::atomic<int> renderedPeriods{0};
		QSemaphore rendered;
		PulseTestCleanup cleanup{device, QObject::connect(Engine::audioEngine(),
			&AudioEngine::nextAudioBuffer, this, [&renderedPeriods, &rendered](const SampleFrame*) {
				renderedPeriods.fetch_add(1, std::memory_order_relaxed);
				rendered.release();
			}, Qt::DirectConnection)};

		device.startProcessing();
		const bool fellBack = rendered.tryAcquire(1, kPulseHandshakeMs);
		device.stopProcessing();

		QVERIFY2(gFakePulse.playbackConnectionCalled.load(std::memory_order_acquire),
			"the device never asked for a playback connection, so the case proved nothing");
		QVERIFY2(fellBack, "the device stayed on the PulseAudio path after the connect was refused");
		QVERIFY(renderedPeriods.load(std::memory_order_relaxed) > 0);
		// The refused connect is answered while the context callback runs, so
		// the device never iterates the mainloop it has no stream on.
		QCOMPARE(gFakePulse.mainloopIterations.load(std::memory_order_relaxed), 0);
#else
		QSKIP("PulseAudio symbol interposition is exercised on Linux builds with PulseAudio support");
#endif
	}

	//! A stream that FAILS after the connection was established must take the
	//! device off the PulseAudio path: the engine renders through the fallback
	//! instead of the device sitting on a dead stream that produces silence.
	void failedPulseStreamLeavesThePulseAudioPath()
	{
#if defined(LMMS_HAVE_PULSEAUDIO) && defined(Q_OS_LINUX)
		setPulseScenario(PulseScenario::StreamFailsLater);
		bool success = false;
		AudioPulseAudio device(success, Engine::audioEngine());
		QVERIFY(success);

		std::atomic<int> renderedPeriods{0};
		QSemaphore rendered;
		PulseTestCleanup cleanup{device, QObject::connect(Engine::audioEngine(),
			&AudioEngine::nextAudioBuffer, this, [&renderedPeriods, &rendered](const SampleFrame*) {
				renderedPeriods.fetch_add(1, std::memory_order_relaxed);
				rendered.release();
			}, Qt::DirectConnection)};

		device.startProcessing();
		const bool fellBack = rendered.tryAcquire(1, kPulseHandshakeMs);
		device.stopProcessing();

		QVERIFY2(gFakePulse.playbackConnectionCalled.load(std::memory_order_acquire),
			"the device never asked for a playback connection, so the case proved nothing");
		QVERIFY2(gFakePulse.streamFailureDelivered.load(std::memory_order_acquire),
			"the fake never handed the stream its failure, so the case proved nothing");
		QVERIFY2(fellBack, "the device stayed on the PulseAudio path after the stream failed");
		QVERIFY(renderedPeriods.load(std::memory_order_relaxed) > 0);
		// The failure arrives on the first mainloop iteration and the device
		// leaves the loop with it, so no second iteration happens.
		QCOMPARE(gFakePulse.mainloopIterations.load(std::memory_order_relaxed), 1);
#else
		QSKIP("PulseAudio symbol interposition is exercised on Linux builds with PulseAudio support");
#endif
	}

	void contextCreationFailureFreesPulseMainloop()
	{
#if defined(LMMS_HAVE_PULSEAUDIO) && defined(Q_OS_LINUX)
		setPulseScenario(PulseScenario::ContextCreationFails);
		bool success = false;
		AudioPulseAudio device(success, Engine::audioEngine());
		QVERIFY(success);

		device.startProcessing();
		const bool threadStopped = device.wait(kExitBudgetMs);
		device.stopProcessing();

		QVERIFY(threadStopped);
		QCOMPARE(gFakePulse.mainloopFreeCalls.load(std::memory_order_relaxed), 1U);
#else
		QSKIP("PulseAudio symbol interposition is exercised on Linux builds with PulseAudio support");
#endif
	}
};

QTEST_GUILESS_MAIN(AudioEngineTeardownTest)
#include "AudioEngineTeardownTest.moc"
