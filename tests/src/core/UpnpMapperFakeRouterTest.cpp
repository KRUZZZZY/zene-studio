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

/*! UpnpMapper finds a router by SSDP (an M-SEARCH, answered by unicast), fetches its device description over
 *  HTTP, then speaks SOAP: AddPortMapping, GetExternalIPAddress, and DeletePortMapping on the way out. The whole
 *  exchange runs on 127.0.0.1: the mapper's test seam (UpnpMapper::setSearchEndpoint, PortMapping::setTestEndpoints)
 *  points the search at a UDP socket here instead of the SSDP multicast group and accepts a router on a loopback
 *  address, so no packet leaves this machine and no real router can answer. The recorded shapes are a miniupnpd
 *  IGD's. Cases: a mapping and its removal, a renewal that needs no new search, a router that refuses (718), one that
 *  only takes permanent leases (725 then lease 0), a device that is not a gateway, a description that cannot be
 *  read, a description redirect that is refused rather than followed, and no router at all (the search time-out). Then PortMapping::start() end to end: NAT-PMP answered, a silent
 *  gateway falling back to UPnP, both failing, and no gateway at all. */

#include "lmmsconfig.h"

#include <QtTest>

#ifdef ZENE_HAVE_COLLAB

#include <algorithm>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkReply>
#include <QPointer>
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
	Redirect,          //!< the description is a 302 to another URL on this machine
	Silent,            //!< never answers the search
};

struct Recorded
{
	QByteArray requestLine;
	QByteArray soapAction;
	QByteArray body;
};

//! A SOAP action whose answer the test releases by hand: the connection stays open until then.
struct HeldAction
{
	QPointer<QTcpSocket> socket;
	Recorded request;
};

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
	bool holdActions = false;  //!< when set, SOAP answers wait in `held` until releaseHeld() sends them
	QList<HeldAction> held;

	//! Sends the answer to a held action (by its index in arrival order); the action leaves `held`.
	void releaseHeld(int index)
	{
		if (index < 0 || index >= held.size()) { return; }
		const HeldAction action = held.takeAt(index);
		if (action.socket) { answer(action.socket.data(), action.request); }
	}

	bool start(QString* why)
	{
		const QHostAddress loopback(QHostAddress::LocalHost);
		if (!m_http.listen(loopback, 0)) { *why = QStringLiteral("cannot listen on 127.0.0.1"); return false; }
		if (!m_ssdp.bind(loopback, 0)) { *why = QStringLiteral("cannot bind the fake SSDP socket: %1").arg(m_ssdp.errorString()); return false; }
		connect(&m_ssdp, &QUdpSocket::readyRead, this, &FakeRouter::onSearch);
		connect(&m_http, &QTcpServer::newConnection, this, &FakeRouter::onConnection);
		return true;
	}

	//! Where the mapper's search must go.
	quint16 searchPort() const { return m_ssdp.localPort(); }

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
			if (mode == Mode::Silent || !datagram.data().startsWith("M-SEARCH")) { continue; }
			const QByteArray reply = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
				"USN: uuid:fake-router::urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\nLocation: http://127.0.0.1:"
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
		if (holdActions && !request.requestLine.startsWith("GET")) { held.append({socket, request}); return; }
		answer(socket, request);
		return;
	}

	void answer(QTcpSocket* socket, const Recorded& request)
	{
		if (mode == Mode::Redirect && request.requestLine.startsWith("GET /rootDesc.xml"))
		{
			socket->write("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + QByteArray::number(m_http.serverPort())
				+ "/moved/rootDesc.xml\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
			socket->disconnectFromHost();
			return;
		}
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

	QUdpSocket m_ssdp;
	QTcpServer m_http;
	QHash<QTcpSocket*, QByteArray> m_buffers;
};

//! A PCP/NAT-PMP gateway on 127.0.0.1: PCP is answered "unsupported version", then NAT-PMP's address and mapping.
class FakeGateway : public QObject
{
	Q_OBJECT
public:
	bool silent = false;
	QList<QByteArray> requests;

	bool start()
	{
		if (!m_socket.bind(QHostAddress(QHostAddress::LocalHost), 0)) { return false; }
		connect(&m_socket, &QUdpSocket::readyRead, this, &FakeGateway::answer);
		return true;
	}
	quint16 port() const { return m_socket.localPort(); }

private slots:
	void answer()
	{
		while (m_socket.hasPendingDatagrams())
		{
			const QNetworkDatagram datagram = m_socket.receiveDatagram();
			requests.append(datagram.data());
			if (silent) { continue; }
			m_socket.writeDatagram(replyTo(datagram.data()), datagram.senderAddress(), datagram.senderPort());
		}
	}

private:
	static QByteArray replyTo(const QByteArray& request)
	{
		if (request.at(0) == 2) { return QByteArray::fromHex("00810001000003e8"); }   // PCP: unsupported version
		if (request.size() == 2) { return QByteArray::fromHex("00800000000003e8cb007107"); }  // external 203.0.113.7
		return QByteArray::fromHex("00820000000003e8") + request.mid(4, 2) + QByteArray::fromHex("9c4000001c20");
	}

	QUdpSocket m_socket;
};

class UpnpMapperFakeRouterTest : public QObject
{
	Q_OBJECT

	FakeRouter m_router;
	FakeGateway m_gateway;
	const QString m_lan = QStringLiteral("127.0.0.1");

	MappingResult map(UpnpMapper& mapper, quint16 port, quint32 lease, int timeoutMs = 10000)
	{
		mapper.setSearchEndpoint(QHostAddress(QHostAddress::LocalHost), m_router.searchPort(), true);
		QSignalSpy finished(&mapper, &UpnpMapper::finished);
		mapper.map(port, port, lease, QHostAddress(m_lan));
		if (!finished.wait(timeoutMs)) { return MappingResult(); }
		return finished.first().first().value<MappingResult>();
	}

	//! A PortMapping whose gateway, address and UPnP search are all on 127.0.0.1, with a short PCP/NAT-PMP schedule.
	void aim(PortMapping& mapping)
	{
		PortMapping::TestEndpoints endpoints;
		endpoints.gateway = QHostAddress(QHostAddress::LocalHost);
		endpoints.gatewayPort = m_gateway.port();
		endpoints.local = QHostAddress(QHostAddress::LocalHost);
		endpoints.upnpSearch = QHostAddress(QHostAddress::LocalHost);
		endpoints.upnpSearchPort = m_router.searchPort();
		mapping.setTestEndpoints(endpoints);
		mapping.findChild<PmpMapper*>()->setRetrySchedule({50, 50});
	}

	MappingResult start(PortMapping& mapping, quint16 port)
	{
		QSignalSpy finished(&mapping, &PortMapping::finished);
		mapping.start(port);
		if (!finished.wait(10000)) { return MappingResult(); }
		return finished.first().first().value<MappingResult>();
	}

private slots:
	void initTestCase();

	void init();

	void aMappingIsMadeRenewedAndRemoved();

	//! The first AddPortMapping after a discovery must ask for the lease the caller asked for, not a fixed one.
	void theFirstMappingAfterDiscoveryAsksForTheRequestedLease();

	void aRouterThatRefusesIsReportedWithItsOwnWords();

	void aRouterThatOnlyTakesPermanentLeasesIsAskedAgainWithoutOne();

	void aDeviceThatIsNotAGatewayAndAnUnreadableDescriptionAreRefusedPlainly();

	void aRedirectedDescriptionIsRefusedNotFollowed();

	void noRouterAtAllEndsTheSearchWithAPlainMessage();

	void startMapsThroughNatPmpWhenTheGatewayAnswers();

	void aSilentGatewayFallsBackToUpnpAndStopRemovesTheMapping();

	//! stop() and start() again while the first AddPortMapping is still out: its stale answer must not make the router
	//! delete the mapping that the newer attempt is adding on the same port.
	void staleAddAfterStopAndRestartKeepsTheNewerMapping();

	//! With nothing newer running, a stale AddPortMapping answer is still taken back: stop() leaves no entry behind.
	void staleAddAfterStopIsTakenBack();

	void whenBothFailTheMessageNamesBothAndWhatToDo();

	void withNoGatewayItGoesStraightToUpnp();
};

#include "UpnpMapperFakeRouterTestParts1.h"

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
