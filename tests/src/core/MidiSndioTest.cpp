/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <thread>
#include <type_traits>

#include <poll.h>
#include <sndio.h>

#include <QSemaphore>
#include <QSet>
#include <QThread>

#include "MidiClient.h"
#include "TimePos.h"

#define private public
#include "MidiSndio.h"
#undef private

namespace
{
//! Whether the fake device opens. A failed open hands MidiSndio no handle.
bool deviceOpens = true;
int fakeHandleStorage = 0;
int mioWriteCalls = 0;
//! Bytes the fake device has accepted, in order. The write side releases one
//! permit per byte, so a test can wait for exactly the bytes it sent.
QByteArray deviceBytes;
QSemaphore bytesWritten;
//! The thread that performed the most recent device write.
QThread* writerThread = nullptr;
//! The next `stallWrites` device writes block on writeGate, as a stalled device would.
int stallWrites = 0;
QSemaphore writeStarted;
QSemaphore writeGate;
//! One permit per reader-thread exit. The reader exits only after teardown has set m_quit.
QSemaphore readerFinished;
//! How many times the device handle was closed, the bytes the device had accepted
//! at the moment of the last close, and a permit released per close.
int closeCalls = 0;
QByteArray closedBytes;
QSemaphore closed;
//! Set by the reader thread's finished() signal; the fake mio_close records it, so a
//! test can tell whether the handle was closed while the reader could still use it.
std::atomic<bool> readerStopped{false};
bool closedAfterReaderStopped = false;
//! Write plan: when >= 0 the device accepts at most this many bytes in total, then
//! reports end-of-file and fails every further write with EIO.
int failAfterBytes = -1;
bool deviceEof = false;
//! One permit per mio_eof() and per mio_close(): the writer has reacted to a short write.
QSemaphore writerReacted;

mio_hdl* fakeHandle()
{
	return reinterpret_cast<mio_hdl*>(&fakeHandleStorage);
}

lmms::MidiEvent noteOn(unsigned char key = 0x3C)
{
	return lmms::MidiEvent(lmms::MidiNoteOn, 0, key, 0x7F);
}

lmms::MidiEvent noteOff(unsigned char key = 0x3C)
{
	return lmms::MidiEvent(lmms::MidiNoteOff, 0, key, 0);
}

//! Opens the write gate on every exit path, so a held writer can always finish.
struct GateRelease
{
	bool released = false;
	~GateRelease()
	{
		if (!released) { writeGate.release(); }
	}
	void release()
	{
		released = true;
		writeGate.release();
	}
};

lmms::MidiEvent filler(unsigned char value = 100)
{
	return lmms::MidiEvent(lmms::MidiControlChange, 0, 7, value);
}

//! Parses the device stream as three-byte frames (all test events have three bytes).
//! Returns the (channel * 128 + key) of every note still sounding at the end, and
//! flags a frame that does not start with a status byte or is cut short.
QSet<int> soundingNotes(const QByteArray& stream, bool* malformed)
{
	QSet<int> sounding;
	*malformed = stream.size() % 3 != 0;
	for (qsizetype i = 0; i + 2 < stream.size(); i += 3)
	{
		const unsigned status = static_cast<unsigned char>(stream[i]);
		const int key = static_cast<unsigned char>(stream[i + 1]);
		const unsigned velocity = static_cast<unsigned char>(stream[i + 2]);
		if (status < 0x80 || key >= 0x80 || velocity >= 0x80)
		{
			*malformed = true;
			return sounding;
		}
		const int id = static_cast<int>(status & 0x0F) * 128 + key;
		const unsigned type = status & 0xF0;
		if (type == 0x90 && velocity > 0) { sounding.insert(id); }
		else if (type == 0x80 || type == 0x90) { sounding.remove(id); }
	}
	return sounding;
}

//! Waits (bounded) until the client's overflow counter reaches `expected`.
bool overflowCountReaches(const lmms::MidiSndio& midi, std::uint64_t expected)
{
	const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (midi.outputQueueOverflowCount() != expected)
	{
		if (std::chrono::steady_clock::now() > limit) { return false; }
		std::this_thread::yield();
	}
	return true;
}

void resetDevice(bool opens)
{
	deviceOpens = opens;
	mioWriteCalls = 0;
	deviceBytes.clear();
	writerThread = nullptr;
	stallWrites = 0;
	closeCalls = 0;
	closedBytes.clear();
	readerStopped = false;
	closedAfterReaderStopped = false;
	failAfterBytes = -1;
	deviceEof = false;
	writerReacted.tryAcquire(writerReacted.available());
	bytesWritten.tryAcquire(bytesWritten.available());
	writeStarted.tryAcquire(writeStarted.available());
	writeGate.tryAcquire(writeGate.available());
	readerFinished.tryAcquire(readerFinished.available());
	closed.tryAcquire(closed.available());
}
} // namespace

extern "C" mio_hdl* mio_open(const char*, unsigned int, int)
{
	return deviceOpens ? fakeHandle() : nullptr;
}

extern "C" void mio_close(mio_hdl*)
{
	++closeCalls;
	closedBytes = deviceBytes;
	closedAfterReaderStopped = readerStopped.load();
	closed.release();
	writerReacted.release();
}

extern "C" int mio_eof(mio_hdl*)
{
	writerReacted.release();
	return deviceEof ? 1 : 0;
}

extern "C" size_t mio_write(mio_hdl*, const void* buf, size_t size)
{
	++mioWriteCalls;
	writerThread = QThread::currentThread();
	if (stallWrites > 0)
	{
		--stallWrites;
		writeStarted.release();
		writeGate.acquire();
	}
	if (failAfterBytes >= 0)
	{
		const qsizetype room = std::max<qsizetype>(0, failAfterBytes - deviceBytes.size());
		if (room == 0)
		{
			deviceEof = true;
			errno = EIO;
			return 0;
		}
		size = std::min(size, static_cast<size_t>(room));
	}
	deviceBytes.append(static_cast<const char*>(buf), static_cast<qsizetype>(size));
	bytesWritten.release(static_cast<int>(size));
	return size;
}

extern "C" size_t mio_read(mio_hdl*, void*, size_t)
{
	return 0;
}

extern "C" int mio_pollfd(mio_hdl*, struct pollfd*, int)
{
	return 0;
}

extern "C" int mio_revents(mio_hdl*, struct pollfd*)
{
	return 0;
}

class TestMidiSndio final : public lmms::MidiSndio
{
public:
	using lmms::MidiSndio::processOutEvent;
};

class MidiSndioTest : public QObject
{
	Q_OBJECT

private slots:
	//! The reader thread polls the stop flag while the destructor writes it, so
	//! the flag must be an atomic: a plain bool is a data race, volatile or not.
	void quitFlagIsAtomic()
	{
		QVERIFY((std::is_same_v<decltype(lmms::MidiSndio::m_quit), std::atomic_bool>));
	}

	void failedOpenDoesNotWrite()
	{
		resetDevice(false);
		TestMidiSndio midi;
		midi.processOutEvent(noteOn(), lmms::TimePos(), nullptr);
		QCOMPARE(mioWriteCalls, 0);
	}

	//! sendByte() runs on the audio thread during playback. The device write must
	//! happen on the writer thread, so a device that stalls cannot hold up the caller.
	void deviceWriteRunsOffTheCallerThread()
	{
		resetDevice(true);
		TestMidiSndio midi;
		midi.processOutEvent(noteOn(), lmms::TimePos(), nullptr);
		QVERIFY(bytesWritten.tryAcquire(3, 5000));
		QVERIFY(writerThread != nullptr);
		QVERIFY(writerThread != QThread::currentThread());
		QCOMPARE(deviceBytes, QByteArray::fromHex("903C7F"));
	}

	//! Queuing must not reorder the MIDI stream: the device sees whole messages in send order.
	void queuedMessagesReachTheDeviceInSendOrder()
	{
		resetDevice(true);
		TestMidiSndio midi;
		midi.processOutEvent(noteOn(), lmms::TimePos(), nullptr);
		midi.processOutEvent(noteOff(), lmms::TimePos(), nullptr);
		QVERIFY(bytesWritten.tryAcquire(6, 5000));
		QCOMPARE(deviceBytes, QByteArray::fromHex("903C7F803C00"));
	}

	//! The output queue is bounded by whole events. When it is full, the next complete
	//! event is refused and counted; no byte of it may reach the device. The writer is
	//! held in its first device write, so the queue is deterministically saturated.
	void fullOutputQueueRefusesWholeEventsAndCountsOverflow()
	{
		constexpr int QueueCapacity = 1024;
		resetDevice(true);
		stallWrites = 1;
		auto midi = std::make_unique<TestMidiSndio>();
		QVERIFY(midi->isRunning());
		GateRelease gate;

		midi->processOutEvent(filler(), lmms::TimePos(), nullptr);
		QVERIFY(writeStarted.tryAcquire(1, 5000));
		for (int i = 0; i < QueueCapacity; ++i)
		{
			midi->processOutEvent(filler(), lmms::TimePos(), nullptr);
		}
		QCOMPARE(static_cast<qulonglong>(midi->outputQueueOverflowCount()), 0ull);

		midi->processOutEvent(filler(0x7F), lmms::TimePos(), nullptr);
		QCOMPARE(static_cast<qulonglong>(midi->outputQueueOverflowCount()), 1ull);

		gate.release();
		midi.reset();
		QVERIFY(closed.tryAcquire(1, 5000));
		QCOMPARE(closedBytes, QByteArray::fromHex("B00764").repeated(QueueCapacity + 1));
		QVERIFY(!closedBytes.contains(QByteArray::fromHex("B0077F")));
	}

	//! A Note Off refused because the queue was full must not leave the note stuck: once
	//! the queue drains, a recovery note-off releases it. 40000 events of three bytes
	//! exceed any pipe, so a byte pipe in front of the device would drop bytes too.
	void refusedNoteOffIsReleasedAfterOverflowDrains()
	{
		constexpr int QueueCapacity = 1024;
		constexpr int Fillers = 40000;
		resetDevice(true);
		auto midi = std::make_unique<TestMidiSndio>();
		QVERIFY(midi->isRunning());
		midi->processOutEvent(noteOn(), lmms::TimePos(), nullptr);
		QVERIFY(bytesWritten.tryAcquire(3, 5000));
		stallWrites = 1;
		GateRelease gate;

		midi->processOutEvent(filler(), lmms::TimePos(), nullptr);
		QVERIFY(writeStarted.tryAcquire(1, 5000));
		for (int i = 0; i < Fillers; ++i)
		{
			midi->processOutEvent(filler(), lmms::TimePos(), nullptr);
		}
		midi->processOutEvent(noteOff(), lmms::TimePos(), nullptr);
		QCOMPARE(static_cast<qulonglong>(midi->outputQueueOverflowCount()),
			static_cast<qulonglong>(Fillers - QueueCapacity + 1));

		gate.release();
		midi.reset();
		QVERIFY(closed.tryAcquire(1, 5000));
		bool malformed = true;
		const QSet<int> sounding = soundingNotes(closedBytes, &malformed);
		QVERIFY(!malformed);
		QVERIFY(sounding.isEmpty());
		QCOMPARE(closedBytes.right(3), QByteArray::fromHex("803C00"));
		QCOMPARE(closedBytes.count(QByteArray::fromHex("B00764")), QueueCapacity + 1);
	}

	//! A device that fails for good keeps its handle until the reader thread has
	//! stopped: closing it earlier would free it under the reader. Events after the
	//! failure are refused and counted at once.
	void terminalWriteFailureKeepsTheHandleUntilTheReaderStops()
	{
		resetDevice(true);
		failAfterBytes = 1;
		auto midi = std::make_unique<TestMidiSndio>();
		QVERIFY(midi->isRunning());
		QObject::connect(static_cast<QThread*>(midi.get()), &QThread::finished, this,
			[] { readerStopped = true; }, Qt::DirectConnection);

		midi->processOutEvent(noteOn(), lmms::TimePos(), nullptr);
		QVERIFY(writerReacted.tryAcquire(1, 5000));
		QCOMPARE(closeCalls, 0);

		midi->processOutEvent(noteOn(0x40), lmms::TimePos(), nullptr);
		QVERIFY(overflowCountReaches(*midi, 2));
		QCOMPARE(closeCalls, 0);

		midi.reset();
		QVERIFY(closed.tryAcquire(1, 5000));
		QCOMPARE(closeCalls, 1);
		QVERIFY(closedAfterReaderStopped);
		QCOMPARE(closedBytes, QByteArray::fromHex("90"));
		QCOMPARE(mioWriteCalls, 2);
	}

	//! An event published just before teardown must reach the device, not be counted
	//! as undelivered: the writer must look at `closing` before it looks at the queue.
	void eventQueuedJustBeforeTeardownIsDelivered()
	{
		resetDevice(true);
		auto midi = std::make_unique<TestMidiSndio>();
		QVERIFY(midi->isRunning());
		midi->processOutEvent(noteOn(), lmms::TimePos(), nullptr);
		midi->processOutEvent(noteOff(), lmms::TimePos(), nullptr);
		midi.reset();
		QVERIFY(closed.tryAcquire(1, 5000));
		QCOMPARE(closeCalls, 1);
		QCOMPARE(closedBytes, QByteArray::fromHex("903C7F803C00"));
	}

	//! Ordinary teardown drains the queue: bytes still queued when teardown begins
	//! reach the device before the handle is closed. The writer is held inside its
	//! first device write while two more bytes queue behind it. The gate opens only
	//! after the reader thread has finished, which can only happen once the
	//! destructor has set m_quit, so the queue is provably non-empty at that moment.
	//! Discarding it would fail the close check.
	void queuedBytesAreDeliveredBeforeTeardownCloses()
	{
		resetDevice(true);
		stallWrites = 1;
		auto midi = std::make_unique<TestMidiSndio>();
		QVERIFY(midi->isRunning());
		// The reader emits finished() after its loop has seen m_quit. The callback
		// touches only globals, so it stays valid whatever happens to the client.
		QThread* reader = midi.get();
		QObject::connect(reader, &QThread::finished, this, [] { readerFinished.release(); },
			Qt::DirectConnection);
		midi->processOutEvent(noteOn(), lmms::TimePos(), nullptr);
		QVERIFY(writeStarted.tryAcquire(1, 5000));
		midi->processOutEvent(noteOff(), lmms::TimePos(), nullptr);
		midi->processOutEvent(noteOn(), lmms::TimePos(), nullptr);

		// The client is destroyed on its own thread; this thread only waits on globals.
		std::thread teardown([owned = std::move(midi)]() mutable { owned.reset(); });
		const bool teardownStarted = readerFinished.tryAcquire(1, 5000);
		// Release the writer on every path, so the teardown thread can always be joined.
		writeGate.release();
		teardown.join();

		QVERIFY(teardownStarted);
		QVERIFY(closed.tryAcquire(1, 5000));
		QCOMPARE(closeCalls, 1);
		QCOMPARE(closedBytes, QByteArray::fromHex("903C7F803C00903C7F"));
	}

	//! A device write that never returns must not hold teardown up, and the thread
	//! must not be killed inside the write. Teardown gives up on the writer and
	//! leaves the handle open; the writer closes it when the write returns, after
	//! delivering the bytes that were queued behind the stalled one.
	void stalledWriteTeardownDoesNotCloseTheHandleUnderTheWrite()
	{
		resetDevice(true);
		stallWrites = 1;
		{
			TestMidiSndio midi;
			midi.processOutEvent(noteOn(), lmms::TimePos(), nullptr);
			QVERIFY(writeStarted.tryAcquire(1, 5000));
			midi.processOutEvent(noteOff(), lmms::TimePos(), nullptr);
			midi.processOutEvent(noteOn(), lmms::TimePos(), nullptr);
		}
		QCOMPARE(closeCalls, 0);
		writeGate.release();
		QVERIFY(closed.tryAcquire(1, 5000));
		QCOMPARE(closeCalls, 1);
		QCOMPARE(closedBytes, QByteArray::fromHex("903C7F803C00903C7F"));
	}
};

QTEST_APPLESS_MAIN(MidiSndioTest)
#include "MidiSndioTest.moc"
