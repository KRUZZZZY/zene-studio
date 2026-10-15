/*
 * PortMapping.h - automatic port mapping for a collaboration host: PCP and
 *                 NAT-PMP on UDP 5351, then UPnP IGD, implemented with Qt
 *                 (0.5.0 F6, WF-050/052)
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

#ifndef LMMS_PORT_MAPPING_H
#define LMMS_PORT_MAPPING_H

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include "PortMappingCodec.h"
#include "lmms_export.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QUdpSocket;
class PortMappingCodecTest;

namespace lmms::portmap
{

//! What one mapping attempt achieved, in words a person can act on.
struct MappingResult
{
	bool ok = false;
	QString method;  //!< "pcp", "nat-pmp" or "upnp"; empty when nothing worked
	QHostAddress externalAddress;
	quint16 externalPort = 0;
	quint32 lifetime = 0;  //!< seconds; 0 = permanent (UPnP only)
	QString message;
	QJsonObject toJson() const;
};

//! PCP, falling back to NAT-PMP, against one gateway. Every request is retried on a short schedule (RFC 6886
//! asks for 250 ms doubling; this stops after four tries, about four seconds).
class LMMS_EXPORT PmpMapper : public QObject
{
	Q_OBJECT
public:
	explicit PmpMapper(QObject* parent = nullptr);
	~PmpMapper() override;

	//! The gateway (tests point this at a fake one on 127.0.0.1).
	void setServer(const QHostAddress& gateway, quint16 port = PmpServerPort);
	void setClientAddress(const QHostAddress& local) { m_client = local; }
	void setRetrySchedule(const QList<int>& delaysMs) { m_schedule = delaysMs; }

	void map(quint16 internalPort, quint16 externalPort, quint32 lifetime);
	//! Best effort: sends the delete for whichever protocol mapped, once, and does not wait.
	void unmap();

signals:
	void finished(const lmms::portmap::MappingResult& result);

private:
	enum class Step { Idle, Pcp, PmpAddress, PmpMap };
	void sendCurrent();
	void onTimeout();
	void onReadyRead();
	void handle(const QByteArray& datagram);
	void handlePcp(const QByteArray& datagram);
	void handlePmp(const QByteArray& datagram);
	void advance(Step next);
	void finish(bool ok, const QString& message);

	QUdpSocket* m_socket;
	QTimer* m_timer;
	QHostAddress m_gateway;
	quint16 m_serverPort = PmpServerPort;
	QHostAddress m_client;
	QList<int> m_schedule{250, 500, 1000, 2000};
	int m_try = 0;
	Step m_step = Step::Idle;
	QByteArray m_nonce;
	quint16 m_internal = 0;
	quint16 m_external = 0;
	quint32 m_lifetime = 0;
	MappingResult m_result;
	QString m_pcpFailure;
};

//! UPnP IGD: SSDP discovery, the device description, then SOAP AddPortMapping and GetExternalIPAddress.
class LMMS_EXPORT UpnpMapper : public QObject
{
	Q_OBJECT
public:
	explicit UpnpMapper(QObject* parent = nullptr);
	~UpnpMapper() override;

	void map(quint16 internalPort, quint16 externalPort, quint32 lease, const QHostAddress& local);
	void unmap();

	/*! TEST SEAM. Where the SSDP search goes, and whether a router on a loopback address is accepted. The defaults
	 *  are the real ones - the SSDP multicast group on port 1900, LAN routers only - and nothing but C++ calls this:
	 *  no control command, Lua binding or setting reaches it. The tests point it at a socket on 127.0.0.1 so that
	 *  nothing they do leaves the machine. */
	void setSearchEndpoint(const QHostAddress& address, quint16 port, bool allowLoopbackRouter);

signals:
	void finished(const lmms::portmap::MappingResult& result);

private:
	friend struct UpnpMapperTestAccess;
	void onSsdpReadyRead();
	void onSearchTimeout();
	void fetchDescription(const QUrl& location);
	void onDescription(QNetworkReply* reply, const QUrl& location);
	void addMapping(quint32 lease);
	void onAdded(QNetworkReply* reply, quint32 lease);
	void onExternalAddress(QNetworkReply* reply);
	QNetworkReply* soap(const QString& action, const SoapArgs& args);
	void finish(bool ok, const QString& message);

	QUdpSocket* m_ssdp;
	QTimer* m_searchTimer;
	QNetworkAccessManager* m_http;
	IgdService m_service;
	QHostAddress m_local;
	QHostAddress m_searchAddress{QStringLiteral("239.255.255.250")};
	quint16 m_searchPort = 1900;  //!< SsdpPort
	bool m_allowLoopbackRouter = false;
	quint16 m_internal = 0;
	quint16 m_external = 0;
	bool m_searching = false;
	bool m_mapped = false;
	MappingResult m_result;
};

/*! The host's mapping: PCP/NAT-PMP first, then UPnP, then the plain answer that neither worked. Renews at
 *  half the lifetime and removes the mapping on stop(). Never verifies reachability from outside - that
 *  would need a server, which this design does not have - so a success means "the router said yes". */
class LMMS_EXPORT PortMapping : public QObject
{
	Q_OBJECT
public:
	explicit PortMapping(QObject* parent = nullptr);
	~PortMapping() override;

	void start(quint16 internalPort);
	void stop();
	bool busy() const { return m_busy; }
	const MappingResult& result() const { return m_result; }
	QHostAddress localAddress() const { return m_local; }
	QJsonObject toJson() const;

	//! The default gateway: /proc/net/route on Linux; elsewhere a guess (x.y.z.1 of the LAN address) that
	//! \a guessed reports, because Qt has no portable routing-table API.
	static QHostAddress discoverGateway(bool* guessed);
	//! This machine's address on the route toward \a gateway (the address peers on the LAN reach).
	static QHostAddress lanAddress(const QHostAddress& gateway);

	//! The addresses start() would otherwise discover or use from the real network.
	struct TestEndpoints
	{
		QHostAddress gateway;       //!< the PCP/NAT-PMP gateway
		quint16 gatewayPort = 0;    //!< ... and its port
		QHostAddress local;         //!< this machine's address, as the mapping names it
		QHostAddress upnpSearch;    //!< where the UPnP search goes
		quint16 upnpSearchPort = 0;
	};
	/*! TEST SEAM, C++ only (no command, Lua binding or setting reaches it). With endpoints set, start() uses them
	 *  instead of reading the routing table and instead of the multicast group, and accepts a router on a loopback
	 *  address, so a test can run the whole mapping against sockets on 127.0.0.1. */
	void setTestEndpoints(const TestEndpoints& endpoints);

signals:
	void finished(const lmms::portmap::MappingResult& result);

private:
	friend class ::PortMappingCodecTest;

	void onPmpFinished(const MappingResult& result);
	void onUpnpFinished(const MappingResult& result);
	void succeed(const MappingResult& result);
	void renew();

	PmpMapper* m_pmp;
	UpnpMapper* m_upnp;
	QTimer* m_renew;
	QHostAddress m_gateway;
	QHostAddress m_local;
	bool m_gatewayGuessed = false;
	TestEndpoints m_test;
	bool m_hasTestEndpoints = false;
	quint16 m_internal = 0;
	bool m_busy = false;
	QString m_pmpFailure;
	MappingResult m_result;
};

} // namespace lmms::portmap

#endif // LMMS_PORT_MAPPING_H
