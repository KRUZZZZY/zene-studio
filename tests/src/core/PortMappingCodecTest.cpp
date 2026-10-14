/*
 * PortMappingCodecTest.cpp - NAT-PMP/PCP against a fake gateway, and the UPnP
 *                            formats against recorded bytes (0.5.0 F6)
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

/*! No router is needed. The fake gateway is a QUdpSocket on 127.0.0.1 that answers the way RFC 6886 / 6887
 *  gateways do; the UPnP description and SOAP replies are recorded shapes of a miniupnpd IGD. */

#include "lmmsconfig.h"

#include <QtTest>

#ifdef ZENE_HAVE_COLLAB

#include <QNetworkDatagram>
#include <QUdpSocket>

#include "PortMapping.h"
#include "PortMappingCodec.h"

using namespace lmms::portmap;

namespace
{

QByteArray hex(const char* text)
{
	return QByteArray::fromHex(text);
}

//! How the fake gateway answers.
enum class Gateway
{
	PmpOnly,  //!< "unsupported version" to PCP, then NAT-PMP
	Pcp,      //!< a PCP MAP success
	Silent,   //!< never answers
};

const char* const RecordedDescription = R"(<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0"><specVersion><major>1</major><minor>0</minor></specVersion>
<device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
<serviceList><service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>
<controlURL>/ctl/L3F</controlURL></service></serviceList>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>
<serviceList><service><serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>
<controlURL>/ctl/PPPConn</controlURL></service>
<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>
<serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId><controlURL>/ctl/IPConn</controlURL>
<eventSubURL>/evt/IPConn</eventSubURL><SCPDURL>/WANIPCn.xml</SCPDURL></service></serviceList>
</device></deviceList></device></deviceList></device></root>)";

} // namespace

class PortMappingCodecTest : public QObject
{
	Q_OBJECT
private:
	QUdpSocket m_gateway;
	Gateway m_mode = Gateway::PmpOnly;
	QList<QByteArray> m_requests;

	void answer()
	{
		while (m_gateway.hasPendingDatagrams())
		{
			const QNetworkDatagram datagram = m_gateway.receiveDatagram();
			m_requests.append(datagram.data());
			const QByteArray reply = replyTo(datagram.data());
			if (!reply.isEmpty()) { m_gateway.writeDatagram(reply, datagram.senderAddress(), datagram.senderPort()); }
		}
	}

	QByteArray replyTo(const QByteArray& request) const
	{
		if (m_mode == Gateway::Silent) { return QByteArray(); }
		if (request.at(0) == 2) { return m_mode == Gateway::Pcp ? pcpSuccess(request) : hex("00810001000003e8"); }
		if (request.size() == 2) { return hex("00800000000003e8cb007107"); }  // external 203.0.113.7
		// A TCP map: echo the internal port, grant external 40000 for 7200 s.
		return hex("00820000000003e8") + request.mid(4, 2) + hex("9c4000001c20");
	}

	static QByteArray pcpSuccess(const QByteArray& request)
	{
		QByteArray reply = hex("02810000") + hex("00001c20") + hex("000003e8") + QByteArray(12, '\0');
		reply += request.mid(24, 12);                       // the nonce
		reply += hex("06000000") + request.mid(40, 2);       // protocol, reserved, internal port
		reply += hex("9c41") + hex("00000000000000000000ffffcb007108");  // 40001 on 203.0.113.8
		return reply;
	}

	MappingResult runMapper()
	{
		PmpMapper mapper;
		mapper.setServer(QHostAddress(QHostAddress::LocalHost), m_gateway.localPort());
		mapper.setClientAddress(QHostAddress(QStringLiteral("192.168.1.20")));
		mapper.setRetrySchedule({100, 100});
		QSignalSpy finished(&mapper, &PmpMapper::finished);
		mapper.map(41740, 41740, 7200);
		if (!finished.wait(5000)) { return MappingResult(); }
		return finished.first().first().value<MappingResult>();
	}

private slots:
	void initTestCase()
	{
		qRegisterMetaType<lmms::portmap::MappingResult>("lmms::portmap::MappingResult");
		QVERIFY(m_gateway.bind(QHostAddress(QHostAddress::LocalHost), 0));
		connect(&m_gateway, &QUdpSocket::readyRead, this, &PortMappingCodecTest::answer);
	}

	void init() { m_requests.clear(); }

	//! Byte-exact NAT-PMP requests (RFC 6886 3.2 / 3.3) and a decoded mapping response.
	void natPmpEncodeDecode()
	{
		QCOMPARE(encodePmpAddressRequest(), hex("0000"));
		QCOMPARE(encodePmpMapRequest(41740, 41741, 7200), hex("00020000a30ca30d00001c20"));
		QCOMPARE(encodePmpMapRequest(41740, 0, 0), hex("00020000a30c000000000000"));
		const PmpReply map = decodePmpReply(hex("0082000000000064a30c9c4000001c20"));
		QVERIFY(map.valid);
		QCOMPARE(map.opcode, quint8(130));
		QCOMPARE(map.internalPort, quint16(41740));
		QCOMPARE(map.externalPort, quint16(40000));
		QCOMPARE(map.lifetime, quint32(7200));
		const PmpReply address = decodePmpReply(hex("0080000000000064cb007107"));
		QCOMPARE(address.externalAddress, QHostAddress(QStringLiteral("203.0.113.7")));
		QVERIFY(!decodePmpReply(hex("008200")).valid);       // truncated
		QVERIFY(!decodePmpReply(hex("0082000000000064")).valid); // truncated mapping success
		QVERIFY(!decodePmpReply(hex("0080000000000064")).valid); // truncated address success
		QVERIFY(!decodePmpReply(hex("0002000000000000")).valid);  // a request, not a response
		QCOMPARE(decodePmpReply(hex("0082000200000064")).result, quint16(2));
	}

	//! A PCP MAP request is 60 bytes with the client address IPv4-mapped; a reply is only accepted with our nonce.
	void pcpEncodeDecode()
	{
		const QByteArray nonce = hex("0102030405060708090a0b0c");
		const QByteArray request = encodePcpMapRequest(nonce, QHostAddress(QStringLiteral("192.168.1.20")), 41740, 41740, 7200);
		QCOMPARE(request.size(), 60);
		QCOMPARE(request.left(8), hex("0201000000001c20"));
		QCOMPARE(request.mid(8, 16), hex("00000000000000000000ffffc0a80114"));
		QCOMPARE(request.mid(24, 12), nonce);
		QCOMPARE(static_cast<quint8>(request.at(36)), quint8(6));
		const PcpReply reply = decodePcpReply(pcpSuccess(request), nonce);
		QVERIFY(reply.valid);
		QCOMPARE(reply.externalPort, quint16(40001));
		QCOMPARE(reply.externalAddress, QHostAddress(QStringLiteral("203.0.113.8")));
		QVERIFY(!decodePcpReply(pcpSuccess(request), hex("ffffffffffffffffffffffff")).valid);
		QVERIFY(decodePcpReply(hex("00810001000003e8"), nonce).pmpOnly);
	}

	//! The fake gateway speaks NAT-PMP only: PCP is refused, the address and the mapping come from NAT-PMP.
	void natPmpAgainstFakeGateway()
	{
		m_mode = Gateway::PmpOnly;
		const MappingResult result = runMapper();
		QVERIFY2(result.ok, qPrintable(result.message));
		QCOMPARE(result.method, QStringLiteral("nat-pmp"));
		QCOMPARE(result.externalAddress, QHostAddress(QStringLiteral("203.0.113.7")));
		QCOMPARE(result.externalPort, quint16(40000));
		QCOMPARE(result.lifetime, quint32(7200));
		QCOMPARE(m_requests.size(), 3);
		QCOMPARE(m_requests.at(1), encodePmpAddressRequest());
		QCOMPARE(m_requests.at(2), encodePmpMapRequest(41740, 41740, 7200));
	}

	void pcpAgainstFakeGateway()
	{
		m_mode = Gateway::Pcp;
		const MappingResult result = runMapper();
		QVERIFY2(result.ok, qPrintable(result.message));
		QCOMPARE(result.method, QStringLiteral("pcp"));
		QCOMPARE(result.externalPort, quint16(40001));
		QCOMPARE(result.externalAddress, QHostAddress(QStringLiteral("203.0.113.8")));
	}

	//! A gateway that never answers: retried on the schedule, then a plain failure that says what was tried.
	void silentGatewayFailsPlainly()
	{
		m_mode = Gateway::Silent;
		const MappingResult result = runMapper();
		QVERIFY(!result.ok);
		QVERIFY2(result.message.contains(QStringLiteral("did not answer")), qPrintable(result.message));
		QCOMPARE(m_requests.size(), 4);  // two PCP tries, two NAT-PMP address tries
	}

	void gatewayFromRecordedRouteTable()
	{
		const QByteArray table = "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n"
			"wlp2s0\t00000000\t0101A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n"
			"eth0\t00000000\t0100000A\t0003\t0\t0\t100\t00000000\t0\t0\t0\n"
			"eth0\t0000000A\t00000000\t0001\t0\t0\t100\t00FFFFFF\t0\t0\t0\n";
		QCOMPARE(gatewayFromProcRoute(table), QHostAddress(QStringLiteral("10.0.0.1")));  // the lower metric wins
		QVERIFY(gatewayFromProcRoute("Iface\tDestination\tGateway\n").isNull());
	}

	void ssdpAndDescription()
	{
		const QByteArray ssdp = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:schemas-upnp-org:device:"
			"InternetGatewayDevice:1\r\nUSN: uuid:x::urn\r\nLocation: http://192.168.1.1:5000/rootDesc.xml\r\n\r\n";
		QCOMPARE(ssdpLocation(ssdp), QUrl(QStringLiteral("http://192.168.1.1:5000/rootDesc.xml")));
		QVERIFY(!ssdpLocation("HTTP/1.1 200 OK\r\n\r\n").isValid());
		QVERIFY(ssdpSearch(QStringLiteral("ssdp:all")).startsWith("M-SEARCH * HTTP/1.1\r\n"));
		const IgdService service = parseIgdDescription(RecordedDescription, QUrl(QStringLiteral("http://192.168.1.1:5000/rootDesc.xml")));
		QVERIFY(service.isValid());
		QCOMPARE(service.serviceType, QStringLiteral("urn:schemas-upnp-org:service:WANIPConnection:1"));
		QCOMPARE(service.controlUrl, QUrl(QStringLiteral("http://192.168.1.1:5000/ctl/IPConn")));
		const QUrl routerLocation(QStringLiteral("http://192.168.1.1:5000/rootDesc.xml"));
		const QByteArray hostileBase = R"(<root><URLBase>http://127.0.0.1:8123/</URLBase><service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>/control</controlURL></service></root>)";
		const QByteArray hostileControl = R"(<root><service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>http://127.0.0.1:8123/control</controlURL></service></root>)";
		QVERIFY(!parseIgdDescription(hostileBase, routerLocation).isValid());
		QVERIFY(!parseIgdDescription(hostileControl, routerLocation).isValid());
		QVERIFY(!parseIgdDescription("<root><device/></root>", QUrl(QStringLiteral("http://192.168.1.1/"))).isValid());
	}

	void soapShapes()
	{
		const QByteArray envelope = soapEnvelope(QStringLiteral("AddPortMapping"), QStringLiteral("urn:schemas-upnp-org:service:WANIPConnection:1"),
			{{QStringLiteral("NewExternalPort"), QStringLiteral("41740")}, {QStringLiteral("NewPortMappingDescription"), QStringLiteral("a<b")}});
		QVERIFY(envelope.contains("<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"));
		QVERIFY(envelope.contains("<NewExternalPort>41740</NewExternalPort>"));
		QVERIFY(envelope.contains("<NewPortMappingDescription>a&lt;b</NewPortMappingDescription>"));
		const QByteArray address = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
			"<u:GetExternalIPAddressResponse xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\"><NewExternalIPAddress>"
			"198.51.100.20</NewExternalIPAddress></u:GetExternalIPAddressResponse></s:Body></s:Envelope>";
		QCOMPARE(soapValue(address, QStringLiteral("NewExternalIPAddress")), QStringLiteral("198.51.100.20"));
		QCOMPARE(soapErrorCode(address), 0);
		const QByteArray fault = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><s:Fault><faultcode>"
			"s:Client</faultcode><detail><UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>718</errorCode>"
			"<errorDescription>ConflictInMappingEntry</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>";
		QCOMPARE(soapErrorCode(fault), 718);
		QCOMPARE(soapValue(fault, QStringLiteral("errorDescription")), QStringLiteral("ConflictInMappingEntry"));
	}

	void publicAddressClassification()
	{
		for (const char* address : {"198.18.0.1", "198.19.255.254", "198.51.100.20", "203.0.113.7", "10.1.2.3", "172.16.0.1",
				 "172.31.255.255", "192.0.0.8", "192.0.0.170", "192.0.2.1", "192.88.99.1", "192.168.0.1", "100.64.0.1", "100.127.255.254",
				 "127.0.0.1", "169.254.1.1", "0.0.0.0", "224.0.0.1"})
		{
			QVERIFY2(!isPublicIPv4(QHostAddress(QString::fromLatin1(address))), address);
		}
		for (const char* address : {"8.8.8.8", "172.32.0.1", "100.128.0.1", "192.0.0.9", "192.0.0.10", "192.31.196.1",
				 "192.52.193.1", "192.175.48.1", "198.17.255.255", "198.20.0.1"})
		{
			QVERIFY2(isPublicIPv4(QHostAddress(QString::fromLatin1(address))), address);
		}
	}
};

QTEST_GUILESS_MAIN(PortMappingCodecTest)
#include "PortMappingCodecTest.moc"

#else // !ZENE_HAVE_COLLAB

class PortMappingCodecTest : public QObject
{
	Q_OBJECT
private slots:
	void compiledOut() { QSKIP("this build has no port mapping (ZENE_HAVE_COLLAB is not defined)"); }
};

QTEST_GUILESS_MAIN(PortMappingCodecTest)
#include "PortMappingCodecTest.moc"

#endif // ZENE_HAVE_COLLAB
