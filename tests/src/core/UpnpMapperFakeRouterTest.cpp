/*
 * UpnpMapperFakeRouterTest.cpp - the UPnP mapper against a fake internet gateway device on this machine's LAN address
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

/*! UpnpMapper finds a router by SSDP (a multicast M-SEARCH, answered by unicast), fetches its device description
 *  over HTTP, then speaks SOAP: AddPortMapping, GetExternalIPAddress, and DeletePortMapping on the way out. It
 *  refuses to fetch a description from anything but a LAN host, so a fake router has to live on a LAN address, not
 *  on 127.0.0.1: this test finds this machine's private IPv4 address, answers the search from a socket joined to the
 *  SSDP group and serves the description and the SOAP actions from an HTTP server on that address. Every
 *  recorded shape is a miniupnpd IGD's. The cases: a mapping and its removal, a renewal that needs no new search, a
 *  router that refuses (718), one that only takes permanent leases (725 then lease 0), a device that is not a gateway,
 *  and a description that cannot be read. The search is a multicast datagram, so a real router on the LAN
 *  could in principle answer too; the fake answers within microseconds, and the mapper takes the first answer.
 *  There is deliberately NO "no router answers" case: with a silent fake, the real router on this LAN answers
 *  the search and is sent a real AddPortMapping (measured: it replied UPnP error 501 to it), so the search
 *  time-out cannot be tested without a network namespace. Skipped, with the reason, where the machine has no
 *  LAN address or port 1900 cannot be shared. */

#include "lmmsconfig.h"

#include <QtTest>

#ifdef ZENE_HAVE_COLLAB

#include <QHash>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#include "PortMapping.h"
#include "PortMappingCodec.h"

using namespace lmms::portmap;

namespace
{

const char* const Description = R"(<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0"><specVersion><major>1</major><minor>0</minor></specVersion>
<device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>
<serviceList><service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>
<serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId><controlURL>/ctl/IPConn</controlURL>
<eventSubURL>/evt/IPConn</eventSubURL><SCPDURL>/WANIPCn.xml</SCPDURL></service></serviceList>
</device></deviceList></device></deviceList></device></root>)";

const char* const NotAGateway = R"(<?xml version="1.0"?><root><device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType></device></root>)";

const char* const SoapHead = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>";
const char* const SoapTail = "</s:Body></s:Envelope>";

enum class Mode
{
	Normal,
	Refuse718,         //!< AddPortMapping: ConflictInMappingEntry
	PermanentOnly,     //!< AddPortMapping with a lease: 725 OnlyPermanentLeasesSupported
	NotAnIgd,          //!< a description without a WAN connection service
	DescriptionError,  //!< the description is an HTTP 500
};

struct Recorded
{
	QByteArray requestLine;
	QByteArray soapAction;
	QByteArray body;
};

QString lanAddress()
{
	for (const QHostAddress& address : QNetworkInterface::allAddresses())
	{
		bool v4 = false;
		address.toIPv4Address(&v4);
		if (v4 && !address.isLoopback() && !isPublicIPv4(address) && (address.toIPv4Address() >> 24) != 169) { return address.toString(); }
	}
	return QString();
}

QByteArray soapFault(int code, const char* text)
{
	return QByteArray(SoapHead) + "<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>"
		"<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>" + QByteArray::number(code) + "</errorCode>"
		"<errorDescription>" + text + "</errorDescription></UPnPError></detail></s:Fault>" + SoapTail;
}

QByteArray soapReply(const QByteArray& action, const QByteArray& value = QByteArray())
{
	return QByteArray(SoapHead) + "<u:" + action + "Response xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">" + value
		+ "</u:" + action + "Response>" + SoapTail;
}

} // namespace

class FakeRouter : public QObject
{
	Q_OBJECT
public:
	Mode mode = Mode::Normal;
	QList<Recorded> requests;

	bool start(const QString& lan, QString* why)
	{
		m_lan = lan;
		if (!m_http.listen(QHostAddress(lan), 0)) { *why = QStringLiteral("cannot listen on %1").arg(lan); return false; }
		if (!m_ssdp.bind(QHostAddress(QHostAddress::AnyIPv4), SsdpPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
		{
			*why = QStringLiteral("SSDP port 1900 cannot be shared: %1").arg(m_ssdp.errorString());
			return false;
		}
		if (!m_ssdp.joinMulticastGroup(QHostAddress(QStringLiteral("239.255.255.250"))))
		{
			*why = QStringLiteral("cannot join the SSDP group: %1").arg(m_ssdp.errorString());
			return false;
		}
		connect(&m_ssdp, &QUdpSocket::readyRead, this, &FakeRouter::onSearch);
		connect(&m_http, &QTcpServer::newConnection, this, &FakeRouter::onConnection);
		return true;
	}

	int count(const char* needle) const
	{
		int n = 0;
		for (const Recorded& request : requests) { n += request.requestLine.contains(needle) || request.soapAction.contains(needle) ? 1 : 0; }
		return n;
	}

	Recorded last(const char* needle) const
	{
		for (int i = requests.size() - 1; i >= 0; --i)
		{
			if (requests.at(i).requestLine.contains(needle) || requests.at(i).soapAction.contains(needle)) { return requests.at(i); }
		}
		return Recorded();
	}

private slots:
	void onSearch()
	{
		while (m_ssdp.hasPendingDatagrams())
		{
			const QNetworkDatagram datagram = m_ssdp.receiveDatagram();
			if (!datagram.data().startsWith("M-SEARCH")) { continue; }
			const QByteArray reply = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
				"USN: uuid:fake-router::urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\nLocation: http://" + m_lan.toLatin1() + ':'
				+ QByteArray::number(m_http.serverPort()) + "/rootDesc.xml\r\n\r\n";
			m_ssdp.writeDatagram(reply, datagram.senderAddress(), datagram.senderPort());
		}
	}

	void onConnection()
	{
		while (QTcpSocket* socket = m_http.nextPendingConnection())
		{
			connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onRequest(socket); });
			connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
		}
	}

private:
	static int bodyLength(const QByteArray& head)
	{
		for (const QByteArray& line : head.split('\n'))
		{
			if (line.toLower().startsWith("content-length:")) { return line.mid(15).trimmed().toInt(); }
		}
		return 0;
	}

	static QByteArray headerValue(const QByteArray& head, const char* name)
	{
		for (const QByteArray& line : head.split('\n'))
		{
			if (line.toLower().startsWith(QByteArray(name).toLower() + ':')) { return line.mid(static_cast<int>(qstrlen(name)) + 1).trimmed(); }
		}
		return QByteArray();
	}

	void onRequest(QTcpSocket* socket)
	{
		QByteArray& buffer = m_buffers[socket];
		buffer += socket->readAll();
		const int end = buffer.indexOf("\r\n\r\n");
		if (end < 0) { return; }
		const QByteArray head = buffer.left(end);
		const int length = bodyLength(head);
		if (buffer.size() < end + 4 + length) { return; }
		Recorded request;
		request.requestLine = head.left(head.indexOf('\r'));
		request.soapAction = headerValue(head, "SOAPAction");
		request.body = buffer.mid(end + 4, length);
		m_buffers.remove(socket);
		requests.append(request);
		respond(socket, request);
	}

	void respond(QTcpSocket* socket, const Recorded& request)
	{
		int status = 200;
		QByteArray body;
		if (request.requestLine.startsWith("GET")) { body = describe(&status); }
		else { body = act(request, &status); }
		socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") + "\r\nContent-Type: text/xml\r\nContent-Length: "
			+ QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
		socket->disconnectFromHost();
	}

	QByteArray describe(int* status) const
	{
		if (mode == Mode::DescriptionError) { *status = 500; return "oops"; }
		return mode == Mode::NotAnIgd ? QByteArray(NotAGateway) : QByteArray(Description);
	}

	QByteArray act(const Recorded& request, int* status) const
	{
		if (request.soapAction.contains("AddPortMapping"))
		{
			if (mode == Mode::Refuse718) { *status = 500; return soapFault(718, "ConflictInMappingEntry"); }
			if (mode == Mode::PermanentOnly && !request.body.contains("<NewLeaseDuration>0</NewLeaseDuration>"))
			{
				*status = 500;
				return soapFault(725, "OnlyPermanentLeasesSupported");
			}
			return soapReply("AddPortMapping");
		}
		if (request.soapAction.contains("GetExternalIPAddress")) { return soapReply("GetExternalIPAddress", "<NewExternalIPAddress>203.0.113.9</NewExternalIPAddress>"); }
		return soapReply("DeletePortMapping");
	}

	QString m_lan;
	QUdpSocket m_ssdp;
	QTcpServer m_http;
	QHash<QTcpSocket*, QByteArray> m_buffers;
};

class UpnpMapperFakeRouterTest : public QObject
{
	Q_OBJECT

	FakeRouter m_router;
	QString m_lan;

	MappingResult map(UpnpMapper& mapper, quint16 port, quint32 lease, int timeoutMs = 10000)
	{
		QSignalSpy finished(&mapper, &UpnpMapper::finished);
		mapper.map(port, port, lease, QHostAddress(m_lan));
		if (!finished.wait(timeoutMs)) { return MappingResult(); }
		return finished.first().first().value<MappingResult>();
	}

private slots:
	void initTestCase()
	{
		qRegisterMetaType<lmms::portmap::MappingResult>("lmms::portmap::MappingResult");
		m_lan = lanAddress();
		if (m_lan.isEmpty()) { QSKIP("this machine has no private LAN IPv4 address for a fake router to live on"); }
		QString why;
		if (!m_router.start(m_lan, &why)) { QSKIP(qPrintable(why)); }
	}

	void init()
	{
		m_router.mode = Mode::Normal;
		m_router.requests.clear();
	}

	void aMappingIsMadeRenewedAndRemoved()
	{
		UpnpMapper mapper;
		const MappingResult result = map(mapper, 41740, 3600);
		QVERIFY2(result.ok, qPrintable(result.message));
		QCOMPARE(result.method, QStringLiteral("upnp"));
		QCOMPARE(result.externalPort, quint16(41740));
		QCOMPARE(result.lifetime, quint32(3600));
		QCOMPARE(result.externalAddress, QHostAddress(QStringLiteral("203.0.113.9")));
		QVERIFY(result.message.contains(QStringLiteral("41740")));

		// The description was fetched once, then the two SOAP actions in order.
		QCOMPARE(m_router.count("GET /rootDesc.xml"), 1);
		const Recorded add = m_router.last("AddPortMapping");
		QVERIFY(add.soapAction.contains("urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping"));
		QVERIFY(add.body.contains("<NewExternalPort>41740</NewExternalPort>"));
		QVERIFY(add.body.contains("<NewInternalPort>41740</NewInternalPort>"));
		QVERIFY(add.body.contains("<NewProtocol>TCP</NewProtocol>"));
		QVERIFY(add.body.contains("<NewInternalClient>" + m_lan.toLatin1() + "</NewInternalClient>"));
		QVERIFY(add.body.contains("<NewLeaseDuration>3600</NewLeaseDuration>"));
		QVERIFY(add.body.contains("Zene Studio collaboration"));
		QCOMPARE(m_router.count("GetExternalIPAddress"), 1);

		// A renewal knows the router already: no new search or description, just the mapping again.
		const MappingResult renewed = map(mapper, 41740, 3600);
		QVERIFY2(renewed.ok, qPrintable(renewed.message));
		QCOMPARE(m_router.count("GET /rootDesc.xml"), 1);
		QCOMPARE(m_router.count("AddPortMapping"), 2);

		// Removing it tells the router, once; a second removal has nothing to remove.
		mapper.unmap();
		QTRY_COMPARE_WITH_TIMEOUT(m_router.count("DeletePortMapping"), 1, 5000);
		const Recorded removal = m_router.last("DeletePortMapping");
		QVERIFY(removal.body.contains("<NewExternalPort>41740</NewExternalPort>"));
		QVERIFY(removal.body.contains("<NewProtocol>TCP</NewProtocol>"));
		mapper.unmap();
		QTest::qWait(100);
		QCOMPARE(m_router.count("DeletePortMapping"), 1);
	}

	void aRouterThatRefusesIsReportedWithItsOwnWords()
	{
		m_router.mode = Mode::Refuse718;
		UpnpMapper mapper;
		const MappingResult result = map(mapper, 41741, 3600);
		QVERIFY(!result.ok);
		QVERIFY2(result.message.contains(QStringLiteral("refused the UPnP mapping")), qPrintable(result.message));
		QVERIFY2(result.message.contains(QStringLiteral("UPnP error 718: ConflictInMappingEntry")), qPrintable(result.message));
		QCOMPARE(m_router.count("GetExternalIPAddress"), 0);
		mapper.unmap();  // nothing was mapped: nothing is removed
		QTest::qWait(100);
		QCOMPARE(m_router.count("DeletePortMapping"), 0);
	}

	void aRouterThatOnlyTakesPermanentLeasesIsAskedAgainWithoutOne()
	{
		m_router.mode = Mode::PermanentOnly;
		UpnpMapper mapper;
		const MappingResult result = map(mapper, 41742, 3600);
		QVERIFY2(result.ok, qPrintable(result.message));
		QCOMPARE(result.lifetime, quint32(0));  // permanent
		QCOMPARE(m_router.count("AddPortMapping"), 2);
		QVERIFY(m_router.last("AddPortMapping").body.contains("<NewLeaseDuration>0</NewLeaseDuration>"));
	}

	void aDeviceThatIsNotAGatewayAndAnUnreadableDescriptionAreRefusedPlainly()
	{
		m_router.mode = Mode::NotAnIgd;
		{
			UpnpMapper mapper;
			const MappingResult result = map(mapper, 41743, 3600);
			QVERIFY(!result.ok);
			QVERIFY2(result.message.contains(QStringLiteral("not an internet gateway")), qPrintable(result.message));
		}
		m_router.mode = Mode::DescriptionError;
		{
			UpnpMapper mapper;
			const MappingResult result = map(mapper, 41744, 3600);
			QVERIFY(!result.ok);
			QVERIFY2(result.message.contains(QStringLiteral("description could not be read")), qPrintable(result.message));
		}
		QCOMPARE(m_router.count("AddPortMapping"), 0);
	}
};

QTEST_GUILESS_MAIN(UpnpMapperFakeRouterTest)
#include "UpnpMapperFakeRouterTest.moc"

#else // !ZENE_HAVE_COLLAB

class UpnpMapperFakeRouterTest : public QObject
{
	Q_OBJECT
private slots:
	void compiledOut() { QSKIP("this build has no port mapping (ZENE_HAVE_COLLAB is not defined)"); }
};

QTEST_GUILESS_MAIN(UpnpMapperFakeRouterTest)
#include "UpnpMapperFakeRouterTest.moc"

#endif // ZENE_HAVE_COLLAB
