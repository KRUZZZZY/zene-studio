/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

/*! Q050-488: the ALSA raw-MIDI worker stops cooperatively.
 *
 *  The worker polled the device with a 10 s timeout while the destructor waited 1 s
 *  and then called QThread::terminate(), so closing an idle device always took a
 *  second and always killed the thread. The fakes below replace libasound's rawmidi
 *  entry points (the executable's definitions win over the shared library, the same
 *  technique as MidiOutQueueTest and AudioAlsaPollDescriptorTest): the "device" is a
 *  pipe that only the test writes to, so with no input the worker blocks in poll()
 *  exactly as on an idle real device. Hermetic; no ALSA hardware or sequencer needed. */

#include <QtTest>

#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "AllocationProbe.h"
#include "MidiAlsaRaw.h"
#include "RetroMidiRing.h"

namespace
{

struct FakeRawmidi
{
	std::atomic<bool> open{false};
};

FakeRawmidi g_input;
FakeRawmidi g_output;
int g_devicePipe[2] = {-1, -1};

std::atomic<int> g_openResult{0};
std::atomic<int> g_descriptorCount{1};
std::atomic<int> g_descriptorResult{1};
std::atomic<int> g_closeCalls{0};
std::atomic<int> g_callsOnClosedHandle{0};
std::atomic<int> g_writeCalls{0};
std::atomic<int> g_lastWritten{-1};
std::atomic<bool> g_workerRunningAtClose{false};
const QThread* g_client = nullptr;

FakeRawmidi* fake(snd_rawmidi_t* handle)
{
	return reinterpret_cast<FakeRawmidi*>(handle);
}

void noteUse(snd_rawmidi_t* handle)
{
	if (handle == nullptr || !fake(handle)->open.load()) { ++g_callsOnClosedHandle; }
}

void resetDevice()
{
	g_openResult = 0;
	g_descriptorCount = 1;
	g_descriptorResult = 1;
	g_closeCalls = 0;
	g_callsOnClosedHandle = 0;
	g_writeCalls = 0;
	g_lastWritten = -1;
	g_workerRunningAtClose = false;
	g_client = nullptr;
}

} // namespace

extern "C" int snd_rawmidi_open(snd_rawmidi_t** input, snd_rawmidi_t** output, const char*, int)
{
	if (g_openResult.load() < 0) { return g_openResult.load(); }
	g_input.open = true;
	g_output.open = true;
	*input = reinterpret_cast<snd_rawmidi_t*>(&g_input);
	*output = reinterpret_cast<snd_rawmidi_t*>(&g_output);
	return 0;
}

extern "C" int snd_rawmidi_close(snd_rawmidi_t* handle)
{
	noteUse(handle);
	++g_closeCalls;
	if (g_client != nullptr && g_client->isRunning()) { g_workerRunningAtClose = true; }
	fake(handle)->open = false;
	return 0;
}

extern "C" int snd_rawmidi_nonblock(snd_rawmidi_t* handle, int)
{
	noteUse(handle);
	return 0;
}

extern "C" int snd_rawmidi_poll_descriptors_count(snd_rawmidi_t* handle)
{
	noteUse(handle);
	return g_descriptorCount.load();
}

extern "C" int snd_rawmidi_poll_descriptors(snd_rawmidi_t* handle, pollfd* descriptors, unsigned int space)
{
	noteUse(handle);
	if (space >= 1) { descriptors[0] = {g_devicePipe[0], POLLIN, 0}; }
	return g_descriptorResult.load();
}

extern "C" int snd_rawmidi_poll_descriptors_revents(snd_rawmidi_t* handle, pollfd* descriptors, unsigned int,
		unsigned short* revents)
{
	noteUse(handle);
	*revents = descriptors[0].revents;
	return 0;
}

extern "C" ssize_t snd_rawmidi_read(snd_rawmidi_t* handle, void* buffer, size_t size)
{
	noteUse(handle);
	if (buffer == nullptr || size == 0) { return 0; }
	const ssize_t n = ::read(g_devicePipe[0], buffer, size);
	return n < 0 ? -errno : n;
}

extern "C" ssize_t snd_rawmidi_write(snd_rawmidi_t* handle, const void* buffer, size_t size)
{
	noteUse(handle);
	++g_writeCalls;
	g_lastWritten = *static_cast<const unsigned char*>(buffer);
	return static_cast<ssize_t>(size);
}

class TestMidiAlsaRaw final : public lmms::MidiAlsaRaw
{
public:
	using lmms::MidiAlsaRaw::sendByte;
	using lmms::MidiAlsaRaw::stopWorker;
};

class MidiAlsaRawShutdownTest : public QObject
{
	Q_OBJECT

private slots:
	void init()
	{
		resetDevice();
		QVERIFY(::pipe(g_devicePipe) == 0);
		QVERIFY(::fcntl(g_devicePipe[0], F_SETFL, ::fcntl(g_devicePipe[0], F_GETFL) | O_NONBLOCK) == 0);
	}

	void cleanup()
	{
		for (int& fd : g_devicePipe)
		{
			if (fd >= 0) { ::close(fd); }
			fd = -1;
		}
	}

	//! The reproduced hang. A worker blocked waiting for input must stop within the
	//! bound, repeatedly. The bound: after the stop request the worker finishes at most
	//! its 5 ms pacing sleep and returns from a poll the wake pipe has made readable, so
	//! well under 10 ms of work; 500 ms leaves room for a loaded machine and is half of
	//! the old 1000 ms wait, which every close of an idle device used to exhaust before
	//! QThread::terminate(). Before the change each close took >= 1000 ms.
	void closingAWorkerBlockedOnInputIsBounded()
	{
		constexpr auto bound = std::chrono::milliseconds(500);
		for (int round = 0; round < 5; ++round)
		{
			resetDevice();
			auto* client = new TestMidiAlsaRaw;
			g_client = client;
			QVERIFY(client->isRunning());
			QTest::qWait(50); // let the worker reach its blocking poll()
			const auto started = std::chrono::steady_clock::now();
			delete client;
			const auto took = std::chrono::steady_clock::now() - started;
			QVERIFY2(took < bound, qPrintable(QStringLiteral("round %1: close took %2 ms")
				.arg(round).arg(std::chrono::duration_cast<std::chrono::milliseconds>(took).count())));
			QCOMPARE(g_closeCalls.load(), 2);
			QVERIFY(!g_workerRunningAtClose.load());
			QCOMPARE(g_callsOnClosedHandle.load(), 0);
		}
	}

	//! Duplicate stop: stopWorker() twice and then the destructor (a third stop) join
	//! once, close each handle once and touch nothing that was closed.
	void duplicateStopIsHarmless()
	{
		{
			TestMidiAlsaRaw client;
			g_client = &client;
			QVERIFY(client.isRunning());
			QTest::qWait(50); // let the worker reach its blocking poll()
			const auto started = std::chrono::steady_clock::now();
			client.stopWorker();
			QVERIFY(!client.isRunning());
			QVERIFY(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500));
			client.stopWorker();
			QVERIFY(!client.isRunning());
			QCOMPARE(g_closeCalls.load(), 0); // stopping releases nothing; the destructor does
		}
		QCOMPARE(g_closeCalls.load(), 2);
		QVERIFY(!g_workerRunningAtClose.load());
		QCOMPARE(g_callsOnClosedHandle.load(), 0);
	}

	//! Partial startup failure: the device opens but has no poll descriptor, or the
	//! descriptors cannot be retrieved. No worker starts, both handles are closed once
	//! at once, the destructor closes nothing twice, and output is refused afterwards.
	void partialStartupFailureReleasesOnce_data()
	{
		QTest::addColumn<int>("count");
		QTest::addColumn<int>("retrieval");
		QTest::newRow("no descriptors") << 0 << 1;
		QTest::newRow("negative count") << -EIO << 1;
		QTest::newRow("retrieval fails") << 1 << -EIO;
	}

	void partialStartupFailureReleasesOnce()
	{
		QFETCH(int, count);
		QFETCH(int, retrieval);
		g_descriptorCount = count;
		g_descriptorResult = retrieval;
		{
			TestMidiAlsaRaw client;
			QVERIFY(!client.isRunning());
			QCOMPARE(g_closeCalls.load(), 2);
			client.sendByte(0x90);
			QCOMPARE(g_writeCalls.load(), 0);
		}
		QCOMPARE(g_closeCalls.load(), 2);
		QCOMPARE(g_callsOnClosedHandle.load(), 0);
	}

	//! The device never opened: nothing is started, nothing is closed, and a byte sent
	//! to it is dropped instead of reaching ALSA with a null handle.
	void failedOpenStartsNothingAndDropsOutput()
	{
		g_openResult = -ENOENT;
		{
			TestMidiAlsaRaw client;
			QVERIFY(!client.isRunning());
			client.sendByte(0x90);
			client.stopWorker();
		}
		QCOMPARE(g_writeCalls.load(), 0);
		QCOMPARE(g_closeCalls.load(), 0);
		QCOMPARE(g_callsOnClosedHandle.load(), 0);
	}

	//! Delivery is unchanged: a note-on written to the device reaches the parser (seen
	//! through the client's retrospective capture), and the worker still stops quickly.
	void inputStillReachesTheParser()
	{
		TestMidiAlsaRaw client;
		g_client = &client;
		client.retroCapture().arm(true);
		const std::array<unsigned char, 3> noteOn{0x90, 60, 100};
		QCOMPARE(::write(g_devicePipe[1], noteOn.data(), noteOn.size()), ssize_t{3});
		std::array<lmms::RetroMidiEvent, 4> seen{};
		std::size_t n = 0;
		QTRY_VERIFY_WITH_TIMEOUT((n = client.retroCapture().ring().copyOut(seen.data(), seen.size())) == 1, 5000);
		QCOMPARE(int{seen[0].param1}, 60);
		QCOMPARE(int{seen[0].param2}, 100);
		client.retroCapture().arm(false);
	}

	//! The audio-thread entry point (MidiClientRaw::processOutEvent -> sendByte) still
	//! hands the byte to ALSA directly: no allocation, no free, no wake-pipe traffic.
	void sendByteStaysAllocationFree()
	{
		TestMidiAlsaRaw client;
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		client.sendByte(0x90);
		client.sendByte(0x3c);
		lmms::test::tlCountAllocations = false;
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
		QCOMPARE(lmms::test::tlFreeCount, std::uint64_t{0});
		QCOMPARE(g_writeCalls.load(), 2);
		QCOMPARE(g_lastWritten.load(), 0x3c);
	}
};

QTEST_GUILESS_MAIN(MidiAlsaRawShutdownTest)
#include "MidiAlsaRawShutdownTest.moc"
