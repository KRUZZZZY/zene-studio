/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

/*! WinMM MIDI output path: short-message packing and output routing.
 *
 *  Packing: MidiWinMM::processOutEvent sends winmmShortMessage( event ); the WinMM callback
 *  cannot be driven on this platform, so these tests reach the exact expression that output
 *  path uses. A 14-bit pitch-bend value must reach the wire as an LSB then an MSB, 7 bits
 *  each - the split the WinMM input path reverses.
 *
 *  Routing (cluster new-p9-276): the output path must not build the destination list per
 *  event. It publishes a precomputed snapshot when the subscriptions or the device list
 *  change, and reads that on the playback path - one atomic load and a scan over a POD
 *  table, no QStringList, no allocation. The WinMM translation unit is empty on this
 *  platform (LMMS_HAVE_WINMM is off in lmmsconfig.h), so these tests exercise the
 *  platform-neutral routing header the output path calls, and assert on the product source
 *  that the output path still goes through it.
 */

#include <QtTest>

#include <QFile>
#include <QMap>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <utility>
#include <vector>

#include "AllocationProbe.h"
#include "MidiEvent.h"
#include "MidiPort.h"
#include "MidiWinMMShortMessage.h"

#if __has_include("MidiWinMMOutRouting.h")
#  define ZENE_HAVE_WINMM_OUT_ROUTING 1
#endif

#ifdef ZENE_HAVE_WINMM_OUT_ROUTING
#include "MidiWinMMOutRouting.h"
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

//! Distinct, never-dereferenced port identities. The routing only compares a port pointer - it
//! never calls through one - so a routing test needs no live MIDI client or GUI machinery to key
//! its table.
[[maybe_unused]] const lmms::MidiPort* portId(unsigned n)
{
	return reinterpret_cast<const lmms::MidiPort*>(static_cast<std::uintptr_t>(0x7E000000u)
		+ static_cast<std::uintptr_t>(n) * 0x100u);
}

//! The client's own state in its own shapes: handle -> device name (QMap order is the order the
//! output path sends in) and device name -> subscribed ports.
struct RoutingScenario
{
	QMap<std::uintptr_t, QString> devices;
	QMap<QString, lmms::MidiPortList> subscriptions;

	void addDevice(std::uintptr_t handle, const QString& name) { devices.insert(handle, name); }
	void unsubscribe(const QString& name, const lmms::MidiPort* port)
	{
		subscriptions[name].removeAll(const_cast<lmms::MidiPort*>(port));
	}
	void subscribe(const QString& name, const lmms::MidiPort* port)
	{
		// The client's subscribeWritablePort() removes an earlier entry first, so a port appears once
		// per name; a scenario may still list a port twice to pin what the name search did with it.
		subscriptions[name].push_back(const_cast<lmms::MidiPort*>(port));
	}
};

/*! The pre-fix algorithm of MidiWinMM::processOutEvent(), kept in shape as the reference the
 *  published snapshot has to reproduce: for every event, collect the destination NAMES
 *  subscribed to @a port (one entry per name, however often the port is listed), then send to
 *  every open device whose name is in that list, in device-map order. */
[[maybe_unused]] std::vector<std::uintptr_t> nameSearchDevices(const RoutingScenario& scenario, const lmms::MidiPort* port)
{

	QStringList outDevs;
	for (auto it = scenario.subscriptions.begin(); it != scenario.subscriptions.end(); ++it)
	{
		for (const lmms::MidiPort* subscribed : it.value())
		{
			if (subscribed == port)
			{
				outDevs += it.key();
				break;
			}
		}
	}

	std::vector<std::uintptr_t> sent;
	for (auto it = scenario.devices.begin(); it != scenario.devices.end(); ++it)
	{
		if (outDevs.contains(it.value())) { sent.push_back(it.key()); }
	}
	return sent;
}

#ifdef ZENE_HAVE_WINMM_OUT_ROUTING

//! The stop list MidiWinMM::refreshOutputRouting() publishes for @a scenario, through the
//! same buildRoutes() call it makes out of m_outputDevices and m_outputSubs.
std::vector<lmms::MidiWinMMOutRoute> routesFor(const RoutingScenario& scenario)
{
	std::vector<std::pair<QString, std::uintptr_t>> devices;
	devices.reserve(static_cast<std::size_t>(scenario.devices.size()));
	for (auto it = scenario.devices.begin(); it != scenario.devices.end(); ++it)
	{
		devices.emplace_back(it.value(), it.key());
	}

	QMap<QString, lmms::MidiPortList> subscriptions;
	for (auto it = scenario.subscriptions.begin(); it != scenario.subscriptions.end(); ++it)
	{
		lmms::MidiPortList ports;
		for (const lmms::MidiPort* port : it.value())
		{
			ports.push_back(const_cast<lmms::MidiPort*>(port));
		}
		subscriptions.insert(it.key(), ports);
	}
	return lmms::MidiWinMMOutRouting::buildRoutes(devices, subscriptions);
}

//! The devices the published snapshot sends @a port's events to, in the order it sends them.
std::vector<std::uintptr_t> snapshotDevices(const lmms::MidiWinMMOutRouting& routing, const lmms::MidiPort* port)
{
	std::vector<std::uintptr_t> sent;
	routing.forEachDevice(port, [&sent](const lmms::MidiWinMMOutRoute& route) { sent.push_back(route.device); });
	return sent;
}

#endif // ZENE_HAVE_WINMM_OUT_ROUTING

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
		// Status is type | channel; byte 1 and byte 2 are the two 7-bit data bytes (param(0), param(1) for non-bend messages).
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

	//! The output path must keep calling the shared packer, so the helper is not dead code.
	void winmmOutputPathUsesTheSharedPacker()
	{
		QFile source(QStringLiteral(ZENE_SOURCE_DIR "/src/core/midi/MidiWinMM.cpp"));
		const bool opened = source.open(QIODevice::ReadOnly | QIODevice::Text);
		QVERIFY(opened);
		QVERIFY(source.readAll().contains("winmmShortMessage( event )"));
	}

	/*! The output path publishes its routing instead of rebuilding it for every event (cluster
	 *  new-p9-276). The WinMM translation unit is empty here, so the contract is read from the
	 *  product source: no per-event QStringList, the published snapshot, and a republish at every
	 *  place that edits the subscriptions or the device list. */
	void outputPathRoutesThroughThePublishedSnapshot()
	{
		QFile source(QStringLiteral(ZENE_SOURCE_DIR "/src/core/midi/MidiWinMM.cpp"));
		QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
		const QString product = QString::fromUtf8(source.readAll());

		QVERIFY2(!product.contains(QStringLiteral("QStringList outDevs")),
			"MidiWinMM::processOutEvent() still builds a QStringList of destination devices per event");
		QVERIFY2(!product.contains(QStringLiteral("outDevs.contains")),
			"MidiWinMM::processOutEvent() still searches that per-event QStringList once per device");
		QVERIFY2(product.contains(QStringLiteral("m_outputRouting.forEachDevice")),
			"MidiWinMM::processOutEvent() does not send through the published routing snapshot");
		// The definition plus one republish at every edit site: subscribeWritablePort, applyPortMode,
		// removePort, closeDevices and openDevices.
		const int republishes = product.count(QStringLiteral("refreshOutputRouting()"));
		QVERIFY2(republishes >= 6,
			qPrintable(QStringLiteral("only %1 refreshOutputRouting() mentions: a subscription or device "
				"edit does not republish the routing").arg(republishes)));
	}

	//! The published snapshot must route exactly like the per-event name search it replaced.
	void routingMatchesTheNameSearchItReplaced()
	{
#ifdef ZENE_HAVE_WINMM_OUT_ROUTING
		RoutingScenario scenario;
		scenario.addDevice(0x11u, QStringLiteral("Synth:out"));
		scenario.addDevice(0x22u, QStringLiteral("Other:out"));
		// Two open devices share a name: the name search sent to both, and so must the snapshot.
		scenario.addDevice(0x33u, QStringLiteral("Drums:out"));
		scenario.addDevice(0x44u, QStringLiteral("Drums:out"));
		scenario.subscribe(QStringLiteral("Synth:out"), portId(1));
		scenario.subscribe(QStringLiteral("Other:out"), portId(2));
		scenario.subscribe(QStringLiteral("Drums:out"), portId(3));
		scenario.subscribe(QStringLiteral("Drums:out"), portId(4));
		// A port listed twice for one name still gets one send per device (the search stopped at the
		// first match), and a name with no open device routes nowhere.
		scenario.subscribe(QStringLiteral("Drums:out"), portId(3));
		scenario.subscribe(QStringLiteral("Gone:out"), portId(5));

		lmms::MidiWinMMOutRouting routing;
		routing.publish(routesFor(scenario));

		for (unsigned p = 0; p < 6; ++p)
		{
			QVERIFY2(snapshotDevices(routing, portId(p)) == nameSearchDevices(scenario, portId(p)),
				qPrintable(QStringLiteral("port %1 is routed differently from the name search").arg(p)));
		}
		// Unsubscribed port: nothing at all.
		QVERIFY(snapshotDevices(routing, portId(9)).empty());
#else
		QFAIL("include/MidiWinMMOutRouting.h is missing: the WinMM output path has no precomputed "
			"routing snapshot, so processOutEvent() still builds a QStringList per event");
#endif
	}

	//! Reconfiguration: every publish replaces what the output path reads, and no publish means no sends.
	void routingFollowsReconfiguration()
	{
#ifdef ZENE_HAVE_WINMM_OUT_ROUTING
		RoutingScenario scenario;
		scenario.addDevice(0x11u, QStringLiteral("Synth:out"));
		scenario.subscribe(QStringLiteral("Synth:out"), portId(1));

		lmms::MidiWinMMOutRouting routing;
		// Before the first publish the output path sends nothing and reads no table.
		QVERIFY(routing.snapshot() == nullptr);
		QVERIFY(snapshotDevices(routing, portId(1)).empty());

		routing.publish(routesFor(scenario));
		QVERIFY2(routing.snapshot() != nullptr, "the first publish published no snapshot");
		QCOMPARE(snapshotDevices(routing, portId(1)).size(), std::size_t{1});

		// Unsubscribing republishes: the port reaches no device.
		scenario.unsubscribe(QStringLiteral("Synth:out"), portId(1));
		routing.publish(routesFor(scenario));
		QVERIFY(snapshotDevices(routing, portId(1)).empty());

		// Subscribing again plus a second device of the same name: both, in device-map order.
		scenario.subscribe(QStringLiteral("Synth:out"), portId(1));
		scenario.addDevice(0x55u, QStringLiteral("Synth:out"));
		routing.publish(routesFor(scenario));
		const std::vector<std::uintptr_t> expected{0x11u, 0x55u};
		QVERIFY(snapshotDevices(routing, portId(1)) == expected);

		// A closeDevices()/openDevices() refresh drops the devices: the routing empties again.
		scenario.devices.clear();
		routing.publish(routesFor(scenario));
		QVERIFY(snapshotDevices(routing, portId(1)).empty());

		// Republishing identical routing is a no-op: the audio thread's snapshot pointer is kept.
		scenario.addDevice(0x11u, QStringLiteral("Synth:out"));
		routing.publish(routesFor(scenario));
		const void* published = routing.snapshot();
		routing.publish(routesFor(scenario));
		QCOMPARE(static_cast<const void*>(routing.snapshot()), published);
#else
		QFAIL("include/MidiWinMMOutRouting.h is missing: the WinMM output path has no precomputed "
			"routing snapshot to reconfigure");
#endif
	}

	/*! The playback path allocates nothing. The pre-fix per-event search is measured first as the
	 *  control: the probe sees it allocate, so a zero count below means the snapshot really is
	 *  allocation-free and not that the probe was blind. */
	void outputPathDoesNotAllocate()
	{
#ifdef ZENE_HAVE_WINMM_OUT_ROUTING
		RoutingScenario scenario;
		scenario.addDevice(0x11u, QStringLiteral("Synth:out"));
		scenario.addDevice(0x22u, QStringLiteral("Other:out"));
		scenario.subscribe(QStringLiteral("Synth:out"), portId(1));
		scenario.subscribe(QStringLiteral("Other:out"), portId(1));
		scenario.subscribe(QStringLiteral("Other:out"), portId(2));

		lmms::MidiWinMMOutRouting routing;
		routing.publish(routesFor(scenario));

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int i = 0; i < 1000; ++i)
		{
			const std::vector<std::uintptr_t> devices = nameSearchDevices(scenario, portId(1));
			if (devices.empty()) { QFAIL("the control routed to no device"); }
		}
		lmms::test::tlCountAllocations = false;
		const quint64 controlAllocations = static_cast<quint64>(lmms::test::tlAllocationCount);
		QVERIFY2(controlAllocations > 0,
			"the probe did not see the pre-fix per-event search allocate: this criterion measures nothing");

		unsigned sends = 0;
		std::uintptr_t lastDevice = 0;
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int i = 0; i < 1000; ++i)
		{
			routing.forEachDevice(portId(1), [&sends, &lastDevice](const lmms::MidiWinMMOutRoute& route)
			{
				lastDevice = route.device;
				++sends;
			});
		}
		lmms::test::tlCountAllocations = false;
		const quint64 allocations = static_cast<quint64>(lmms::test::tlAllocationCount);
		const quint64 frees = static_cast<quint64>(lmms::test::tlFreeCount);
		QVERIFY2(allocations == 0 && frees == 0,
			qPrintable(QStringLiteral("the output path allocated %1 and freed %2 times over 1000 events")
				.arg(allocations).arg(frees)));
		// Two devices carry port 1, so a thousand events reach two stops each.
		QCOMPARE(sends, 2000u);
		QCOMPARE(lastDevice, std::uintptr_t{0x22u});
#else
		QFAIL("include/MidiWinMMOutRouting.h is missing: the WinMM output path has no precomputed "
			"routing snapshot, so its playback path allocates per event");
#endif
	}

	//! Bounds: a full device list and many ports still route without a single allocation.
	void routingStaysAllocationFreeAtScale()
	{
#ifdef ZENE_HAVE_WINMM_OUT_ROUTING
		RoutingScenario scenario;
		for (unsigned d = 0; d < 32; ++d)
		{
			scenario.addDevice(0x100u + d, QStringLiteral("Synth:out"));
		}
		for (unsigned p = 0; p < 16; ++p)
		{
			scenario.subscribe(QStringLiteral("Synth:out"), portId(p));
		}
		scenario.subscribe(QStringLiteral("Synth:out"), portId(0));

		lmms::MidiWinMMOutRouting routing;
		routing.publish(routesFor(scenario));

		unsigned sends = 0;
		for (unsigned p = 0; p < 16; ++p)
		{
			QCOMPARE(snapshotDevices(routing, portId(p)).size(), std::size_t{32});
		}
		// 16 ports x 32 devices, minus the duplicate the name search collapsed: 512 stops.
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (unsigned p = 0; p < 16; ++p)
		{
			routing.forEachDevice(portId(p), [&sends](const lmms::MidiWinMMOutRoute&) { ++sends; });
		}
		lmms::test::tlCountAllocations = false;
		QCOMPARE(sends, 512u);
		QCOMPARE(static_cast<quint64>(lmms::test::tlAllocationCount), quint64{0});
		QCOMPARE(static_cast<quint64>(lmms::test::tlFreeCount), quint64{0});
#else
		QFAIL("include/MidiWinMMOutRouting.h is missing: the WinMM output path has no precomputed "
			"routing snapshot to route a full device list");
#endif
	}
};

QTEST_GUILESS_MAIN(MidiWinMMShortMessageTest)
#include "MidiWinMMShortMessageTest.moc"
