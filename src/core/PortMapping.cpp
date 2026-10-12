/*
 * PortMapping.cpp - automatic port mapping for a collaboration host: PCP and
 *                   NAT-PMP on UDP 5351, then UPnP IGD, implemented with Qt
 *                   (0.5.0 F6, WF-050/052)
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

#include <QFile>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

namespace lmms::portmap
{

namespace
{

constexpr quint32 PmpLifetimeS = 7200;
constexpr quint32 UpnpLeaseS = 3600;

//! The first up, non-loopback IPv4 address, preferring a private one (the LAN side of a home router).
QHostAddress firstLanInterfaceAddress()
{
	QHostAddress fallback;
	for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces())
	{
		const auto flags = iface.flags();
		if (!flags.testFlag(QNetworkInterface::IsUp) || flags.testFlag(QNetworkInterface::IsLoopBack)) { continue; }
		for (const QNetworkAddressEntry& entry : iface.addressEntries())
		{
			const QHostAddress ip = entry.ip();
			if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback()) { continue; }
			if (!isPublicIPv4(ip)) { return ip; }
			if (fallback.isNull()) { fallback = ip; }
		}
	}
	return fallback;
}

} // namespace

QHostAddress PortMapping::discoverGateway(bool* guessed)
{
	*guessed = false;
#ifdef Q_OS_LINUX
	QFile routes(QStringLiteral("/proc/net/route"));
	if (routes.open(QIODevice::ReadOnly))
	{
		const QHostAddress gateway = gatewayFromProcRoute(routes.read(64 * 1024));
		if (!gateway.isNull()) { return gateway; }
	}
#endif
	// Qt has no portable routing-table API. Home routers sit at .1 of the LAN's /24 far more often than not;
	// the guess is reported as one, and UPnP (which needs no gateway address) is tried whatever it finds.
	const QHostAddress lan = firstLanInterfaceAddress();
	if (lan.isNull()) { return QHostAddress(); }
	*guessed = true;
	return QHostAddress((lan.toIPv4Address() & 0xffffff00u) | 1u);
}

QHostAddress PortMapping::lanAddress(const QHostAddress& gateway)
{
	if (!gateway.isNull())
	{
		// A UDP "connect" sends nothing; it only asks the kernel which local address routes to the gateway.
		QUdpSocket probe;
		probe.connectToHost(gateway, PmpServerPort);
		if (probe.waitForConnected(200) && probe.localAddress().protocol() == QAbstractSocket::IPv4Protocol)
		{
			return probe.localAddress();
		}
	}
	return firstLanInterfaceAddress();
}

PortMapping::PortMapping(QObject* parent) :
	QObject(parent),
	m_pmp(new PmpMapper(this)),
	m_upnp(new UpnpMapper(this)),
	m_renew(new QTimer(this))
{
	m_renew->setSingleShot(true);
	connect(m_renew, &QTimer::timeout, this, &PortMapping::renew);
	connect(m_pmp, &PmpMapper::finished, this, &PortMapping::onPmpFinished);
	connect(m_upnp, &UpnpMapper::finished, this, &PortMapping::onUpnpFinished);
}

PortMapping::~PortMapping()
{
	stop();
}

void PortMapping::start(quint16 internalPort)
{
	stop();
	m_internal = internalPort;
	m_busy = true;
	m_pmpFailure.clear();
	m_gateway = discoverGateway(&m_gatewayGuessed);
	m_local = lanAddress(m_gateway);
	if (m_gateway.isNull())
	{
		m_pmpFailure = QStringLiteral("no default gateway was found");
		m_upnp->map(m_internal, m_internal, UpnpLeaseS, m_local);
		return;
	}
	m_pmp->setServer(m_gateway);
	m_pmp->setClientAddress(m_local);
	m_pmp->map(m_internal, m_internal, PmpLifetimeS);
}

void PortMapping::stop()
{
	m_renew->stop();
	m_pmp->unmap();
	m_upnp->unmap();
	m_busy = false;
	m_result = MappingResult();
}

void PortMapping::onPmpFinished(const MappingResult& result)
{
	if (result.ok) { succeed(result); return; }
	m_pmpFailure = result.message + (m_gatewayGuessed ? QStringLiteral(" (the gateway address was guessed)") : QString());
	m_upnp->map(m_internal, m_internal, UpnpLeaseS, m_local);
}

void PortMapping::onUpnpFinished(const MappingResult& result)
{
	if (result.ok) { succeed(result); return; }
	m_busy = false;
	m_result = MappingResult();
	m_result.message = QStringLiteral("your router didn't open the port (NAT-PMP/PCP: %1; UPnP: %2). Share a LAN invite, or "
		"forward TCP port %3 to %4 by hand in the router's settings").arg(m_pmpFailure, result.message)
		.arg(m_internal).arg(m_local.toString());
	emit finished(m_result);
}

void PortMapping::succeed(const MappingResult& result)
{
	m_busy = false;
	m_result = result;
	if (!isPublicIPv4(result.externalAddress))
	{
		m_result.message += QStringLiteral(" - but the router's own external address (%1) is not a public one: there is "
			"another NAT in front of it (carrier-grade NAT, or a second router), so peers on the internet still cannot reach "
			"this port. Share a LAN invite, or forward the port on the outer router too")
			.arg(result.externalAddress.isNull() ? QStringLiteral("unknown") : result.externalAddress.toString());
	}
	if (result.lifetime > 0)
	{
		const quint32 halfLifetime = result.lifetime / 2 + result.lifetime % 2;
		const quint32 renewalSeconds = qMax<quint32>(1, qMin<quint32>(halfLifetime, 3600));
		m_renew->start(static_cast<int>(renewalSeconds * 1000));
	}
	emit finished(m_result);
}

void PortMapping::renew()
{
	// The same request again extends the lease (RFC 6886 3.3; UPnP replaces its own entry).
	if (m_result.method == QLatin1String("upnp")) { m_upnp->map(m_internal, m_result.externalPort, UpnpLeaseS, m_local); }
	else { m_pmp->map(m_internal, m_result.externalPort, PmpLifetimeS); }
}

QJsonObject PortMapping::toJson() const
{
	QJsonObject json = m_result.toJson();
	json.insert(QStringLiteral("busy"), m_busy);
	json.insert(QStringLiteral("gateway"), m_gateway.isNull() ? QString() : m_gateway.toString());
	json.insert(QStringLiteral("gateway_guessed"), m_gatewayGuessed);
	json.insert(QStringLiteral("lan_address"), m_local.isNull() ? QString() : m_local.toString());
	json.insert(QStringLiteral("internal_port"), m_internal);
	return json;
}

} // namespace lmms::portmap
