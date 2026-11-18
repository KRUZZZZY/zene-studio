/*
 * PortMappingPmp.cpp - PCP (RFC 6887) falling back to NAT-PMP (RFC 6886) on
 *                      UDP 5351 of the gateway (0.5.0 F6, WF-050/052)
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

#include <QJsonObject>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QTimer>
#include <QUdpSocket>

namespace lmms::portmap
{

QJsonObject MappingResult::toJson() const
{
	return QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("method"), method},
		{QStringLiteral("external_address"), externalAddress.isNull() ? QString() : externalAddress.toString()},
		{QStringLiteral("external_port"), externalPort}, {QStringLiteral("lifetime_s"), static_cast<double>(lifetime)},
		{QStringLiteral("reachable_from_internet"), ok && isPublicIPv4(externalAddress)},
		{QStringLiteral("verified_from_outside"), false}, {QStringLiteral("message"), message}};
}

PmpMapper::PmpMapper(QObject* parent) :
	QObject(parent),
	m_socket(new QUdpSocket(this)),
	m_timer(new QTimer(this))
{
	m_timer->setSingleShot(true);
	connect(m_timer, &QTimer::timeout, this, &PmpMapper::onTimeout);
	connect(m_socket, &QUdpSocket::readyRead, this, &PmpMapper::onReadyRead);
}

PmpMapper::~PmpMapper() = default;

void PmpMapper::setServer(const QHostAddress& gateway, quint16 port)
{
	m_gateway = gateway;
	m_serverPort = port;
}

void PmpMapper::map(quint16 internalPort, quint16 externalPort, quint32 lifetime)
{
	// A restart gets a new endpoint whenever the last attempt used one, whether it is still pending or was
	// canceled by unmap(), which leaves the socket bound. NAT-PMP replies have no transaction id, so keep the
	// old endpoint bound until the replacement has a different local port: the canceled attempt's late answers
	// are then addressed to a port that the new attempt does not hold.
	if (m_socket->state() == QAbstractSocket::BoundState)
	{
		m_timer->stop();
		const quint16 previousPort = m_socket->localPort();
		auto* replacement = new QUdpSocket(this);
		const bool bound = replacement->bind(QHostAddress(QHostAddress::AnyIPv4), 0);
		if (!bound || replacement->localPort() == previousPort)
		{
			const QString error = bound ? QStringLiteral("replacement reused local port %1").arg(previousPort)
				: replacement->errorString();
			delete replacement;
			m_socket->close();
			m_result = MappingResult();
			m_pcpFailure.clear();
			finish(false, QStringLiteral("could not bind a replacement UDP socket: %1").arg(error));
			return;
		}
		connect(replacement, &QUdpSocket::readyRead, this, &PmpMapper::onReadyRead);
		auto* previous = m_socket;
		m_socket = replacement;
		delete previous;
	}
	else if (!m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0))
	{
		const QString error = m_socket->errorString();
		m_result = MappingResult();
		m_pcpFailure.clear();
		finish(false, QStringLiteral("could not bind a UDP socket: %1").arg(error));
		return;
	}
	m_internal = internalPort;
	m_external = externalPort;
	m_lifetime = lifetime;
	m_result = MappingResult();
	m_pcpFailure.clear();
	m_nonce = QByteArray(PcpNonceBytes, Qt::Uninitialized);
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(m_nonce.data()), PcpNonceBytes / 4);
	advance(Step::Pcp);
}

void PmpMapper::unmap()
{
	const bool mapped = m_result.ok;
	const Step step = m_step;
	const bool pending = m_step != Step::Idle;
	const bool pcpMapping = m_result.method == QLatin1String("pcp");
	m_timer->stop();
	m_step = Step::Idle;
	if (mapped)
	{
		const QByteArray packet = pcpMapping
			? encodePcpMapRequest(m_nonce, m_client, m_internal, 0, 0)
			: encodePmpMapRequest(m_internal, 0, 0);
		m_socket->writeDatagram(packet, m_gateway, m_serverPort);
	}
	else if (pending)
	{
		// Only expire a mapping that this attempt could actually have created. A PCP attempt may have
		// reached the router; a NAT-PMP mapping is only possible once the map request itself is out.
		if (step == Step::Pcp)
		{
			m_socket->writeDatagram(encodePcpMapRequest(m_nonce, m_client, m_internal, 0, 0), m_gateway, m_serverPort);
		}
		else if (step == Step::PmpMap)
		{
			m_socket->writeDatagram(encodePmpMapRequest(m_internal, 0, 0), m_gateway, m_serverPort);
		}
	}
	m_result = MappingResult();
}

void PmpMapper::advance(Step next)
{
	m_step = next;
	m_try = 0;
	sendCurrent();
}

void PmpMapper::sendCurrent()
{
	QByteArray packet;
	switch (m_step)
	{
	case Step::Pcp: packet = encodePcpMapRequest(m_nonce, m_client, m_internal, m_external, m_lifetime); break;
	case Step::PmpAddress: packet = encodePmpAddressRequest(); break;
	case Step::PmpMap: packet = encodePmpMapRequest(m_internal, m_external, m_lifetime); break;
	case Step::Idle: return;
	}
	m_socket->writeDatagram(packet, m_gateway, m_serverPort);
	m_timer->start(m_schedule.value(m_try, 1000));
}

void PmpMapper::onTimeout()
{
	if (m_step == Step::Idle) { return; }
	if (++m_try < m_schedule.size()) { sendCurrent(); return; }
	if (m_step == Step::Pcp)
	{
		m_pcpFailure = QStringLiteral("no PCP answer");
		advance(Step::PmpAddress);
		return;
	}
	finish(false, QStringLiteral("the router did not answer PCP or NAT-PMP on %1 port %2 (it may be off in the router's "
		"settings)").arg(m_gateway.toString()).arg(m_serverPort));
}

void PmpMapper::onReadyRead()
{
	while (m_socket->hasPendingDatagrams())
	{
		const QNetworkDatagram datagram = m_socket->receiveDatagram(1100);
		// Only answers from the gateway's configured service count: any other host on the LAN, or another port
		// on the gateway, could send to this port.
		if (m_step != Step::Idle && datagram.senderAddress().isEqual(m_gateway, QHostAddress::TolerantConversion)
			&& datagram.senderPort() == m_serverPort)
		{
			handle(datagram.data());
		}
	}
}

void PmpMapper::handle(const QByteArray& datagram)
{
	if (m_step == Step::Pcp) { handlePcp(datagram); }
	else { handlePmp(datagram); }
}

void PmpMapper::handlePcp(const QByteArray& datagram)
{
	const PcpReply reply = decodePcpReply(datagram, m_nonce);
	if (reply.pmpOnly) { m_pcpFailure = QStringLiteral("the router speaks NAT-PMP only"); advance(Step::PmpAddress); return; }
	if (!reply.valid) { return; }
	if (reply.result != 0) { m_pcpFailure = pcpResultText(reply.result); advance(Step::PmpAddress); return; }
	m_result.method = QStringLiteral("pcp");
	m_result.externalAddress = reply.externalAddress;
	m_result.externalPort = reply.externalPort;
	m_result.lifetime = reply.lifetime;
	finish(true, QStringLiteral("the router opened TCP port %1 (PCP)").arg(reply.externalPort));
}

void PmpMapper::handlePmp(const QByteArray& datagram)
{
	const PmpReply reply = decodePmpReply(datagram);
	const quint8 expected = m_step == Step::PmpAddress ? 128 : 130;
	if (!reply.valid || reply.opcode != expected) { return; }
	if (reply.result != 0) { finish(false, QStringLiteral("NAT-PMP: %1").arg(pmpResultText(reply.result))); return; }
	if (m_step == Step::PmpAddress)
	{
		m_result.externalAddress = reply.externalAddress;
		advance(Step::PmpMap);
		return;
	}
	m_result.method = QStringLiteral("nat-pmp");
	m_result.externalPort = reply.externalPort;
	m_result.lifetime = reply.lifetime;
	finish(true, QStringLiteral("the router opened TCP port %1 (NAT-PMP)").arg(reply.externalPort));
}

void PmpMapper::finish(bool ok, const QString& message)
{
	m_timer->stop();
	m_step = Step::Idle;
	m_result.ok = ok;
	m_result.message = ok || m_pcpFailure.isEmpty() ? message : QStringLiteral("%1 (PCP: %2)").arg(message, m_pcpFailure);
	emit finished(m_result);
}

} // namespace lmms::portmap
