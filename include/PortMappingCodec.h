/*
 * PortMappingCodec.h - the wire formats of automatic port mapping: NAT-PMP
 *                      (RFC 6886), PCP (RFC 6887), SSDP and UPnP IGD SOAP
 *                      (0.5.0 F6, WF-050/052)
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

#ifndef LMMS_PORT_MAPPING_CODEC_H
#define LMMS_PORT_MAPPING_CODEC_H

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

#include "lmms_export.h"

//! Pure functions only - no socket here - so every format is unit-tested against recorded bytes (there is no
//! miniupnpc/libnatpmp dependency; these few hundred lines are the whole implementation).
namespace lmms::portmap
{

//! The gateway's NAT-PMP / PCP port.
constexpr quint16 PmpServerPort = 5351;
constexpr quint16 SsdpPort = 1900;

struct PmpReply
{
	bool valid = false;
	quint8 opcode = 0;     //!< 128 = external address, 130 = TCP mapping
	quint16 result = 0;    //!< 0 = success
	quint32 epoch = 0;
	QHostAddress externalAddress;
	quint16 internalPort = 0;
	quint16 externalPort = 0;
	quint32 lifetime = 0;
};

LMMS_EXPORT QByteArray encodePmpAddressRequest();
//! A TCP mapping request; lifetime 0 with external port 0 deletes the mapping.
LMMS_EXPORT QByteArray encodePmpMapRequest(quint16 internalPort, quint16 externalPort, quint32 lifetime);
LMMS_EXPORT PmpReply decodePmpReply(const QByteArray& bytes);
LMMS_EXPORT QString pmpResultText(quint16 result);

struct PcpReply
{
	bool valid = false;
	//! The gateway answered in NAT-PMP's format with "unsupported version": it speaks NAT-PMP, not PCP.
	bool pmpOnly = false;
	quint8 result = 0;  //!< 0 = success
	quint32 lifetime = 0;
	quint32 epoch = 0;
	quint16 externalPort = 0;
	QHostAddress externalAddress;
};

constexpr int PcpNonceBytes = 12;
//! A PCP MAP request for TCP from \a client (IPv4, sent as an IPv4-mapped IPv6 address).
LMMS_EXPORT QByteArray encodePcpMapRequest(const QByteArray& nonce, const QHostAddress& client, quint16 internalPort,
	quint16 externalPort, quint32 lifetime);
//! Invalid unless it is a MAP response carrying \a nonce (a reply to someone else's request is ignored).
LMMS_EXPORT PcpReply decodePcpReply(const QByteArray& bytes, const QByteArray& nonce);
LMMS_EXPORT QString pcpResultText(quint8 result);

//! The default gateway in /proc/net/route's text (lowest metric); null when there is none.
LMMS_EXPORT QHostAddress gatewayFromProcRoute(const QByteArray& text);

//! An SSDP M-SEARCH for \a searchTarget.
LMMS_EXPORT QByteArray ssdpSearch(const QString& searchTarget);
//! The LOCATION header of an SSDP response; invalid when absent.
LMMS_EXPORT QUrl ssdpLocation(const QByteArray& datagram);

struct IgdService
{
	QString serviceType;
	QUrl controlUrl;
	bool isValid() const { return !serviceType.isEmpty() && controlUrl.isValid(); }
};

//! The WANIPConnection (preferred) or WANPPPConnection service of an IGD description, resolved against
//! URLBase or \a location.
LMMS_EXPORT IgdService parseIgdDescription(const QByteArray& xml, const QUrl& location);

using SoapArgs = QList<QPair<QString, QString>>;
LMMS_EXPORT QByteArray soapEnvelope(const QString& action, const QString& serviceType, const SoapArgs& args);
//! The text of the first element named \a element (local name); empty when absent.
LMMS_EXPORT QString soapValue(const QByteArray& xml, const QString& element);
//! A UPnP fault's errorCode (0 when the body is not a fault).
LMMS_EXPORT int soapErrorCode(const QByteArray& xml);

//! False for private, carrier-grade-NAT (100.64/10), loopback, link-local and reserved IPv4 addresses: a
//! mapping on such an "external" address is behind another NAT and unreachable from the internet.
LMMS_EXPORT bool isPublicIPv4(const QHostAddress& address);

} // namespace lmms::portmap

#endif // LMMS_PORT_MAPPING_CODEC_H
