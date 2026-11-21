/*
 * SfzPlayerRealtimeTest.cpp - Q050-484: the SFZ instrument's render path is real-time safe
 *
 * The render path of the SFZ player - the MIDI entry point, the queue, and the sfizz render call - runs with the
 * allocation probe counting: playing notes of a sample far longer than sfizz preloads (so it has to be streamed
 * from the file while it plays, a cache miss for every block past the preload) must allocate and free nothing on
 * the render thread. On Linux the sample reads are also intercepted: every read of a sample file must come from a
 * thread other than the render thread (sfizz's own workers), and some must happen during playback - proof that the
 * streaming happened at all, and happened off the render thread.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QTemporaryDir>

#include <atomic>
#include <mutex>
#include <set>
#include <thread>

#ifdef Q_OS_LINUX
#include <dlfcn.h>
#include <sndfile.h>
#endif

#include "../core/AllocationProbe.h"
#include "ControlRegistry.h"
#include "NotePlayHandle.h"
#include "SfzTestSupport.h"

using namespace lmms;
using namespace lmms::sfz_test;

#ifdef Q_OS_LINUX
namespace
{
std::mutex readersMutex;
std::set<std::thread::id> sampleReaders;
std::atomic<bool> recordReads{false};
std::atomic<long> readsDuringPlayback{0};

void noteRead()
{
	if (!recordReads.load()) { return; }
	const std::lock_guard<std::mutex> lock{readersMutex};
	sampleReaders.insert(std::this_thread::get_id());
	readsDuringPlayback.fetch_add(1);
}
} // namespace

// The executable's copies win over libsndfile's for the module (the test target exports its symbols), so every
// sample read the player makes passes through here.
extern "C" __attribute__((visibility("default"))) sf_count_t sf_readf_float(SNDFILE* file, float* buffer, sf_count_t frames)
{
	using Fn = sf_count_t (*)(SNDFILE*, float*, sf_count_t);
	static const auto real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "sf_readf_float"));
	noteRead();
	return real(file, buffer, frames);
}

extern "C" __attribute__((visibility("default"))) sf_count_t sf_readf_short(SNDFILE* file, short* buffer, sf_count_t frames)
{
	using Fn = sf_count_t (*)(SNDFILE*, short*, sf_count_t);
	static const auto real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "sf_readf_short"));
	noteRead();
	return real(file, buffer, frames);
}
#endif

class SfzPlayerRealtimeTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! Notes, a key switch and a release on a streamed sample, the render into a buffer made beforehand: zero allocations
	//! and zero frees on the render thread (MIDI entry point, queue and sfizz render all inside the counted region), and
	//! the sample file is read only by other threads.
	void renderingAStreamedSampleAllocatesNothingAndReadsNoFileOnTheRenderThread()
	{
		const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		const QString sfz = writeLayeredInstrument(m_dir.path(), rate * 3);
		SfzSession s;
		QVERIFY(s.start() && s.open(sfz));
		const auto frames = static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod());
		std::vector<SampleFrame> buffer(frames);
		for (int i = 0; i < 8; ++i) { s.instrument->play(std::span<SampleFrame>{buffer}); }

#ifdef Q_OS_LINUX
		{
			const std::lock_guard<std::mutex> lock{readersMutex};
			sampleReaders.clear();
		}
		readsDuringPlayback = 0;
		recordReads = true;
#endif
		std::uint64_t allocations = 0;
		std::uint64_t frees = 0;
		const auto counted = [&](auto&& action) {
			test::resetAllocationCount();
			test::tlCountAllocations = true;
			action();
			test::tlCountAllocations = false;
			allocations += test::tlAllocationCount;
			frees += test::tlFreeCount;
		};
		counted([&] { s.noteOn(60, 100); s.noteOn(62, 90, 40); s.noteOn(37, 90); });
		for (int block = 0; block < 200; ++block)
		{
			QTest::qWait(1);  // outside the counted region: lets sfizz's workers stream
			counted([&] { s.instrument->play(std::span<SampleFrame>{buffer}); });
			if (block == 100) { counted([&] { s.noteOff(60); s.noteOn(64, 80); }); }
		}
		counted([&] { s.noteOff(62); s.noteOff(64); });
#ifdef Q_OS_LINUX
		recordReads = false;
#endif
		QVERIFY2(SfzSession::peak(s.render(2), 0, 512) < 10.f, "the instrument is still alive");

		QCOMPARE(static_cast<qulonglong>(allocations), 0ull);
		QCOMPARE(static_cast<qulonglong>(frees), 0ull);
#ifdef Q_OS_LINUX
		QVERIFY2(readsDuringPlayback.load() > 0, "the three-second sample was streamed from its file while it played");
		const std::lock_guard<std::mutex> lock{readersMutex};
		QVERIFY2(sampleReaders.count(std::this_thread::get_id()) == 0, "no sample file was read on the render thread");
#endif
	}
};

QTEST_MAIN(SfzPlayerRealtimeTest)
#include "SfzPlayerRealtimeTest.moc"
