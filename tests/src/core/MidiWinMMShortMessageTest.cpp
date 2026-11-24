/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

/*! WinMM MIDI output path: short-message packing and bounded delivery.
 *
 *  Packing: MidiWinMM::processOutEvent sends winmmShortMessage( event ); the WinMM callback
 *  cannot be driven on this platform, so these tests reach the exact expression that output
 *  path uses. A 14-bit pitch-bend value must reach the wire as an LSB then an MSB, 7 bits
 *  each - the split the WinMM input path reverses.
 *
 *  Delivery (cluster new-p9-276): MidiWinMM is a thin WinMM handle adapter around
 *  MidiWinMMOutput, which owns the device list, the subscriptions, the published routing and
 *  the bounded queue. These tests drive that same class with a controllable sink, so they
 *  exercise production publication, subscription and playback rather than a copy of it. The
 *  only difference from production is who pumps the queue (the test, or the output's worker).
 */

#include <QtTest>

#include <QMap>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <semaphore>
#include <thread>
#include <utility>
#include <vector>

#include "AllocationProbe.h"
#include "MidiEvent.h"
#include "MidiPort.h"
#include "MidiWinMMShortMessage.h"

#if __has_include("MidiWinMMOutput.h")
#include "MidiWinMMOutput.h"
#endif

using lmms::MidiEvent;
using lmms::winmmShortMessage;

namespace
{

//! Byte of a packed short message: 0 = status, 1 and 2 = the two 7-bit data bytes (for pitch bend: low, high).
std::uint32_t byteAt(std::uint32_t msg, unsigned index)
{
	return (msg >> (8 * index)) & 0xFFu;
}

//! Builds an event through the (type, channel, param0, param1) constructor. A literal 0 in the
//! channel slot also matches the SysEx constructor's const char* overload, so the types are fixed here.
MidiEvent eventOf(lmms::MidiEventTypes type, int channel, int p0, int p1 = 0)
{
	return MidiEvent(type, static_cast<std::int8_t>(channel), static_cast<std::int16_t>(p0),
		static_cast<std::int16_t>(p1));
}

#ifdef ZENE_HAVE_WINMM_BOUNDED_OUTPUT

//! Distinct, never-dereferenced port identities: the output path only compares a port pointer.
const lmms::MidiPort* portId(unsigned n)
{
	return reinterpret_cast<const lmms::MidiPort*>(static_cast<std::uintptr_t>(0x7E000000u)
		+ static_cast<std::uintptr_t>(n) * 0x100u);
}

struct Sent
{
	std::uintptr_t device = 0;
	std::uint32_t message = 0;

	bool operator==(const Sent&) const = default;
};

//! Records what reached the "driver", in delivery order.
class RecordingSink final : public lmms::MidiWinMMSink
{
public:
	void send(std::uintptr_t device, std::uint32_t message) override
	{
		sent.push_back(Sent{device, message});
	}

	std::vector<Sent> sent;
};

//! Records one send and releases a semaphore, so the test can wait without a sleep.
class SignallingSink final : public lmms::MidiWinMMSink
{
public:
	void send(std::uintptr_t device, std::uint32_t message) override
	{
		sent.push_back(Sent{device, message});
		thread = std::this_thread::get_id();
		arrived.release();
	}

	std::vector<Sent> sent;
	std::binary_semaphore arrived{0};
	std::thread::id thread;
};

//! Holds the worker inside a driver call until the test releases it.
class GatedSink final : public lmms::MidiWinMMSink
{
public:
	void send(std::uintptr_t device, std::uint32_t message) override
	{
		sent.push_back(Sent{device, message});
		entered.release();
		release.acquire();
		finished.store(true, std::memory_order_release);
	}

	std::vector<Sent> sent;
	std::binary_semaphore entered{0};
	std::binary_semaphore release{0};
	std::atomic<bool> finished{false};
};

//! @return whether @a sink received exactly @a expected, in order.
bool received(const RecordingSink& sink, const std::vector<Sent>& expected)
{
	return sink.sent == expected;
}

QString describe(const std::vector<Sent>& sent)
{
	QStringList parts;
	for (const Sent& s : sent)
	{
		parts << QStringLiteral("0x%1:0x%2")
			.arg(s.device, 0, 16).arg(s.message, 0, 16);
	}
	return parts.join(QStringLiteral(", "));
}

#endif // ZENE_HAVE_WINMM_BOUNDED_OUTPUT

} // namespace

class MidiWinMMShortMessageTest : public QObject
{
	Q_OBJECT

private slots:
	//! The centre of the bend range is 8192: LSB 0x00, MSB 0x40.
	void centredBendIsCentre()
	{
		const std::uint32_t msg = winmmShortMessage(eventOf(lmms::MidiPitchBend, 0, 8192));
		QCOMPARE(byteAt(msg, 0), 0xE0u);
		QCOMPARE(byteAt(msg, 1), 0x00u);
		QCOMPARE(byteAt(msg, 2), 0x40u);
	}

	//! Whole-message equality at the range edges and at a mid-range value.
	void bendLowAndHighBytesForKnownValues()
	{
		struct Case
		{
			int value;
			std::uint32_t lsb;
			std::uint32_t msb;
		};
		static const Case cases[] = {
			{0, 0x00u, 0x00u},
			{8191, 0x7Fu, 0x3Fu},
			{8193, 0x01u, 0x40u},
			{16383, 0x7Fu, 0x7Fu},
			{12345, 0x39u, 0x60u},
		};
		for (const Case& c : cases)
		{
			const std::uint32_t msg = winmmShortMessage(eventOf(lmms::MidiPitchBend, 0, c.value));
			QCOMPARE(msg, 0xE0u | (c.lsb << 8) | (c.msb << 16));
		}
	}

	//! Every bend value survives the trip through the bytes the WinMM input path reads back.
	void everyBendRoundTripsThroughTheInputContract()
	{
		for (int v = 0; v <= 16383; ++v)
		{
			const std::uint32_t msg = winmmShortMessage(eventOf(lmms::MidiPitchBend, 0, v));
			const int data1 = static_cast<int>(byteAt(msg, 1));
			const int data2 = static_cast<int>(byteAt(msg, 2));
			QCOMPARE(data1, v & 0x7F);
			QCOMPARE(data2, (v >> 7) & 0x7F);
			QCOMPARE(data1 + data2 * 128, v);
		}
	}

	//! The bend branch must not disturb the generic path: other messages keep their data bytes.
	void otherChannelMessagesKeepTheirDataBytes()
	{
		QCOMPARE(winmmShortMessage(eventOf(lmms::MidiNoteOn, 3, 60, 100)), 0x00643C93u);
		QCOMPARE(winmmShortMessage(eventOf(lmms::MidiControlChange, 5, 1, 127)), 0x7F01B5u);
		QCOMPARE(winmmShortMessage(eventOf(lmms::MidiChannelPressure, 2, 64)), 0x000040D2u);
	}

	//! The channel nibble goes into the status byte and the bend data is unchanged.
	void bendKeepsStatusAndChannel()
	{
		const std::uint32_t msg = winmmShortMessage(eventOf(lmms::MidiPitchBend, 15, 8192));
		QCOMPARE(byteAt(msg, 0), 0xEFu);
		QCOMPARE(byteAt(msg, 2), 0x40u);
	}

	/*! Normal path: the production subscription and playback methods resolve an event to every
	 *  subscribed device, in device-map order, with the name-search multiplicity. */
	void publishedRoutingDrivesTheQueueToTheSubscribedDevices()
	{
#ifdef ZENE_HAVE_WINMM_BOUNDED_OUTPUT
		RecordingSink sink;
		lmms::MidiWinMMOutput output(sink, false);
		output.setDevices({
			{QStringLiteral("Synth:out"), 0x11u},
			{QStringLiteral("Other:out"), 0x22u},
			{QStringLiteral("Drums:out"), 0x33u},
			{QStringLiteral("Drums:out"), 0x44u},
		});
		output.subscribeWritablePort(portId(1), QStringLiteral("Synth:out"), true);
		output.subscribeWritablePort(portId(2), QStringLiteral("Other:out"), true);
		output.subscribeWritablePort(portId(3), QStringLiteral("Drums:out"), true);
		output.subscribeWritablePort(portId(3), QStringLiteral("Drums:out"), true); // duplicate collapses
		output.subscribeWritablePort(portId(4), QStringLiteral("Drums:out"), true);
		output.subscribeWritablePort(portId(5), QStringLiteral("Gone:out"), true); // no open device
		output.subscribeWritablePort(portId(6), QStringLiteral("Synth:out"), true);
		output.subscribeWritablePort(portId(6), QStringLiteral("Other:out"), true);

		output.processOutEvent(portId(1), 0x1001u);
		output.processOutEvent(portId(2), 0x1002u);
		output.processOutEvent(portId(3), 0x1003u);
		output.processOutEvent(portId(3), 0x1004u); // a duplicate subscription still sends once per device
		output.processOutEvent(portId(4), 0x1005u);
		output.processOutEvent(portId(5), 0x1006u); // name with no open device
		output.processOutEvent(portId(9), 0x1007u); // no subscription at all
		output.processOutEvent(portId(6), 0x1008u);
		QCOMPARE(output.drainForTesting(), std::size_t{10});

		const std::vector<Sent> expected{
			{0x11u, 0x1001u},
			{0x22u, 0x1002u},
			{0x33u, 0x1003u},
			{0x44u, 0x1003u},
			{0x33u, 0x1004u},
			{0x44u, 0x1004u},
			{0x33u, 0x1005u},
			{0x44u, 0x1005u},
			{0x11u, 0x1008u},
			{0x22u, 0x1008u},
		};
		QVERIFY2(received(sink, expected), qPrintable(describe(sink.sent)));
		QCOMPARE(output.overflowCount(), std::uint64_t{0});
#else
		QFAIL("include/MidiWinMMOutput.h is missing: the WinMM output path has no bounded delivery");
#endif
	}

	//! Reconfiguration: unsubscribe, remove, port-mode and re-subscribe all take effect at once.
	void subscriptionChangesTakeEffectImmediately()
	{
#ifdef ZENE_HAVE_WINMM_BOUNDED_OUTPUT
		RecordingSink sink;
		lmms::MidiWinMMOutput output(sink, false);
		output.setDevices({
			{QStringLiteral("A:out"), 0x11u},
			{QStringLiteral("B:out"), 0x22u},
		});
		const lmms::MidiPort* port = portId(1);

		output.subscribeWritablePort(port, QStringLiteral("A:out"), true);
		output.processOutEvent(port, 1u);
		QCOMPARE(output.drainForTesting(), std::size_t{1});
		QVERIFY(received(sink, {{0x11u, 1u}}));

		output.subscribeWritablePort(port, QStringLiteral("A:out"), false);
		sink.sent.clear();
		output.processOutEvent(port, 2u);
		QCOMPARE(output.drainForTesting(), std::size_t{0});
		QVERIFY(received(sink, {}));

		output.subscribeWritablePort(port, QStringLiteral("B:out"), true);
		output.processOutEvent(port, 3u);
		QCOMPARE(output.drainForTesting(), std::size_t{1});
		QVERIFY(received(sink, {{0x22u, 3u}}));

		sink.sent.clear();
		output.subscribeWritablePort(port, QStringLiteral("A:out"), true);
		output.processOutEvent(port, 4u);
		QCOMPARE(output.drainForTesting(), std::size_t{2});
		QVERIFY(received(sink, {{0x11u, 4u}, {0x22u, 4u}}));

		sink.sent.clear();
		output.removePort(port);
		output.processOutEvent(port, 5u);
		QCOMPARE(output.drainForTesting(), std::size_t{0});
		QVERIFY(received(sink, {}));

		output.subscribeWritablePort(port, QStringLiteral("A:out"), true);
		output.applyPortMode(port, false);
		output.processOutEvent(port, 6u);
		QCOMPARE(output.drainForTesting(), std::size_t{0});
		QVERIFY(received(sink, {}));

		output.applyPortMode(port, true);
		output.processOutEvent(port, 7u);
		QCOMPARE(output.drainForTesting(), std::size_t{0});
		QVERIFY(received(sink, {}));
#else
		QFAIL("include/MidiWinMMOutput.h is missing: the WinMM output path has no bounded delivery");
#endif
	}

	//! Exhaustion: a full queue drops the newest commands, counts them and never blocks.
	void queueFullDropsNewestAndCounts()
	{
#ifdef ZENE_HAVE_WINMM_BOUNDED_OUTPUT
		RecordingSink sink;
		lmms::MidiWinMMOutput output(sink, false);
		output.setDevices({{QStringLiteral("A:out"), 0x11u}});
		const lmms::MidiPort* port = portId(1);
		output.subscribeWritablePort(port, QStringLiteral("A:out"), true);

		const std::size_t capacity = lmms::MidiWinMMOutput::QueueCapacity;
		for (std::size_t i = 0; i < capacity + 3; ++i)
		{
			output.processOutEvent(port, static_cast<std::uint32_t>(i));
		}
		QCOMPARE(output.overflowCount(), std::uint64_t{3});
		QCOMPARE(output.drainForTesting(), capacity);
		QCOMPARE(sink.sent.size(), capacity);
		for (std::size_t i = 0; i < capacity; ++i)
		{
			QCOMPARE(sink.sent[i].device, std::uintptr_t{0x11u});
			QCOMPARE(sink.sent[i].message, static_cast<std::uint32_t>(i));
		}

		output.processOutEvent(port, 0xABCDu);
		QCOMPARE(output.overflowCount(), std::uint64_t{3});
		QCOMPARE(output.drainForTesting(), std::size_t{1});
		QCOMPARE(sink.sent.back().message, 0xABCDu);
#else
		QFAIL("include/MidiWinMMOutput.h is missing: the WinMM output path has no bounded queue");
#endif
	}

	/*! The audio-thread contract: submission performs no allocation or free. The probe is first
	 *  shown to see an ordinary allocation, so a zero count below is not a blind probe. */
	void submissionIsAllocationFree()
	{
#ifdef ZENE_HAVE_WINMM_BOUNDED_OUTPUT
		RecordingSink sink;
		lmms::MidiWinMMOutput output(sink, false);
		output.setDevices({{QStringLiteral("A:out"), 0x11u}});
		const lmms::MidiPort* port = portId(1);
		output.subscribeWritablePort(port, QStringLiteral("A:out"), true);

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		std::vector<unsigned char> control;
		for (unsigned i = 0; i < 64; ++i)
		{
			control.push_back(static_cast<unsigned char>(i));
		}
		lmms::test::tlCountAllocations = false;
		QCOMPARE(control.size(), std::size_t{64});
		QVERIFY2(static_cast<quint64>(lmms::test::tlAllocationCount) > 0,
			"the allocation probe saw nothing: the criterion below would measure nothing");

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (std::uint32_t i = 0; i < 1000; ++i)
		{
			output.processOutEvent(port, i);
		}
		lmms::test::tlCountAllocations = false;
		QCOMPARE(static_cast<quint64>(lmms::test::tlAllocationCount), quint64{0});
		QCOMPARE(static_cast<quint64>(lmms::test::tlFreeCount), quint64{0});

		QCOMPARE(output.drainForTesting(), std::size_t{1000});
		QCOMPARE(sink.sent.size(), std::size_t{1000});
#else
		QFAIL("include/MidiWinMMOutput.h is missing: the WinMM output path has no bounded queue");
#endif
	}

	//! The driver call runs on the output worker, not on the thread that submitted the event.
	void workerSendsOffTheSubmittingThread()
	{
#ifdef ZENE_HAVE_WINMM_BOUNDED_OUTPUT
		SignallingSink sink;
		lmms::MidiWinMMOutput output(sink, true);
		output.setDevices({{QStringLiteral("A:out"), 0x11u}});
		const lmms::MidiPort* port = portId(1);
		output.subscribeWritablePort(port, QStringLiteral("A:out"), true);

		const std::thread::id submitting = std::this_thread::get_id();
		output.processOutEvent(port, 0x903C64u);
		sink.arrived.acquire();

		QCOMPARE(sink.sent.size(), std::size_t{1});
		QCOMPARE(sink.sent[0].device, std::uintptr_t{0x11u});
		QCOMPARE(sink.sent[0].message, 0x903C64u);
		QVERIFY2(sink.thread != submitting,
			"the WinMM sink ran on the submitting (audio) thread");
#else
		QFAIL("include/MidiWinMMOutput.h is missing: the WinMM output path delivers synchronously");
#endif
	}

	//! Teardown: queued commands are discarded, new submissions are refused, and reopening works.
	void clearDevicesDiscardsQueuedCommandsAndStopsSends()
	{
#ifdef ZENE_HAVE_WINMM_BOUNDED_OUTPUT
		RecordingSink sink;
		lmms::MidiWinMMOutput output(sink, false);
		output.setDevices({{QStringLiteral("A:out"), 0x11u}});
		const lmms::MidiPort* port = portId(1);
		output.subscribeWritablePort(port, QStringLiteral("A:out"), true);

		for (std::uint32_t i = 0; i < 3; ++i)
		{
			output.processOutEvent(port, i);
		}
		output.clearDevices();
		QCOMPARE(output.drainForTesting(), std::size_t{3});
		QVERIFY2(sink.sent.empty(), "a command queued before teardown reached a closed device");

		output.processOutEvent(port, 9u);
		QCOMPARE(output.drainForTesting(), std::size_t{0});
		QVERIFY(sink.sent.empty());

		output.setDevices({{QStringLiteral("A:out"), 0x11u}});
		output.subscribeWritablePort(port, QStringLiteral("A:out"), true);
		output.processOutEvent(port, 10u);
		QCOMPARE(output.drainForTesting(), std::size_t{1});
		QVERIFY(received(sink, {{0x11u, 10u}}));
#else
		QFAIL("include/MidiWinMMOutput.h is missing: the WinMM output path delivers synchronously");
#endif
	}

	//! Teardown must wait for a sink call already inside the driver before closing handles.
	void clearDevicesWaitsForInFlightSink()
	{
#ifdef ZENE_HAVE_WINMM_BOUNDED_OUTPUT
		GatedSink sink;
		lmms::MidiWinMMOutput output(sink, true);
		output.setDevices({{QStringLiteral("A:out"), 0x11u}});
		const lmms::MidiPort* port = portId(1);
		output.subscribeWritablePort(port, QStringLiteral("A:out"), true);

		output.processOutEvent(port, 0x1234u);
		sink.entered.acquire(); // the worker is now inside the gated driver call

		std::atomic<bool> clearReturned{false};
		std::thread teardown([&output, &clearReturned]
		{
			output.clearDevices();
			clearReturned.store(true, std::memory_order_release);
		});

		while (!output.teardownWaitedForInFlightForTesting())
		{
			std::this_thread::yield();
		}
		QVERIFY2(!clearReturned.load(std::memory_order_acquire),
			"clearDevices() returned while a sink call was still inside the driver");

		sink.release.release();
		teardown.join();
		QVERIFY(clearReturned.load(std::memory_order_acquire));
		QVERIFY(sink.finished.load(std::memory_order_acquire));
		QCOMPARE(sink.sent.size(), std::size_t{1});
		QCOMPARE(sink.sent[0].message, 0x1234u);
#else
		QFAIL("include/MidiWinMMOutput.h is missing: the WinMM output path delivers synchronously");
#endif
	}
};

QTEST_GUILESS_MAIN(MidiWinMMShortMessageTest)
#include "MidiWinMMShortMessageTest.moc"
