/*
 * MidiReconnectTest.cpp - the engine half of MIDI controller auto-reconnection
 *                         (0.3.0 feature-list row 18, OWNER-31 item 7): an
 *                         assignment is remembered by IDENTITY, a loss is
 *                         counted once, and a device that comes back at a NEW
 *                         sequencer address is re-attached.
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
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <cstddef>

#include <QString>
#include <QStringList>

#include "MidiClient.h"
#include "MidiEvent.h"
#include "MidiEventProcessor.h"
#include "MidiPort.h"
#include "MidiReconnect.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

//! A MIDI client that stands in for the outside world: it owns the port list a
//! real client would poll, and counts the subscribe calls the engine makes on
//! it - which is how "the engine re-attached" is measured without a device.
class FakeMidiClient : public MidiClient
{
public:
	void processOutEvent(const MidiEvent&, const TimePos&, const MidiPort*) override {}

	QStringList readablePorts() const override { return m_live; }

	//! The subscriptions the engine has asked this client for, in order.
	int subscribeCalls() const { return m_subscribeCalls; }

	QStringList& live() { return m_live; }

	void subscribeReadablePort(MidiPort* port, const QString& dest, bool subscribe) override
	{
		++m_subscribeCalls;
		m_lastSubscribed = dest;
		MidiClient::subscribeReadablePort(port, dest, subscribe);
	}

	QString lastSubscribed() const { return m_lastSubscribed; }

private:
	QStringList m_live;
	int m_subscribeCalls = 0;
	QString m_lastSubscribed;
};

//! A client that DOES publish port-list changes - the shape MidiAlsaSeq has.
class PollingMidiClient : public FakeMidiClient
{
public:
	bool noticesPortChanges() const override { return true; }
};

class FakeProcessor : public MidiEventProcessor
{
public:
	void processInEvent(const MidiEvent&, const TimePos&, f_cnt_t) override {}
	void processOutEvent(const MidiEvent&, const TimePos&, f_cnt_t) override {}
};

//! A port name the way the ALSA-sequencer client renders one: the volatile
//! address, a space, then "<client name>:<port name>".
QString alsaName(int client, int port, const QString& clientName, const QString& portName)
{
	return QStringLiteral("%1:%2 %3:%4").arg(client).arg(port).arg(clientName).arg(portName);
}

const QString kProbe = QStringLiteral("Zene Probe Test:controller");

} // namespace


class MidiReconnectTest : public QObject
{
	Q_OBJECT

private slots:

	//! The identity is the NAME half, and it is the SAME for two different
	//! addresses. This is the whole premise: the number a sequencer client gets
	//! is not part of what a user bound.
	void theIdentityIgnoresTheVolatileAddress()
	{
		const QString first = alsaName(128, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		const QString second = alsaName(131, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));

		QCOMPARE(midiPortIdentity(first), kProbe);
		QCOMPARE(midiPortIdentity(second), kProbe);
		QCOMPARE(midiPortAddress(first), QStringLiteral("128:0"));
		QCOMPARE(midiPortAddress(second), QStringLiteral("131:0"));

		// A name with no address is its own identity - never the empty string,
		// which would make every such name match every other.
		QCOMPARE(midiPortIdentity(QStringLiteral("Some Raw Port")),
			QStringLiteral("Some Raw Port"));
		QCOMPARE(midiPortAddress(QStringLiteral("Some Raw Port")), QString());
		QVERIFY(midiPortIdentity(QString()).isEmpty());

		QCOMPARE(midiPortWithAddress(first, QStringLiteral("131:0")), second);
	}

	//! A device that goes away is marked LOST once and stays in the memory: the
	//! loss is counted on the transition, not on every poll.
	void aLossIsCountedOnceAndTheMemoryStays()
	{
		FakeMidiClient client;
		client.live() << alsaName(128, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		FakeProcessor processor;
		MidiPort port(QStringLiteral("Default"), &client, &processor, nullptr,
			MidiPort::Mode::Input);

		const QString name = client.live().first();
		port.subscribeReadablePort(name);
		QCOMPARE(client.reconnect().assignments().size(), std::size_t(1));
		QCOMPARE(client.reconnect().reconcile(client.readablePorts(), QStringList()), 0);
		QCOMPARE(client.reconnect().liveCount(), 1);
		QCOMPARE(client.reconnect().lost(), 0);

		client.live().clear();
		const QStringList empty;
		QCOMPARE(client.reconnect().reconcile(empty, empty), 0);
		QCOMPARE(client.reconnect().lost(), 1);
		QCOMPARE(client.reconnect().liveCount(), 0);
		QCOMPARE(client.reconnect().lostCount(), 1);
		QCOMPARE(client.reconnect().assignments().size(), std::size_t(1));

		// Still absent on the next poll: the loss is NOT counted again.
		QCOMPARE(client.reconnect().reconcile(empty, empty), 0);
		QCOMPARE(client.reconnect().lost(), 1);
	}

	//! THE CLAIM. The device comes back at a NEW address, and the engine's own
	//! subscription moves to it without anyone asking.
	void theDeviceComingBackAtANewAddressIsReattached()
	{
		FakeMidiClient client;
		const QString first = alsaName(128, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		client.live() << first;
		FakeProcessor processor;
		MidiPort port(QStringLiteral("Default"), &client, &processor, nullptr,
			MidiPort::Mode::Input);
		port.subscribeReadablePort(first);

		client.live().clear();
		const QStringList empty;
		client.reconnect().reconcile(empty, empty);

		// The same name, a different client number - what a replug produces.
		const QString second = alsaName(133, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		client.live() << second;
		QCOMPARE(client.reconnect().reconcile(client.live(), QStringList()), 1);
		QCOMPARE(client.reconnect().reconnected(), 1);
		QCOMPARE(client.reconnect().liveCount(), 1);
		QCOMPARE(client.reconnect().lostCount(), 0);
		QCOMPARE(client.lastSubscribed(), second);

		// The engine port holds the NEW name and no longer the old one: the map
		// is what the project serializes as <midiport inports>.
		QVERIFY(port.readablePorts().value(second, false));
		QVERIFY(!port.readablePorts().value(first, false));

		const MidiReconnectAssignment* binding = client.reconnect().assignmentOf(&port, true);
		QVERIFY(binding != nullptr);
		QCOMPARE(binding->reconnects, 1);
		QCOMPARE(binding->identity, kProbe);
		QVERIFY(binding->live);
		QVERIFY(!binding->lost);

		// A second poll with nothing changed must not subscribe again.
		const int calls = client.subscribeCalls();
		QCOMPARE(client.reconnect().reconcile(client.live(), QStringList()), 0);
		QCOMPARE(client.subscribeCalls(), calls);
		QCOMPARE(client.reconnect().reconnected(), 1);
	}

	//! The mode OFF is the whole content of the switch: the loss is still
	//! recorded and reported, and nothing is re-attached. Arming again is what
	//! re-attaches, on the next poll.
	void theModeOffRecordsTheLossWithoutReattaching()
	{
		FakeMidiClient client;
		const QString first = alsaName(128, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		client.live() << first;
		FakeProcessor processor;
		MidiPort port(QStringLiteral("Default"), &client, &processor, nullptr,
			MidiPort::Mode::Input);
		port.subscribeReadablePort(first);
		client.reconnect().reconcile(client.live(), QStringList());

		client.reconnect().setEnabled(false);
		client.live().clear();
		const QStringList empty;
		client.reconnect().reconcile(empty, empty);
		QCOMPARE(client.reconnect().lost(), 1);

		const QString second = alsaName(140, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		client.live() << second;
		const int calls = client.subscribeCalls();
		QCOMPARE(client.reconnect().reconcile(client.live(), QStringList()), 0);
		QCOMPARE(client.subscribeCalls(), calls);
		QCOMPARE(client.reconnect().reconnected(), 0);
		QCOMPARE(client.reconnect().liveCount(), 0);
		QVERIFY(!port.readablePorts().value(second, false));

		client.reconnect().setEnabled(true);
		QCOMPARE(client.reconnect().reconcile(client.live(), QStringList()), 1);
		QCOMPARE(client.reconnect().reconnected(), 1);
		QVERIFY(port.readablePorts().value(second, false));
	}

	//! An explicit unsubscribe is an explicit forget: a controller a user
	//! detached is never silently re-attached when its device returns.
	void anExplicitUnsubscribeIsNotReattached()
	{
		FakeMidiClient client;
		const QString first = alsaName(128, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		client.live() << first;
		FakeProcessor processor;
		MidiPort port(QStringLiteral("Default"), &client, &processor, nullptr,
			MidiPort::Mode::Input);
		port.subscribeReadablePort(first);
		port.subscribeReadablePort(first, false);
		QCOMPARE(client.reconnect().assignments().size(), std::size_t(0));

		client.live().clear();
		const QStringList empty;
		client.reconnect().reconcile(empty, empty);
		client.live() << alsaName(150, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		QCOMPARE(client.reconnect().reconcile(client.live(), QStringList()), 0);
		QCOMPARE(client.reconnect().reconnected(), 0);
		QCOMPARE(client.reconnect().lost(), 0);
	}

	//! Two clients sharing one name (the same controller enumerated twice) are
	//! resolved to the name the assignment last held, and the ambiguity is
	//! REPORTED rather than hidden - never guessed silently.
	void twoClientsSharingOneNameAreReported()
	{
		FakeMidiClient client;
		const QString first = alsaName(128, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		client.live() << first;
		FakeProcessor processor;
		MidiPort port(QStringLiteral("Default"), &client, &processor, nullptr,
			MidiPort::Mode::Input);
		port.subscribeReadablePort(first);
		client.reconnect().reconcile(client.live(), QStringList());
		QCOMPARE(client.reconnect().assignments().front().matches, 1);

		const QString twin = alsaName(129, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		client.live() << twin;
		client.reconnect().reconcile(client.live(), QStringList());
		QCOMPARE(client.reconnect().assignments().front().matches, 2);
		// The name it already held wins, so nothing was re-subscribed.
		QCOMPARE(client.reconnect().assignments().front().name, first);
		QCOMPARE(client.reconnect().reconnected(), 0);
	}

	//! A destroyed port leaves nothing behind: the memory never names a dead
	//! MidiPort.
	void aDestroyedPortIsForgotten()
	{
		FakeMidiClient client;
		client.live() << alsaName(128, 0, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller"));
		FakeProcessor processor;
		{
			MidiPort port(QStringLiteral("Default"), &client, &processor, nullptr,
				MidiPort::Mode::Input);
			port.subscribeReadablePort(client.live().first());
			QCOMPARE(client.reconnect().assignments().size(), std::size_t(1));
		}
		QCOMPARE(client.reconnect().assignments().size(), std::size_t(0));
	}

	//! "Does this backend expose hotplug notice" is the CLIENT CLASS's answer,
	//! so a backend whose changes this build does not consume cannot be reported
	//! as one that does.
	void theNoticeIsTheClientsOwnAnswer()
	{
		FakeMidiClient plain;
		PollingMidiClient polling;
		QVERIFY(!plain.noticesPortChanges());
		QVERIFY(polling.noticesPortChanges());
		QVERIFY(!MidiReconnect::clientNoticesPortChanges(&plain));
		QVERIFY(MidiReconnect::clientNoticesPortChanges(&polling));
		QVERIFY(!MidiReconnect::clientNoticesPortChanges(nullptr));
	}

	//! The written direction keeps its own assignment: the same identity
	//! reasoning applies to a re-plugged OUTPUT port.
	void theWrittenDirectionIsRememberedToo()
	{
		FakeMidiClient client;
		const QString first = alsaName(128, 1, QStringLiteral("Zene Probe Test"),
			QStringLiteral("controller out"));
		client.live() << first;
		FakeProcessor processor;
		MidiPort port(QStringLiteral("Default"), &client, &processor, nullptr,
			MidiPort::Mode::Output);
		port.subscribeWritablePort(first);
		QCOMPARE(client.reconnect().assignments().size(), std::size_t(1));
		QVERIFY(!client.reconnect().assignments().front().readable);
	}
};


QTEST_GUILESS_MAIN(MidiReconnectTest)
#include "MidiReconnectTest.moc"
