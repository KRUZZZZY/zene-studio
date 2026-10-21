/*
 * PortMappingUpnp.cpp - UPnP IGD port mapping: SSDP discovery, the device
 *                       description and SOAP over Qt Network
 *                       (0.5.0 F6, WF-050/052)
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

#include "PortMapping.h"

#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUdpSocket>

namespace lmms::portmap
{

namespace
{

constexpr int SearchMs = 2500;
constexpr int HttpTimeoutMs = 4000;
constexpr qint64 MaxDescriptionBytes = 256 * 1024;
//! UPnP's "OnlyPermanentLeasesSupported": the router wants lease 0.
constexpr int OnlyPermanentLeases = 725;

const char* const SearchTargets[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
	"urn:schemas-upnp-org:device:InternetGatewayDevice:2", "urn:schemas-upnp-org:service:WANIPConnection:1"};

//! A description is only fetched from a LAN address: an SSDP answer naming an internet host is not a router.
bool isLanHost(const QString& host, bool allowLoopback)
{
	const QHostAddress address(host);
	bool isV4 = false;
	address.toIPv4Address(&isV4);
	if (allowLoopback && isV4 && address.isLoopback()) { return true; }  // the test seam only
	return isV4 && !address.isLoopback() && !isPublicIPv4(address);
}

QString replyError(QNetworkReply* reply, const QByteArray& body)
{
	const QString upnp = soapValue(body, QStringLiteral("errorDescription"));
	const int code = soapErrorCode(body);
	if (code != 0) { return QStringLiteral("UPnP error %1%2").arg(code).arg(upnp.isEmpty() ? QString() : QStringLiteral(": ") + upnp); }
	return reply->errorString();
}


void sendDeleteMapping(QNetworkAccessManager* http, const IgdService& service, quint16 externalPort)
{
	QNetworkRequest request(service.controlUrl);
	request.setTransferTimeout(HttpTimeoutMs);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=\"utf-8\""));
	request.setRawHeader("SOAPAction", QStringLiteral("\"%1#DeletePortMapping\"").arg(service.serviceType).toLatin1());
	QNetworkReply* reply = http->post(request, soapEnvelope(QStringLiteral("DeletePortMapping"), service.serviceType,
		{{QStringLiteral("NewRemoteHost"), QString()}, {QStringLiteral("NewExternalPort"), QString::number(externalPort)},
			{QStringLiteral("NewProtocol"), QStringLiteral("TCP")}}));
	QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

} // namespace

UpnpMapper::UpnpMapper(QObject* parent) :
	QObject(parent),
	m_ssdp(new QUdpSocket(this)),
	m_searchTimer(new QTimer(this)),
	m_http(new QNetworkAccessManager(this))
{
	// A system HTTP proxy cannot reach the router on the LAN.
	m_http->setProxy(QNetworkProxy::NoProxy);
	m_searchTimer->setSingleShot(true);
	connect(m_searchTimer, &QTimer::timeout, this, &UpnpMapper::onSearchTimeout);
	connect(m_ssdp, &QUdpSocket::readyRead, this, &UpnpMapper::onSsdpReadyRead);
}

UpnpMapper::~UpnpMapper() = default;

void UpnpMapper::map(quint16 internalPort, quint16 externalPort, quint32 lease, const QHostAddress& local)
{
	++m_generation;
	m_active = true;
	m_internal = internalPort;
	m_external = externalPort;
	m_requestedLease = lease;
	m_local = local;
	m_result = MappingResult();
	if (m_service.isValid()) { addMapping(lease); return; }  // a renewal: the router is already known
	if (m_ssdp->state() != QAbstractSocket::BoundState)
	{
		// A search sent to a loopback endpoint (the test seam) is answered from, and heard on, loopback only.
		m_ssdp->bind(m_allowLoopbackRouter ? QHostAddress(QHostAddress::LocalHost) : QHostAddress(QHostAddress::AnyIPv4), 0);
	}
	for (const char* target : SearchTargets)
	{
		m_ssdp->writeDatagram(ssdpSearch(QString::fromLatin1(target)), m_searchAddress, m_searchPort);
	}
	m_searching = true;
	m_searchTimer->start(SearchMs);
}

void UpnpMapper::setSearchEndpoint(const QHostAddress& address, quint16 port, bool allowLoopbackRouter)
{
	m_searchAddress = address;
	m_searchPort = port;
	m_allowLoopbackRouter = allowLoopbackRouter;
}

void UpnpMapper::onSsdpReadyRead()
{
	while (m_ssdp->hasPendingDatagrams())
	{
		const QNetworkDatagram datagram = m_ssdp->receiveDatagram(4096);
		const QUrl location = ssdpLocation(datagram.data());
		if (!m_searching || !location.isValid() || !isLanHost(location.host(), m_allowLoopbackRouter)) { continue; }
		m_searching = false;
		m_searchTimer->stop();
		fetchDescription(location);
	}
}

void UpnpMapper::onSearchTimeout()
{
	if (!m_searching) { return; }
	m_searching = false;
	finish(false, QStringLiteral("no UPnP router answered on this network"));
}

void UpnpMapper::fetchDescription(const QUrl& location)
{
	const quint64 generation = m_generation;
	QNetworkRequest request(location);
	request.setTransferTimeout(HttpTimeoutMs);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	QNetworkReply* reply = m_http->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply, location, generation] {
		if (generation != m_generation) { reply->deleteLater(); return; }
		onDescription(reply, location);
	});
}

void UpnpMapper::onDescription(QNetworkReply* reply, const QUrl& location)
{
	reply->deleteLater();
	const QByteArray body = reply->read(MaxDescriptionBytes);
	if (reply->error() != QNetworkReply::NoError)
	{
		finish(false, QStringLiteral("the UPnP router's description could not be read (%1)").arg(reply->errorString()));
		return;
	}
	if (reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid())
	{
		finish(false, QStringLiteral("the UPnP router redirected its description to another URL"));
		return;
	}
	m_service = parseIgdDescription(body, location);
	if (!m_service.isValid()) { finish(false, QStringLiteral("the UPnP device that answered is not an internet gateway")); return; }
	addMapping(m_requestedLease);
}

QNetworkReply* UpnpMapper::soap(const QString& action, const SoapArgs& args)
{
	QNetworkRequest request(m_service.controlUrl);
	request.setTransferTimeout(HttpTimeoutMs);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=\"utf-8\""));
	request.setRawHeader("SOAPAction", QStringLiteral("\"%1#%2\"").arg(m_service.serviceType, action).toLatin1());
	return m_http->post(request, soapEnvelope(action, m_service.serviceType, args));
}

void UpnpMapper::addMapping(quint32 lease)
{
	const quint64 generation = m_generation;
	const IgdService service = m_service;
	const quint16 externalPort = m_external;
	QNetworkReply* reply = soap(QStringLiteral("AddPortMapping"), {{QStringLiteral("NewRemoteHost"), QString()},
		{QStringLiteral("NewExternalPort"), QString::number(m_external)}, {QStringLiteral("NewProtocol"), QStringLiteral("TCP")},
		{QStringLiteral("NewInternalPort"), QString::number(m_internal)},
		{QStringLiteral("NewInternalClient"), m_local.toString()}, {QStringLiteral("NewEnabled"), QStringLiteral("1")},
		{QStringLiteral("NewPortMappingDescription"), QStringLiteral("Zene Studio collaboration")},
		{QStringLiteral("NewLeaseDuration"), QString::number(lease)}});
	connect(reply, &QNetworkReply::finished, this, [this, reply, lease, generation, service, externalPort] {
		if (generation != m_generation)
		{
			if (m_active && externalPort == m_external)
			{
				if (reply->error() == QNetworkReply::NoError) { m_mapped = true; }
				reply->deleteLater();
				return;
			}
			reply->deleteLater();
			sendDeleteMapping(m_http, service, externalPort);
			return;
		}
		onAdded(reply, lease);
	});
}

void UpnpMapper::onAdded(QNetworkReply* reply, quint32 lease)
{
	reply->deleteLater();
	const QByteArray body = reply->read(MaxDescriptionBytes);
	const int code = soapErrorCode(body);
	if (code == OnlyPermanentLeases && lease != 0) { addMapping(0); return; }
	if (reply->error() != QNetworkReply::NoError || code != 0)
	{
		if (code == 0) { m_service = IgdService(); }
		finish(false, QStringLiteral("the router refused the UPnP mapping (%1)").arg(replyError(reply, body)));
		return;
	}
	m_mapped = true;
	m_result.method = QStringLiteral("upnp");
	m_result.externalPort = m_external;
	m_result.lifetime = lease;
	QNetworkReply* address = soap(QStringLiteral("GetExternalIPAddress"), {});
	const quint64 generation = m_generation;
	connect(address, &QNetworkReply::finished, this, [this, address, generation] {
		if (generation != m_generation) { address->deleteLater(); return; }
		onExternalAddress(address);
	});
}

void UpnpMapper::onExternalAddress(QNetworkReply* reply)
{
	reply->deleteLater();
	const QByteArray body = reply->read(MaxDescriptionBytes);
	if (reply->error() != QNetworkReply::NoError && soapErrorCode(body) == 0) { m_service = IgdService(); }
	m_result.externalAddress = QHostAddress(soapValue(body, QStringLiteral("NewExternalIPAddress")));
	finish(true, QStringLiteral("the router opened TCP port %1 (UPnP)").arg(m_external));
}

void UpnpMapper::unmap()
{
	++m_generation;
	m_active = false;
	m_searching = false;
	m_searchTimer->stop();
	if (!m_mapped) { return; }
	m_mapped = false;
	sendDeleteMapping(m_http, m_service, m_external);
}

void UpnpMapper::finish(bool ok, const QString& message)
{
	m_result.ok = ok;
	m_result.message = message;
	emit finished(m_result);
}

} // namespace lmms::portmap
