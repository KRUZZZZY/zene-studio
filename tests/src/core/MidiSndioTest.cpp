/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include <QtTest>

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <type_traits>

#include <poll.h>
#include <sndio.h>

#include <QSemaphore>
#include <QThread>

#include "MidiClient.h"

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

mio_hdl* fakeHandle()
{
	return reinterpret_cast<mio_hdl*>(&fakeHandleStorage);
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
	closed.release();
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
	using lmms::MidiSndio::sendByte;
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
		midi.sendByte(0x90);
		QCOMPARE(mioWriteCalls, 0);
	}

	//! sendByte() runs on the audio thread during playback. The device write must
	//! happen on the writer thread, so a device that stalls cannot hold up the caller.
	void deviceWriteRunsOffTheCallerThread()
	{
		resetDevice(true);
		TestMidiSndio midi;
		midi.sendByte(0x90);
		QVERIFY(bytesWritten.tryAcquire(1, 5000));
		QVERIFY(writerThread != nullptr);
		QVERIFY(writerThread != QThread::currentThread());
		QCOMPARE(deviceBytes, QByteArray::fromHex("90"));
	}

	//! Queuing must not reorder the MIDI stream: the device sees bytes in send order.
	void queuedBytesReachTheDeviceInSendOrder()
	{
		resetDevice(true);
		TestMidiSndio midi;
		midi.sendByte(0x90);
		midi.sendByte(0x3C);
		midi.sendByte(0x7F);
		QVERIFY(bytesWritten.tryAcquire(3, 5000));
		QCOMPARE(deviceBytes, QByteArray::fromHex("903C7F"));
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
		midi->sendByte(0x90);
		QVERIFY(writeStarted.tryAcquire(1, 5000));
		midi->sendByte(0x3C);
		midi->sendByte(0x7F);

		// The client is destroyed on its own thread; this thread only waits on globals.
		std::thread teardown([owned = std::move(midi)]() mutable { owned.reset(); });
		const bool teardownStarted = readerFinished.tryAcquire(1, 5000);
		// Release the writer on every path, so the teardown thread can always be joined.
		writeGate.release();
		teardown.join();

		QVERIFY(teardownStarted);
		QVERIFY(closed.tryAcquire(1, 5000));
		QCOMPARE(closeCalls, 1);
		QCOMPARE(closedBytes, QByteArray::fromHex("903C7F"));
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
			midi.sendByte(0x90);
			QVERIFY(writeStarted.tryAcquire(1, 5000));
			midi.sendByte(0x3C);
			midi.sendByte(0x7F);
		}
		QCOMPARE(closeCalls, 0);
		writeGate.release();
		QVERIFY(closed.tryAcquire(1, 5000));
		QCOMPARE(closeCalls, 1);
		QCOMPARE(closedBytes, QByteArray::fromHex("903C7F"));
	}
};

QTEST_APPLESS_MAIN(MidiSndioTest)
#include "MidiSndioTest.moc"
