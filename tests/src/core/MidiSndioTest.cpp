/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include <QtTest>

#include <atomic>
#include <cstddef>
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
	bytesWritten.tryAcquire(bytesWritten.available());
}
} // namespace

extern "C" mio_hdl* mio_open(const char*, unsigned int, int)
{
	return deviceOpens ? fakeHandle() : nullptr;
}

extern "C" void mio_close(mio_hdl*)
{
}

extern "C" size_t mio_write(mio_hdl*, const void* buf, size_t size)
{
	++mioWriteCalls;
	writerThread = QThread::currentThread();
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
};

QTEST_APPLESS_MAIN(MidiSndioTest)
#include "MidiSndioTest.moc"
