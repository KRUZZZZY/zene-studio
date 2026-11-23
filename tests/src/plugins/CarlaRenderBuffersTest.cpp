/*
 * CarlaRenderBuffersTest.cpp - the planar scratch behind CarlaInstrument::playImpl()
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
// playImpl() now hands its whole render step to CarlaRenderBuffers::render(), which is what runs here.

#include <QtTest>

#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#	include <pthread.h>
#	define ZENE_CARLA_RENDER_TEST_HAS_PTHREAD 1
#endif

#include "AudioEngine.h"
#include "CarlaRenderBuffers.h"
#include "SampleFrame.h"
#include "../core/AllocationProbe.h"

namespace
{

constexpr std::uint32_t PeriodFrames = 64;
constexpr float Sentinel = 9.0f;

auto sentinelFrames(std::size_t count) -> std::vector<lmms::SampleFrame>
{
	return std::vector<lmms::SampleFrame>(count, lmms::SampleFrame(Sentinel, Sentinel));
}

//! What a process callback saw and did; filled on whatever thread renders, checked on the test thread.
struct ProcessRecord
{
	std::uint32_t calls = 0;
	std::uint32_t frames = 0;
	bool planesWereSilent = true;
};

//! A callback standing in for Carla's process: records its input and writes a recognisable ramp.
struct RampProcess
{
	ProcessRecord* record;

	void operator()(float** planes, std::uint32_t frames) const
	{
		++record->calls;
		record->frames = frames;
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			if (planes[0][i] != 0.0f || planes[1][i] != 0.0f) { record->planesWereSilent = false; }
			planes[0][i] = static_cast<float>(i);
			planes[1][i] = -static_cast<float>(i);
		}
	}
};

#ifdef ZENE_CARLA_RENDER_TEST_HAS_PTHREAD
//! Renders one full-size period on a thread whose stack is far smaller than that period's two planes.
struct SmallStackRender
{
	lmms::CarlaRenderBuffers* planes;
	std::vector<lmms::SampleFrame>* out;
	ProcessRecord record;

	static void* run(void* arg)
	{
		auto& self = *static_cast<SmallStackRender*>(arg);
		self.planes->render(std::span<lmms::SampleFrame>(*self.out), RampProcess{&self.record});
		return nullptr;
	}
};
#endif

#ifdef ZENE_CARLA_RENDER_TEST_HAS_PTHREAD
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
#endif

} // namespace

class CarlaRenderBuffersTest : public QObject
{
	Q_OBJECT
private slots:
	//! The planes hold the engine's largest period, so no legitimate period is clipped.
	void capacityIsTheEnginesLargestPeriod()
	{
		const lmms::CarlaRenderBuffers planes;
		QCOMPARE(planes.capacity(), static_cast<std::size_t>(lmms::MAXIMUM_BUFFER_SIZE));
	}

	//! Normal path: the planes reach the callback silent, and its output is interleaved into the span.
	void renderClearsPlanesAndInterleavesOutput()
	{
		lmms::CarlaRenderBuffers planes;
		auto out = sentinelFrames(PeriodFrames);

		// A previous period left audio behind; the next one must still see silent planes.
		planes.left()[3] = 0.75f;
		planes.right()[PeriodFrames - 1] = -0.5f;

		ProcessRecord record;
		planes.render(std::span<lmms::SampleFrame>(out), RampProcess{&record});

		QCOMPARE(record.calls, std::uint32_t{1});
		QCOMPARE(record.frames, PeriodFrames);
		QVERIFY(record.planesWereSilent);
		for (std::uint32_t i = 0; i < PeriodFrames; ++i)
		{
			QCOMPARE(out[i][0], static_cast<float>(i));
			QCOMPARE(out[i][1], -static_cast<float>(i));
		}
	}

	//! Exhaustion path: a span longer than the planes is clipped; the frames beyond them are not touched.
	void renderClipsASpanLongerThanThePlanes()
	{
		lmms::CarlaRenderBuffers planes;
		const auto capacity = planes.capacity();
		auto out = sentinelFrames(capacity * 2 + 1);

		ProcessRecord record;
		planes.render(std::span<lmms::SampleFrame>(out), RampProcess{&record});

		QCOMPARE(record.frames, static_cast<std::uint32_t>(capacity));
		QVERIFY(record.planesWereSilent);
		QCOMPARE(out[capacity - 1][0], static_cast<float>(capacity - 1));
		QCOMPARE(out[capacity][0], Sentinel);
		QCOMPARE(out[capacity][1], Sentinel);
		QCOMPARE(out[out.size() - 1][0], Sentinel);
	}

	//! A span shorter than the planes touches only its own frames.
	void renderLeavesFramesPastASmallSpanAlone()
	{
		lmms::CarlaRenderBuffers planes;
		auto out = sentinelFrames(PeriodFrames);

		ProcessRecord record;
		planes.render(std::span<lmms::SampleFrame>(out).first(PeriodFrames / 2), RampProcess{&record});

		QCOMPARE(record.frames, PeriodFrames / 2);
		QCOMPARE(out[PeriodFrames / 2 - 1][0], static_cast<float>(PeriodFrames / 2 - 1));
		QCOMPARE(out[PeriodFrames / 2][0], Sentinel);
	}

	//! Reconfiguration path: periods of changing size reuse the same storage and allocate and free nothing.
	void periodsAfterConstructionAllocateNothing()
	{
		lmms::CarlaRenderBuffers planes;
		auto out = sentinelFrames(PeriodFrames * 2);
		float* const left = planes.left();
		float* const right = planes.right();

		ProcessRecord record;
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (std::uint32_t period = 0; period < 1000; ++period)
		{
			planes.render(std::span<lmms::SampleFrame>(out).first(PeriodFrames + period % PeriodFrames),
				RampProcess{&record});
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
		QCOMPARE(lmms::test::tlFreeCount, std::uint64_t{0});
		QCOMPARE(planes.left(), left);
		QCOMPARE(planes.right(), right);
		QCOMPARE(record.calls, std::uint32_t{1000});
	}

	//! The cause itself: a period as large as the engine allows must render on a thread whose usable stack is
	//! smaller than that period's two planes (2 * 4096 * 4 bytes = 32 KiB). Stack arrays sized from the
	//! period overflow it; the preallocated planes need a fixed few pointers.
	void maximumPeriodRendersOnASmallStack()
	{
#ifdef ZENE_CARLA_RENDER_TEST_HAS_PTHREAD
		lmms::CarlaRenderBuffers planes;
		auto out = sentinelFrames(planes.capacity());

		// The smallest stack the C library will start a thread on (it holds the thread's TLS as well), plus a
		// little headroom for the callback frames. That leaves about PTHREAD_STACK_MIN (16 KiB) usable.
		const auto stackBytes = smallestStartableStack() + 8 * 1024;
		QVERIFY2(stackBytes < 2 * planes.capacity() * sizeof(float) + smallestStartableStack(),
			"the thread's usable stack must be smaller than the two planes of a maximum period");

		SmallStackRender job{&planes, &out, {}};
		pthread_attr_t attributes;
		QCOMPARE(pthread_attr_init(&attributes), 0);
		QCOMPARE(pthread_attr_setstacksize(&attributes, stackBytes), 0);
		pthread_t thread;
		QCOMPARE(pthread_create(&thread, &attributes, &SmallStackRender::run, &job), 0);
		QCOMPARE(pthread_join(thread, nullptr), 0);
		pthread_attr_destroy(&attributes);

		QCOMPARE(job.record.calls, std::uint32_t{1});
		QCOMPARE(job.record.frames, static_cast<std::uint32_t>(planes.capacity()));
		QVERIFY(job.record.planesWereSilent);
		QCOMPARE(out[planes.capacity() - 1][0], static_cast<float>(planes.capacity() - 1));
#else
		QSKIP("needs POSIX threads to set a thread's stack size");
#endif
	}
};

QTEST_APPLESS_MAIN(CarlaRenderBuffersTest)
#include "CarlaRenderBuffersTest.moc"
