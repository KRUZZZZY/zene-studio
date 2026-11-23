/*
 * CarlaPlayImplTest.cpp - the production CarlaInstrument::playImpl() render path
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

// cluster new-p10-545: CarlaInstrument::playImpl() used two stack arrays sized from the audio period.
// This test builds a real CarlaInstrument over a fake Carla native plug-in (the test executable defines
// carla_get_native_rack_plugin(), which interposes the dummy library the carlabase module links) and drives
// the public Instrument::play() entry, so what it observes is the shipping code: the planes Carla's process
// callback receives, and whether a maximum-size period renders on a thread with a very small stack.

#include <QtTest>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#	include <pthread.h>
#	define ZENE_CARLA_PLAY_TEST_HAS_PTHREAD 1
#endif

#include "AudioEngine.h"
#include "Carla.h"
#include "Engine.h"
#include "SampleFrame.h"

namespace
{

#if CARLA_VERSION_HEX >= 0x030000
using CarlaInputBufferList = const float**;
#else
using CarlaInputBufferList = float**;
#endif

constexpr auto MaxFrames = static_cast<std::size_t>(lmms::MAXIMUM_BUFFER_SIZE);

//! True when @a address lies inside the calling thread's stack.
auto onCurrentThreadStack(const void* address) -> bool
{
#if defined(__linux__)
	pthread_attr_t attributes;
	if (pthread_getattr_np(pthread_self(), &attributes) != 0) { return false; }
	void* base = nullptr;
	std::size_t size = 0;
	const int result = pthread_attr_getstack(&attributes, &base, &size);
	pthread_attr_destroy(&attributes);
	if (result != 0) { return false; }
	const auto low = static_cast<const char*>(base);
	return static_cast<const char*>(address) >= low && static_cast<const char*>(address) < low + size;
#elif defined(__APPLE__)
	const auto top = static_cast<const char*>(pthread_get_stackaddr_np(pthread_self()));
	const auto low = top - pthread_get_stacksize_np(pthread_self());
	return static_cast<const char*>(address) >= low && static_cast<const char*>(address) < top;
#else
	(void)address;
	return false;
#endif
}

//! What the fake native plug-in saw in its process() calls.
struct Observation
{
	std::uint32_t calls = 0;
	std::uint32_t frames = 0;
	bool leftOnStack = false;
	bool rightOnStack = false;
	bool planesWereSilent = true;
};

Observation g_seen;

void fakeProcess(NativePluginHandle, CarlaInputBufferList, float** output, std::uint32_t frames,
	const NativeMidiEvent*, std::uint32_t)
{
	++g_seen.calls;
	g_seen.frames = frames;
	g_seen.leftOnStack = onCurrentThreadStack(output[0]);
	g_seen.rightOnStack = onCurrentThreadStack(output[1]);
	for (std::uint32_t i = 0; i < frames; ++i)
	{
		if (output[0][i] != 0.0f || output[1][i] != 0.0f) { g_seen.planesWereSilent = false; }
		output[0][i] = static_cast<float>(i);
		output[1][i] = -static_cast<float>(i);
	}
}

auto fakeInstantiate(const NativeHostDescriptor*) -> NativePluginHandle
{
	static int token = 0;
	return &token;
}

auto fakeParameterCount(NativePluginHandle) -> std::uint32_t { return 0; }

auto fakeDescriptor() -> const NativePluginDescriptor*
{
	static const NativePluginDescriptor descriptor = []
	{
		NativePluginDescriptor d{};
		d.instantiate = &fakeInstantiate;
		d.process = &fakeProcess;
		d.get_parameter_count = &fakeParameterCount;
		return d;
	}();
	return &descriptor;
}

//! Runs @a play on a new thread; a @a stackBytes of 0 keeps the system's default stack.
struct PlayJob
{
	lmms::CarlaInstrument* instrument;
	std::vector<lmms::SampleFrame>* out;

	static void* run(void* arg)
	{
		auto& job = *static_cast<PlayJob*>(arg);
		job.instrument->play(std::span<lmms::SampleFrame>(*job.out));
		return nullptr;
	}
};

#ifdef ZENE_CARLA_PLAY_TEST_HAS_PTHREAD
void* doNothing(void*) { return nullptr; }

//! The smallest stack size, in 1 KiB steps from 16 KiB, that pthread_create accepts on this system.
auto smallestStartableStack() -> std::size_t
{
	for (std::size_t bytes = 16 * 1024; bytes < 1024 * 1024; bytes += 1024)
	{
		pthread_attr_t attributes;
		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, bytes);
		pthread_t thread;
		const int created = pthread_create(&thread, &attributes, &doNothing, nullptr);
		pthread_attr_destroy(&attributes);
		if (created == 0)
		{
			pthread_join(thread, nullptr);
			return bytes;
		}
	}
	return 1024 * 1024;
}

auto playOnThread(PlayJob& job, std::size_t stackBytes) -> bool
{
	pthread_attr_t attributes;
	if (pthread_attr_init(&attributes) != 0) { return false; }
	if (stackBytes != 0 && pthread_attr_setstacksize(&attributes, stackBytes) != 0)
	{
		pthread_attr_destroy(&attributes);
		return false;
	}
	pthread_t thread;
	const int created = pthread_create(&thread, &attributes, &PlayJob::run, &job);
	pthread_attr_destroy(&attributes);
	if (created != 0) { return false; }
	return pthread_join(thread, nullptr) == 0;
}
#endif

auto sentinelFrames(std::size_t count) -> std::vector<lmms::SampleFrame>
{
	return std::vector<lmms::SampleFrame>(count, lmms::SampleFrame(9.0f, 9.0f));
}

} // namespace

// The carlabase module calls these through the dynamic symbol table; the executable's definitions come first
// in the lookup order, so CarlaInstrument sees the fake rack and patchbay plug-in instead of the dummy library's.
extern "C"
{
CARLA_API_EXPORT const NativePluginDescriptor* carla_get_native_rack_plugin() { return fakeDescriptor(); }
CARLA_API_EXPORT const NativePluginDescriptor* carla_get_native_patchbay_plugin() { return fakeDescriptor(); }
CARLA_API_EXPORT const char* carla_get_library_folder() { return "/nonexistent/lib/carla"; }
}

class CarlaPlayImplTest final : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { lmms::Engine::init(true); }
	void cleanupTestCase() { lmms::Engine::destroy(); }

	void init() { g_seen = Observation{}; }

	//! The planes Carla renders into belong to the instrument, not to the stack of the rendering thread,
	//! for a normal period and for a span longer than the engine's maximum (which is clipped, tail silent).
	void playImplRendersIntoPlanesOffTheStack()
	{
#ifdef ZENE_CARLA_PLAY_TEST_HAS_PTHREAD
		auto instrument = std::make_unique<lmms::CarlaInstrument>(nullptr, nullptr, false, nullptr, false);

		auto period = sentinelFrames(256);
		PlayJob job{instrument.get(), &period};
		QVERIFY(playOnThread(job, 0));
		QCOMPARE(g_seen.calls, std::uint32_t{1});
		QCOMPARE(g_seen.frames, std::uint32_t{256});
		QVERIFY2(!g_seen.leftOnStack && !g_seen.rightOnStack, "the render planes are on the thread's stack");
		QVERIFY(g_seen.planesWereSilent);
		for (std::uint32_t i = 0; i < 256; ++i)
		{
			QCOMPARE(period[i][0], static_cast<float>(i));
			QCOMPARE(period[i][1], -static_cast<float>(i));
		}

		g_seen = Observation{};
		auto oversized = sentinelFrames(MaxFrames * 2 + 1);
		PlayJob oversizedJob{instrument.get(), &oversized};
		QVERIFY(playOnThread(oversizedJob, 0));
		QCOMPARE(g_seen.frames, static_cast<std::uint32_t>(MaxFrames));
		QVERIFY2(!g_seen.leftOnStack && !g_seen.rightOnStack, "the render planes are on the thread's stack");
		QVERIFY(g_seen.planesWereSilent);
		QCOMPARE(oversized[MaxFrames - 1][0], static_cast<float>(MaxFrames - 1));
		QCOMPARE(oversized[MaxFrames][0], 0.0f);
		QCOMPARE(oversized[oversized.size() - 1][1], 0.0f);
#else
		QSKIP("needs POSIX threads to read the render thread's stack bounds");
#endif
	}

	//! The cause: a maximum-size period must render on a thread whose usable stack is smaller than that
	//! period's two planes (2 * 4096 * 4 bytes = 32 KiB). Stack arrays sized from the period overflow it.
	void maximumPeriodRendersOnASmallStack()
	{
#ifdef ZENE_CARLA_PLAY_TEST_HAS_PTHREAD
		auto instrument = std::make_unique<lmms::CarlaInstrument>(nullptr, nullptr, false, nullptr, false);
		auto out = sentinelFrames(MaxFrames);

		// The smallest stack the C library will start a thread on (it holds the thread's TLS as well) plus a
		// little headroom for playImpl()'s own frame: about PTHREAD_STACK_MIN (16 KiB) is usable.
		PlayJob job{instrument.get(), &out};
		QVERIFY(playOnThread(job, smallestStartableStack() + 8 * 1024));

		QCOMPARE(g_seen.calls, std::uint32_t{1});
		QCOMPARE(g_seen.frames, static_cast<std::uint32_t>(MaxFrames));
		QVERIFY(g_seen.planesWereSilent);
		QCOMPARE(out[MaxFrames - 1][0], static_cast<float>(MaxFrames - 1));
#else
		QSKIP("needs POSIX threads to set a thread's stack size");
#endif
	}
};

QTEST_GUILESS_MAIN(CarlaPlayImplTest)

#include "CarlaPlayImplTest.moc"
