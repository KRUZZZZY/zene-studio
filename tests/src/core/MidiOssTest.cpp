/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include <QtTest>

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <type_traits>

#include <QSemaphore>
#include <QThread>

#include "MidiClient.h"

#define private public
#include "MidiOss.h"
#undef private

namespace
{
bool deviceOpens = true;
int fakeFdStorage = 0;
int ossWriteCalls = 0;
QByteArray deviceBytes;
QSemaphore bytesWritten;
QThread* writerThread = nullptr;
int stallWrites = 0;
QSemaphore writeStarted;
QSemaphore writeGate;
QSemaphore readerFinished;
int closeCalls = 0;
QByteArray closedBytes;
QSemaphore closed;

void resetDevice(bool opens)
{
	deviceOpens = opens;
	ossWriteCalls = 0;
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

extern "C" ssize_t zeneOssDeviceWrite(int, const void* buf, size_t size)
{
	++ossWriteCalls;
	writerThread = QThread::currentThread();
	if (stallWrites > 0)
	{
		--stallWrites;
		writeStarted.release();
		writeGate.acquire();
	}
	deviceBytes.append(static_cast<const char*>(buf), static_cast<qsizetype>(size));
	bytesWritten.release(static_cast<int>(size));
	return static_cast<ssize_t>(size);
}

extern "C" int zeneOssDeviceClose(int)
{
	++closeCalls;
	closedBytes = deviceBytes;
	closed.release();
	return 0;
}

class TestMidiOss final : public lmms::MidiOss
{
public:
	using lmms::MidiOss::sendByte;
};

class MidiOssTest : public QObject
{
	Q_OBJECT

private slots:
	void quitFlagIsAtomic()
	{
		QVERIFY((std::is_same_v<decltype(lmms::MidiOss::m_quit), std::atomic_bool>));
	}

	void failedOpenDoesNotWrite()
	{
		qputenv("MIDIDEV", "/definitely/not/a/device");
		resetDevice(false);
		TestMidiOss midi;
		midi.sendByte(0x90);
		QCOMPARE(ossWriteCalls, 0);
		qunsetenv("MIDIDEV");
	}

	void deviceWriteRunsOffTheCallerThread()
	{
		const QByteArray path = "/tmp/scratchpad/oss-dev.bin";
		QFile::remove(path);
		{ QFile f(path); f.open(QIODevice::WriteOnly); f.close(); }
		qputenv("MIDIDEV", path.constData());
		resetDevice(true);
		TestMidiOss midi;
		midi.sendByte(0x90);
		QVERIFY(bytesWritten.tryAcquire(1, 5000));
		QVERIFY(writerThread != nullptr);
		QVERIFY(writerThread != QThread::currentThread());
		QCOMPARE(deviceBytes, QByteArray::fromHex("90"));
		qunsetenv("MIDIDEV");
	}

	void queuedBytesReachTheDeviceInSendOrder()
	{
		const QByteArray path = "/tmp/scratchpad/oss-dev.bin";
		QFile::remove(path);
		{ QFile f(path); f.open(QIODevice::WriteOnly); f.close(); }
		qputenv("MIDIDEV", path.constData());
		resetDevice(true);
		TestMidiOss midi;
		midi.sendByte(0x90);
		midi.sendByte(0x3C);
		midi.sendByte(0x7F);
		QVERIFY(bytesWritten.tryAcquire(3, 5000));
		QCOMPARE(deviceBytes, QByteArray::fromHex("903C7F"));
		qunsetenv("MIDIDEV");
	}

	//! A device write that never returns must not hold teardown up, and the descriptor
	//! must NOT be closed while the writer is still inside that write: the writer owns
	//! the descriptor for as long as it may write to it, and closes it when it has
	//! written everything it was given. A teardown that closed the descriptor under the
	//! stalled writer would let the bytes queued behind it land on a closed - or, worse,
	//! a reused - descriptor.
	void stalledWriteTeardownKeepsTheDescriptorUntilTheWriterFinishes()
	{
		const QByteArray path = "/tmp/scratchpad/oss-dev.bin";
		QFile::remove(path);
		{ QFile f(path); f.open(QIODevice::WriteOnly); f.close(); }
		qputenv("MIDIDEV", path.constData());
		resetDevice(true);
		stallWrites = 1;
		{
			// The writer is held inside its first device write while two more bytes
			// queue behind it. The destructor gives up on the stalled writer after the
			// teardown grace period and returns; the writer is still inside the write.
			TestMidiOss midi;
			midi.sendByte(0x90);
			QVERIFY(writeStarted.tryAcquire(1, 5000));
			midi.sendByte(0x3C);
			midi.sendByte(0x7F);
		}

		// The descriptor is still the writer's: it was not closed under the stalled write.
		QCOMPARE(closeCalls, 0);

		// The write returns: the writer delivers the queued bytes and closes the
		// descriptor itself, exactly once.
		writeGate.release();
		QVERIFY(closed.tryAcquire(1, 5000));
		QCOMPARE(closeCalls, 1);
		QCOMPARE(closedBytes, QByteArray::fromHex("903C7F"));
		qunsetenv("MIDIDEV");
	}
};

QTEST_APPLESS_MAIN(MidiOssTest)
#include "MidiOssTest.moc"
