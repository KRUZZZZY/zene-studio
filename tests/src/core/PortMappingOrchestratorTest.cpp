/*
 * PortMappingOrchestratorTest.cpp - the host's port mapping: what it does with the router's answers, and renewals
 *
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
 *
 */

/*! PortMappingCodecTest covers the byte formats and a PmpMapper against a fake gateway. This covers PortMapping,
 *  the object the host starts: it takes the answer of its PCP/NAT-PMP mapper or of its UPnP mapper, reports success
 *  (warning when the router's own external address is not a public one - another NAT in front of it), plans a renewal
 *  at half the lease, renews through the same mapper, and reports a failure in words a person can act on. The
 *  mappers' own answers are injected by emitting their `finished` signal, and the PCP mapper is pointed at a fake
 *  gateway on 127.0.0.1, so no packet leaves this machine: start() is never called, because it would ask the real
 *  default gateway for a real port. Also the two static discoveries (the default gateway, this machine's address
 *  toward it), checked against their own definitions. */

#include "lmmsconfig.h"

#include <QtTest>

#ifdef ZENE_HAVE_COLLAB

#include <QFile>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include "PortMapping.h"
#include "PortMappingCodec.h"

using namespace lmms::portmap;

namespace
{

MappingResult success(const QString& method, const QString& externalAddress, quint16 port, quint32 lifetime)
{
	MappingResult result;
	result.ok = true;
	result.method = method;
	result.externalAddress = QHostAddress(externalAddress);
	result.externalPort = port;
	result.lifetime = lifetime;
	result.message = QStringLiteral("the router opened TCP port %1").arg(port);
	return result;
}

} // namespace

class PortMappingOrchestratorTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { qRegisterMetaType<lmms::portmap::MappingResult>("lmms::portmap::MappingResult"); }

	//! Whatever this machine's network is, discovery obeys its own contract: a gateway that was not guessed is the
	//! route table's; a guessed one is the .1 of a LAN /24 and says so; no gateway is never reported as guessed.
	void theDefaultGatewayIsTheRouteTablesOrAGuessReportedAsOne()
	{
		bool guessed = true;
		const QHostAddress gateway = PortMapping::discoverGateway(&guessed);
		QHostAddress fromTable;
#ifdef Q_OS_LINUX
		QFile routes(QStringLiteral("/proc/net/route"));
		if (routes.open(QIODevice::ReadOnly)) { fromTable = gatewayFromProcRoute(routes.read(64 * 1024)); }
#endif
		QVERIFY(guessed || gateway.isNull() || gateway == fromTable);   // not guessed: the table's own answer
		QVERIFY(!guessed || (!gateway.isNull() && (gateway.toIPv4Address() & 0xffu) == 1u));  // guessed: x.y.z.1
		QVERIFY(fromTable.isNull() == (guessed || gateway.isNull()));   // a table answer is used whenever there is one
	}

	//! A UDP "connect" sends nothing; toward the loopback the address the kernel would use is the loopback, and
	//! with no gateway the answer is an up, non-loopback IPv4 interface address or nothing.
	void theLanAddressIsTheOneTheKernelRoutesTowardTheGateway()
	{
		QCOMPARE(PortMapping::lanAddress(QHostAddress(QHostAddress::LocalHost)), QHostAddress(QHostAddress::LocalHost));
		const QHostAddress lan = PortMapping::lanAddress(QHostAddress());
		QVERIFY(lan.isNull() || (lan.protocol() == QAbstractSocket::IPv4Protocol && !lan.isLoopback()));
	}

	void aFreshMappingReportsNothingAndStopIsHarmless()
	{
		PortMapping mapping;
		QVERIFY(!mapping.busy());
		QVERIFY(!mapping.result().ok);
		QVERIFY(mapping.localAddress().isNull());
		const QJsonObject json = mapping.toJson();
		QCOMPARE(json.value(QStringLiteral("busy")).toBool(), false);
		QCOMPARE(json.value(QStringLiteral("gateway")).toString(), QString());
		QCOMPARE(json.value(QStringLiteral("gateway_guessed")).toBool(), false);
		QCOMPARE(json.value(QStringLiteral("lan_address")).toString(), QString());
		QCOMPARE(json.value(QStringLiteral("internal_port")).toInt(), 0);
		QCOMPARE(json.value(QStringLiteral("ok")).toBool(), false);
		QCOMPARE(json.value(QStringLiteral("verified_from_outside")).toBool(), false);
		mapping.stop();
		QVERIFY(!mapping.busy());
	}

	void aPublicAnswerIsReportedAsItIsAndRenewedAtHalfTheLease()
	{
		PortMapping mapping;
		auto* pmp = mapping.findChild<PmpMapper*>();
		auto* renew = mapping.findChild<QTimer*>();
		QVERIFY(pmp != nullptr && renew != nullptr);
		QSignalSpy finished(&mapping, &PortMapping::finished);

		emit pmp->finished(success(QStringLiteral("nat-pmp"), QStringLiteral("8.8.8.8"), 40000, 7200));
		QCOMPARE(finished.size(), 1);
		const MappingResult result = finished.first().first().value<MappingResult>();
		QVERIFY(result.ok);
		QCOMPARE(result.method, QStringLiteral("nat-pmp"));
		QCOMPARE(result.message, QStringLiteral("the router opened TCP port 40000"));  // a public address adds no warning
		QVERIFY(!mapping.busy());
		QCOMPARE(mapping.result().externalPort, quint16(40000));
		// Half the lease, capped at an hour: 7200 s lease -> renew after 3600 s.
		QVERIFY(renew->isActive());
		QCOMPARE(renew->interval(), 3600 * 1000);
		QCOMPARE(mapping.toJson().value(QStringLiteral("external_port")).toInt(), 40000);
		QVERIFY(mapping.toJson().value(QStringLiteral("reachable_from_internet")).toBool());

		// A short lease renews at its half.
		emit pmp->finished(success(QStringLiteral("pcp"), QStringLiteral("8.8.8.8"), 40001, 600));
		QCOMPARE(renew->interval(), 300 * 1000);

		// stop(): the plan is cancelled and the result forgotten.
		mapping.stop();
		QVERIFY(!renew->isActive());
		QVERIFY(!mapping.result().ok);
	}

	void aPrivateOrMissingExternalAddressGetsTheDoubleNatWarning()
	{
		PortMapping mapping;
		auto* pmp = mapping.findChild<PmpMapper*>();
		QSignalSpy finished(&mapping, &PortMapping::finished);

		emit pmp->finished(success(QStringLiteral("nat-pmp"), QStringLiteral("192.168.50.2"), 40000, 7200));
		MappingResult result = finished.takeLast().first().value<MappingResult>();
		QVERIFY(result.ok);
		QVERIFY2(result.message.contains(QStringLiteral("not a public one")), qPrintable(result.message));
		QVERIFY(result.message.contains(QStringLiteral("192.168.50.2")));
		QVERIFY(!mapping.toJson().value(QStringLiteral("reachable_from_internet")).toBool());

		MappingResult unknown = success(QStringLiteral("nat-pmp"), QString(), 40000, 7200);
		emit pmp->finished(unknown);
		result = finished.takeLast().first().value<MappingResult>();
		QVERIFY(result.message.contains(QStringLiteral("unknown")));
	}

	void aPermanentUpnpLeaseNeverRenews()
	{
		PortMapping mapping;
		auto* upnp = mapping.findChild<UpnpMapper*>();
		auto* renew = mapping.findChild<QTimer*>();
		QVERIFY(upnp != nullptr);
		emit upnp->finished(success(QStringLiteral("upnp"), QStringLiteral("8.8.4.4"), 5000, 0));
		QVERIFY(mapping.result().ok);
		QCOMPARE(mapping.result().method, QStringLiteral("upnp"));
		QVERIFY(!renew->isActive());
	}

	void aUpnpFailureBecomesAnActionableMessage()
	{
		PortMapping mapping;
		auto* upnp = mapping.findChild<UpnpMapper*>();
		QSignalSpy finished(&mapping, &PortMapping::finished);
		MappingResult failure;
		failure.message = QStringLiteral("no UPnP router answered on this network");
		emit upnp->finished(failure);

		QCOMPARE(finished.size(), 1);
		const MappingResult result = finished.first().first().value<MappingResult>();
		QVERIFY(!result.ok);
		QVERIFY(!mapping.busy());
		QVERIFY2(result.message.contains(QStringLiteral("no UPnP router answered")), qPrintable(result.message));
		QVERIFY(result.message.contains(QStringLiteral("didn't open the port")));
		QVERIFY(result.message.contains(QStringLiteral("forward TCP port")));
		QCOMPARE(mapping.toJson().value(QStringLiteral("ok")).toBool(), false);
	}

	//! The renewal asks the SAME mapper for the SAME external port again: here a PCP gateway on 127.0.0.1.
	void aRenewalAsksTheGatewayForTheSameExternalPort()
	{
		QUdpSocket gateway;
		QVERIFY(gateway.bind(QHostAddress(QHostAddress::LocalHost), 0));
		QList<QByteArray> requests;
		connect(&gateway, &QUdpSocket::readyRead, this, [&] {
			while (gateway.hasPendingDatagrams())
			{
				const QNetworkDatagram datagram = gateway.receiveDatagram();
				requests.append(datagram.data());
				// A NAT-PMP TCP mapping success: echo the internal port, grant external 40000 for 7200 s.
				if (datagram.data().at(0) != 2 && datagram.data().size() > 2)
				{
					QByteArray reply = QByteArray::fromHex("00820000000003e8") + datagram.data().mid(4, 2)
						+ QByteArray::fromHex("9c4000001c20");
					gateway.writeDatagram(reply, datagram.senderAddress(), datagram.senderPort());
				}
				else if (datagram.data().at(0) == 2)
				{
					// PCP is answered "unsupported version": the mapper falls back to NAT-PMP.
					gateway.writeDatagram(QByteArray::fromHex("00810001000003e8"), datagram.senderAddress(), datagram.senderPort());
				}
				else
				{
					gateway.writeDatagram(QByteArray::fromHex("00800000000003e8cb007107"), datagram.senderAddress(), datagram.senderPort());
				}
			}
		});

		PortMapping mapping;
		auto* pmp = mapping.findChild<PmpMapper*>();
		auto* renew = mapping.findChild<QTimer*>();
		pmp->setServer(QHostAddress(QHostAddress::LocalHost), gateway.localPort());
		pmp->setClientAddress(QHostAddress(QStringLiteral("192.168.1.20")));
		pmp->setRetrySchedule({100, 100});
		QSignalSpy finished(&mapping, &PortMapping::finished);

		emit pmp->finished(success(QStringLiteral("nat-pmp"), QStringLiteral("8.8.8.8"), 40000, 7200));
		QCOMPARE(finished.size(), 1);
		// The plan fires: the mapper asks again (PCP first, refused, then NAT-PMP's address and mapping).
		renew->start(10);  // the plan's timer, brought forward: its timeout is the renewal
		QTRY_VERIFY_WITH_TIMEOUT(finished.size() == 2, 5000);
		const MappingResult renewed = finished.last().first().value<MappingResult>();
		QVERIFY2(renewed.ok, qPrintable(renewed.message));
		QCOMPARE(renewed.externalPort, quint16(40000));
		QVERIFY(requests.size() >= 3);
		// The last request is the NAT-PMP map for external port 40000 (bytes 6-7 of the request), for the renewal.
		QCOMPARE(requests.last().mid(6, 2), QByteArray::fromHex("9c40"));
		QVERIFY(renew->isActive());  // and the renewed lease is planned again
	}
};

QTEST_GUILESS_MAIN(PortMappingOrchestratorTest)
#include "PortMappingOrchestratorTest.moc"

#else // !ZENE_HAVE_COLLAB

class PortMappingOrchestratorTest : public QObject
{
	Q_OBJECT
private slots:
	void compiledOut() { QSKIP("this build has no port mapping (ZENE_HAVE_COLLAB is not defined)"); }
};

QTEST_GUILESS_MAIN(PortMappingOrchestratorTest)
#include "PortMappingOrchestratorTest.moc"

#endif // ZENE_HAVE_COLLAB
