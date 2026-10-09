/*
 * PortMappingCodec.cpp - the wire formats of automatic port mapping: NAT-PMP
 *                        (RFC 6886), PCP (RFC 6887), SSDP and UPnP IGD SOAP
 *                        (0.5.0 F6, WF-050/052)
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

#include "PortMappingCodec.h"

#include <QXmlStreamReader>

namespace lmms::portmap
{

namespace
{

constexpr quint8 PmpOpAddress = 0;
constexpr quint8 PmpOpMapTcp = 2;
constexpr quint8 PcpVersion = 2;
constexpr quint8 PcpOpMap = 1;
constexpr quint8 PcpResponseBit = 0x80;
constexpr quint8 TcpProtocol = 6;
constexpr int PcpHeaderBytes = 24;
constexpr int PcpMapBytes = 36;
constexpr int RtfGateway = 0x2;

void put16(QByteArray& out, quint16 value)
{
	out.append(static_cast<char>(value >> 8));
	out.append(static_cast<char>(value & 0xff));
}

void put32(QByteArray& out, quint32 value)
{
	put16(out, static_cast<quint16>(value >> 16));
	put16(out, static_cast<quint16>(value & 0xffff));
}

quint16 get16(const QByteArray& in, int at)
{
	return static_cast<quint16>((static_cast<quint8>(in.at(at)) << 8) | static_cast<quint8>(in.at(at + 1)));
}

quint32 get32(const QByteArray& in, int at)
{
	return (static_cast<quint32>(get16(in, at)) << 16) | get16(in, at + 2);
}

//! An IPv4 address as the 16-byte IPv4-mapped IPv6 form PCP carries (::ffff:a.b.c.d).
void putMapped(QByteArray& out, const QHostAddress& address)
{
	out.append(QByteArray(10, '\0'));
	out.append(QByteArray(2, '\xff'));
	put32(out, address.toIPv4Address());
}

QHostAddress mappedFrom(const QByteArray& in, int at)
{
	const QHostAddress address(reinterpret_cast<const quint8*>(in.constData() + at));
	bool isV4 = false;
	const quint32 v4 = address.toIPv4Address(&isV4);
	return isV4 ? QHostAddress(v4) : address;
}

bool decodePmpPayload(PmpReply& reply, const QByteArray& bytes)
{
	if (reply.opcode == (0x80 | PmpOpAddress))
	{
		if (bytes.size() < 12) { return false; }
		reply.externalAddress = QHostAddress(get32(bytes, 8));
	}
	if (reply.opcode == (0x80 | PmpOpMapTcp))
	{
		if (bytes.size() < 16) { return false; }
		reply.internalPort = get16(bytes, 8);
		reply.externalPort = get16(bytes, 10);
		reply.lifetime = get32(bytes, 12);
	}
	return true;
}

//! One /proc/net/route row's gateway when it is a default route, with its metric.
bool defaultRouteOf(const QList<QByteArray>& fields, quint32* gateway, int* metric)
{
	if (fields.size() < 7 || fields.at(1) != "00000000") { return false; }
	bool flagsOk = false;
	bool gatewayOk = false;
	const int flags = fields.at(3).toInt(&flagsOk, 16);
	const quint32 raw = fields.at(2).toUInt(&gatewayOk, 16);
	if (!flagsOk || !gatewayOk || (flags & RtfGateway) == 0 || raw == 0) { return false; }
	// The kernel prints the address in host byte order of a little-endian struct: byte 0 is the first octet.
	*gateway = ((raw & 0xff) << 24) | ((raw & 0xff00) << 8) | ((raw >> 8) & 0xff00) | (raw >> 24);
	*metric = fields.at(6).toInt();
	return true;
}

bool isWanService(const QString& type)
{
	return type.contains(QLatin1String(":service:WANIPConnection:")) || type.contains(QLatin1String(":service:WANPPPConnection:"));
}

bool isRouterHttpUrl(const QUrl& url, const QUrl& location)
{
	return url.isValid() && location.isValid() && !location.host().isEmpty()
		&& url.scheme().compare(QLatin1String("http"), Qt::CaseInsensitive) == 0
		&& url.scheme().compare(location.scheme(), Qt::CaseInsensitive) == 0
		&& url.host().compare(location.host(), Qt::CaseInsensitive) == 0;
}

struct DescriptionScan
{
	QString urlBase;
	QString currentType;
	QString currentControl;
	IgdService best;
	QString bestControl;
};

void finishService(DescriptionScan& scan)
{
	if (!isWanService(scan.currentType)) { return; }
	// WANIPConnection wins over WANPPPConnection; otherwise the first one found is kept.
	const bool better = !scan.best.serviceType.contains(QLatin1String("WANIPConnection"));
	if (scan.best.serviceType.isEmpty() || (better && scan.currentType.contains(QLatin1String("WANIPConnection"))))
	{
		scan.best.serviceType = scan.currentType;
		scan.bestControl = scan.currentControl;
	}
}

void scanElement(QXmlStreamReader& reader, DescriptionScan& scan)
{
	const auto name = reader.name();  // QStringRef on Qt 5, QStringView on Qt 6
	if (name == QLatin1String("URLBase")) { scan.urlBase = reader.readElementText().trimmed(); }
	else if (name == QLatin1String("service")) { scan.currentType.clear(); scan.currentControl.clear(); }
	else if (name == QLatin1String("serviceType")) { scan.currentType = reader.readElementText().trimmed(); }
	else if (name == QLatin1String("controlURL")) { scan.currentControl = reader.readElementText().trimmed(); }
}

} // namespace

QByteArray encodePmpAddressRequest()
{
	return QByteArray("\x00\x00", 2);
}

QByteArray encodePmpMapRequest(quint16 internalPort, quint16 externalPort, quint32 lifetime)
{
	QByteArray out;
	out.append(static_cast<char>(0));
	out.append(static_cast<char>(PmpOpMapTcp));
	put16(out, 0);
	put16(out, internalPort);
	put16(out, externalPort);
	put32(out, lifetime);
	return out;
}

PmpReply decodePmpReply(const QByteArray& bytes)
{
	PmpReply reply;
	if (bytes.size() < 8 || bytes.at(0) != 0 || (static_cast<quint8>(bytes.at(1)) & 0x80) == 0) { return reply; }
	reply.opcode = static_cast<quint8>(bytes.at(1));
	reply.result = get16(bytes, 2);
	reply.epoch = get32(bytes, 4);
	if (!decodePmpPayload(reply, bytes)) { return reply; }
	reply.valid = true;
	return reply;
}

QString pmpResultText(quint16 result)
{
	static const char* const texts[] = {"success", "unsupported version", "not authorised (port mapping is disabled on "
		"the router)", "network failure (the router has no external address yet)", "out of resources",
		"unsupported opcode"};
	return result < 6 ? QString::fromLatin1(texts[result]) : QStringLiteral("error %1").arg(result);
}

QByteArray encodePcpMapRequest(const QByteArray& nonce, const QHostAddress& client, quint16 internalPort,
	quint16 externalPort, quint32 lifetime)
{
	QByteArray out;
	out.append(static_cast<char>(PcpVersion));
	out.append(static_cast<char>(PcpOpMap));
	put16(out, 0);
	put32(out, lifetime);
	putMapped(out, client);
	out.append(nonce.left(PcpNonceBytes).leftJustified(PcpNonceBytes, '\0'));
	out.append(static_cast<char>(TcpProtocol));
	out.append(QByteArray(3, '\0'));
	put16(out, internalPort);
	put16(out, externalPort);
	putMapped(out, QHostAddress(quint32{0}));
	return out;
}

PcpReply decodePcpReply(const QByteArray& bytes, const QByteArray& nonce)
{
	PcpReply reply;
	if (bytes.size() >= 4 && bytes.at(0) == 0)
	{
		reply.pmpOnly = get16(bytes, 2) == 1;  // a NAT-PMP gateway answering "unsupported version"
		return reply;
	}
	const bool shaped = bytes.size() >= PcpHeaderBytes + PcpMapBytes && static_cast<quint8>(bytes.at(0)) == PcpVersion;
	if (!shaped || static_cast<quint8>(bytes.at(1)) != (PcpResponseBit | PcpOpMap)) { return reply; }
	if (bytes.mid(PcpHeaderBytes, PcpNonceBytes) != nonce.left(PcpNonceBytes)) { return reply; }
	reply.result = static_cast<quint8>(bytes.at(3));
	reply.lifetime = get32(bytes, 4);
	reply.epoch = get32(bytes, 8);
	reply.externalPort = get16(bytes, PcpHeaderBytes + 18);
	reply.externalAddress = mappedFrom(bytes, PcpHeaderBytes + 20);
	reply.valid = true;
	return reply;
}

QString pcpResultText(quint8 result)
{
	static const char* const texts[] = {"success", "unsupported version", "not authorised", "malformed request",
		"unsupported opcode", "unsupported option", "malformed option", "network failure", "no resources",
		"unsupported protocol", "user exceeded quota", "cannot provide external address", "address mismatch",
		"excessive remote peers"};
	return result < 14 ? QString::fromLatin1(texts[result]) : QStringLiteral("error %1").arg(result);
}

QHostAddress gatewayFromProcRoute(const QByteArray& text)
{
	QHostAddress best;
	int bestMetric = 0;
	for (const QByteArray& line : text.split('\n'))
	{
		quint32 gateway = 0;
		int metric = 0;
		if (!defaultRouteOf(line.simplified().split(' '), &gateway, &metric)) { continue; }
		if (best.isNull() || metric < bestMetric) { best = QHostAddress(gateway); bestMetric = metric; }
	}
	return best;
}

QByteArray ssdpSearch(const QString& searchTarget)
{
	return QByteArrayLiteral("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ")
		+ searchTarget.toLatin1() + QByteArrayLiteral("\r\n\r\n");
}

QUrl ssdpLocation(const QByteArray& datagram)
{
	for (const QByteArray& line : datagram.split('\n'))
	{
		const qsizetype colon = line.indexOf(':');
		if (colon > 0 && line.left(colon).trimmed().toLower() == "location")
		{
			return QUrl(QString::fromLatin1(line.mid(colon + 1).trimmed()));
		}
	}
	return QUrl();
}

IgdService parseIgdDescription(const QByteArray& xml, const QUrl& location)
{
	QXmlStreamReader reader(xml);
	DescriptionScan scan;
	while (!reader.atEnd())
	{
		const QXmlStreamReader::TokenType token = reader.readNext();
		if (token == QXmlStreamReader::StartElement) { scanElement(reader, scan); }
		else if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("service")) { finishService(scan); }
	}
	if (scan.best.serviceType.isEmpty()) { return IgdService(); }
	const QUrl base = scan.urlBase.isEmpty() ? location : QUrl(scan.urlBase);
	const QUrl controlUrl = base.resolved(QUrl(scan.bestControl));
	if (!isRouterHttpUrl(controlUrl, location)) { return IgdService(); }
	scan.best.controlUrl = controlUrl;
	return scan.best;
}

QByteArray soapEnvelope(const QString& action, const QString& serviceType, const SoapArgs& args)
{
	QString body;
	for (const auto& [name, value] : args) { body += QStringLiteral("<%1>%2</%1>").arg(name, value.toHtmlEscaped()); }
	return QStringLiteral("<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%1 xmlns:u=\"%2\">%3</u:%1></s:Body>"
		"</s:Envelope>\r\n").arg(action, serviceType.toHtmlEscaped(), body).toUtf8();
}

QString soapValue(const QByteArray& xml, const QString& element)
{
	QXmlStreamReader reader(xml);
	while (!reader.atEnd())
	{
		if (reader.readNext() == QXmlStreamReader::StartElement && reader.name() == element)
		{
			return reader.readElementText().trimmed();
		}
	}
	return QString();
}

int soapErrorCode(const QByteArray& xml)
{
	return soapValue(xml, QStringLiteral("errorCode")).toInt();
}

bool isPublicIPv4(const QHostAddress& address)
{
	bool isV4 = false;
	const quint32 a = address.toIPv4Address(&isV4);
	if (!isV4) { return false; }
	const quint32 top = a >> 24;
	const bool reserved = top == 0 || top == 10 || top == 127 || top >= 224;
	const bool cgnat = (a & 0xffc00000) == 0x64400000;     // 100.64.0.0/10
	const bool linkLocal = (a & 0xffff0000) == 0xa9fe0000; // 169.254.0.0/16
	const bool private172 = (a & 0xfff00000) == 0xac100000; // 172.16.0.0/12
	const bool private192 = (a & 0xffff0000) == 0xc0a80000; // 192.168.0.0/16
	return !(reserved || cgnat || linkLocal || private172 || private192);
}

} // namespace lmms::portmap
