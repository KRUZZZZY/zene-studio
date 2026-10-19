/*
 * CollabSession.cpp - the instance's one collaboration session: hosting or
 *                     joined (the lifecycle half; the sync half is
 *                     CollabSessionSync.cpp) (0.5.0 F6, WF-050/052)
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

#include "CollabSession.h"

#include "CollabSessionAddress.h"

#include <QSysInfo>
#include <QTimer>

#include "CollabClient.h"
#include "CollabCommandTap.h"
#include "CollabHost.h"
#include "CollabInvite.h"
#include "CollabLink.h"
#include "ControlServer.h"
#include "PortMapping.h"

namespace lmms::collab
{

QString detail::lanInviteAddress(const QHostAddress& lan, ControlResult* refusal)
{
	if (lan.isNull())
	{
		if (refusal)
		{
			*refusal = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("no LAN address could be detected; connect to the LAN or use scope 'local' for this machine only"));
		}
		return QString();
	}
	return lan.toString();
}

namespace
{

ControlResult refused(const QString& message)
{
	return ControlResult::failure(ControlErrorKind::Refused, message);
}

QString defaultName(const QString& name)
{
	const QString trimmed = name.trimmed();
	return trimmed.isEmpty() ? QSysInfo::machineHostName() : trimmed;
}

//! This machine's LAN address, the one machines on the same network reach.
QString lanInviteAddress(const portmap::PortMapping& mapping, ControlResult* refusal)
{
	const QHostAddress lan = mapping.localAddress().isNull() ? portmap::PortMapping::lanAddress(QHostAddress()) : mapping.localAddress();
	return detail::lanInviteAddress(lan, refusal);
}

/*! The router's external address. \a strict (scope "public") refuses, with the reason, when there is none or it is
 *  not publicly reachable; otherwise (scope "auto") an address the internet cannot reach is passed over for the LAN one. */
QString mappedInviteAddress(const portmap::PortMapping& mapping, bool strict, ControlResult* refusal)
{
	const portmap::MappingResult& mapped = mapping.result();
	if (strict && mapping.busy())
	{
		*refusal = ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("the router is still being asked to open the "
			"port; try again in a few seconds, or ask for scope 'lan'"));
		return QString();
	}
	if (!mapped.ok || mapped.externalAddress.isNull())
	{
		if (strict) { *refusal = refused(mapped.message.isEmpty() ? QStringLiteral("no port mapping (hosted with map_port false)") : mapped.message); }
		return QString();
	}
	const bool reachable = portmap::isPublicIPv4(mapped.externalAddress);
	if (!reachable)
	{
		if (strict)
		{
			const QString reason = mapped.message.isEmpty()
				? QStringLiteral("the router reported an external address that is not publicly reachable") : mapped.message;
			*refusal = refused(QStringLiteral("%1; use scope 'lan' or resolve the upstream NAT").arg(reason));
		}
		return QString();
	}
	return mapped.externalAddress.toString();
}

QString modeName(CollabSession::Mode mode)
{
	switch (mode)
	{
	case CollabSession::Mode::Idle: return QStringLiteral("idle");
	case CollabSession::Mode::Hosting: return QStringLiteral("hosting");
	case CollabSession::Mode::Joined: return QStringLiteral("joined");
	}
	return QString();
}

} // namespace

CollabSession* CollabSession::instance()
{
	static CollabSession* session = [] {
		auto* created = new CollabSession();
		// Whatever route the process leaves by, a host closes its peers and removes its router mapping (the
		// NAT-PMP/PCP delete is one datagram; a UPnP delete is best effort, its one-hour lease is the backstop).
		ControlRegistry::instance()->addShutdownHook([created] { created->resetToIdle(); });
		return created;
	}();
	return session;
}

CollabSession::CollabSession() :
	m_watch(new QTimer(this)),
	m_resync(new QTimer(this))
{
	m_watch->setInterval(250);
	connect(m_watch, &QTimer::timeout, this, &CollabSession::pollJournal);
	m_resync->setSingleShot(true);
	connect(m_resync, &QTimer::timeout, this, [this] {
		if (m_host) { m_host->broadcastSnapshot(m_resyncReason); }
	});
}

CollabSession::~CollabSession()
{
	resetToIdle();
}

void CollabSession::setNotice(const QString& text)
{
	m_notice = text;
	emit changed();
}

void CollabSession::resetToIdle()
{
	CommandTap::clear();
	m_watch->stop();
	m_resync->stop();
	if (m_mapping) { m_mapping->stop(); }
	if (m_host) { m_host->stop(); }
	if (m_client) { m_client->leave(); }
	m_mapping.reset();
	m_host.reset();
	m_client.reset();
	m_dispatcher.reset();
	m_mode = Mode::Idle;
	m_autoResyncPending = false;
}

ControlResult CollabSession::host(quint16 port, bool mapPort, const QString& name)
{
	if (m_mode != Mode::Idle) { return refused(QStringLiteral("a session is already %1; collab.stop or collab.leave it first").arg(modeName(m_mode))); }
	const QString tls = tlsPskUnavailableReason();
	if (!tls.isEmpty()) { return refused(tls); }
	m_host = std::make_unique<CollabHost>();
	for (const CollabRole role : {CollabRole::Editor, CollabRole::Viewer, CollabRole::Remote}) { m_host->setKey(role, randomInviteKey()); }
	QString error;
	// The default port first; when another program holds it, any free port - the invite carries whichever it is.
	const bool bound = m_host->start(QHostAddress::Any, port, &error) || (port == DefaultPort && m_host->start(QHostAddress::Any, 0, &error));
	if (!bound) { m_host.reset(); return refused(error); }
	m_name = defaultName(name);
	m_dispatcher = std::make_unique<ControlServer>(ControlRegistry::instance());
	m_host->setSnapshotProvider([this](QString* why) { return writeSnapshot(why); });
	m_host->setRequestHandler([this](const QByteArray& line, CollabRole role, const QString& origin) {
		return handlePeerRequest(line, role, origin);
	});
	connect(m_host.get(), &CollabHost::peersChanged, this, &CollabSession::changed);
	connect(m_host.get(), &CollabHost::notice, this, &CollabSession::setNotice);
	m_mapping = std::make_unique<portmap::PortMapping>();
	connect(m_mapping.get(), &portmap::PortMapping::finished, this, [this](const portmap::MappingResult& result) { setNotice(result.message); });
	if (mapPort) { m_mapping->start(m_host->port()); }
	m_mode = Mode::Hosting;
	installTap();
	startWatch();
	setNotice(QStringLiteral("hosting on TCP port %1%2").arg(m_host->port())
		.arg(mapPort ? QStringLiteral("; asking the router to open it") : QStringLiteral(" (LAN only)")));
	return ControlResult::success(state());
}

ControlResult CollabSession::stop()
{
	if (m_mode != Mode::Hosting) { return refused(QStringLiteral("this instance is not hosting")); }
	const int peers = m_host->peerCount();
	resetToIdle();
	setNotice(QStringLiteral("stopped hosting; %1 peer(s) disconnected and the router mapping removed").arg(peers));
	return ControlResult::success({{QStringLiteral("stopped"), true}, {QStringLiteral("disconnected"), peers}});
}

QString CollabSession::inviteAddress(const QString& scope, QString* warning, ControlResult* refusal) const
{
	if (scope == QLatin1String("local")) { return QStringLiteral("127.0.0.1"); }
	if (scope == QLatin1String("lan")) { return lanInviteAddress(*m_mapping, refusal); }
	const QString mapped = mappedInviteAddress(*m_mapping, scope == QLatin1String("public"), refusal);
	if (!mapped.isEmpty() || scope == QLatin1String("public")) { return mapped; }
	const QString lan = lanInviteAddress(*m_mapping, refusal);
	if (lan.isEmpty()) { return lan; }
	*warning = QStringLiteral("a LAN invite: only machines on this network can use it");
	return lan;
}

ControlResult CollabSession::invite(CollabRole role, const QString& scope)
{
	if (m_mode != Mode::Hosting) { return refused(QStringLiteral("this instance is not hosting (collab.host first)")); }
	QString warning;
	ControlResult refusal;
	const QString address = inviteAddress(scope, &warning, &refusal);
	if (address.isEmpty()) { return refusal; }
	Invite invite;
	invite.address = QHostAddress(address);
	const portmap::MappingResult& mapped = m_mapping->result();
	invite.port = (mapped.ok && address == mapped.externalAddress.toString()) ? mapped.externalPort : m_host->port();
	invite.role = role;
	invite.key = m_host->key(role);
	return ControlResult::success({{QStringLiteral("invite"), invite.encode()}, {QStringLiteral("role"), roleName(role)},
		{QStringLiteral("address"), address}, {QStringLiteral("port"), invite.port}, {QStringLiteral("warning"), warning},
		{QStringLiteral("secret"), QStringLiteral("the invite carries the session key: share it only with the person it is for")}});
}

ControlResult CollabSession::join(const QString& inviteText, const QString& name)
{
	if (m_mode != Mode::Idle) { return refused(QStringLiteral("a session is already %1; collab.stop or collab.leave it first").arg(modeName(m_mode))); }
	Invite invite;
	QString error;
	if (!Invite::decode(inviteText, &invite, &error)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, error); }
	const QString tls = tlsPskUnavailableReason();
	if (!tls.isEmpty()) { return refused(tls); }
	m_name = defaultName(name);
	m_client = std::make_unique<CollabClient>();
	m_client->setSnapshotSink([this](const QByteArray& project, QString* why) { return applySnapshot(project, why); });
	m_client->setCommandSink([this](const QString& command, const QJsonObject& args, QString* why) {
		return applyCommand(command, args, why);
	});
	connect(m_client.get(), &CollabClient::stateChanged, this, &CollabSession::changed);
	connect(m_client.get(), &CollabClient::peersChanged, this, &CollabSession::changed);
	connect(m_client.get(), &CollabClient::notice, this, &CollabSession::setNotice);
	m_mode = Mode::Joined;
	installTap();
	startWatch();
	m_client->join(invite, m_name);
	return ControlResult::success(state());
}

ControlResult CollabSession::leave()
{
	if (m_mode != Mode::Joined) { return refused(QStringLiteral("this instance has not joined a session")); }
	resetToIdle();
	setNotice(QStringLiteral("left the session; the project stays open here as it was"));
	return ControlResult::success({{QStringLiteral("left"), true}});
}

ControlResult CollabSession::setRole(int peerId, CollabRole role)
{
	if (m_mode != Mode::Hosting) { return refused(QStringLiteral("only the host sets roles")); }
	if (!m_host->setPeerRole(peerId, role)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no peer %1").arg(peerId)); }
	return ControlResult::success(state());
}

ControlResult CollabSession::resync()
{
	if (m_mode == Mode::Hosting) { scheduleResync(QStringLiteral("asked for by the host"), 0); return ControlResult::success(state()); }
	if (m_mode != Mode::Joined || m_client->state() != CollabClient::State::Joined)
	{
		return refused(QStringLiteral("not connected to a session"));
	}
	m_client->requestResync(QStringLiteral("asked for by the peer"));
	return ControlResult::success(state());
}

QJsonObject CollabSession::hostState() const
{
	return QJsonObject{{QStringLiteral("name"), m_name}, {QStringLiteral("listening"), m_host->isListening()},
		{QStringLiteral("port"), m_host->port()}, {QStringLiteral("peers"), m_host->peersJson()},
		{QStringLiteral("seq"), static_cast<double>(m_host->sequence())}, {QStringLiteral("mapping"), m_mapping->toJson()}};
}

QJsonObject CollabSession::peerState() const
{
	return QJsonObject{{QStringLiteral("name"), m_name}, {QStringLiteral("state"), m_client->stateName()},
		{QStringLiteral("host"), m_client->hostAddress()}, {QStringLiteral("role"), roleName(m_client->role())},
		{QStringLiteral("peer_id"), m_client->peerId()}, {QStringLiteral("peers"), m_client->peers()},
		{QStringLiteral("seq"), static_cast<double>(m_client->sequence())}, {QStringLiteral("has_project"), m_client->hasSnapshot()}};
}

QJsonObject CollabSession::state() const
{
	QJsonObject result{{QStringLiteral("available"), true}, {QStringLiteral("mode"), modeName(m_mode)},
		{QStringLiteral("tls_backend"), tlsBackendName()}, {QStringLiteral("last_notice"), m_notice},
		{QStringLiteral("undo"), QStringLiteral("shared: undo and redo walk the host's history for everyone, and re-sync the peers")}};
	if (m_mode == Mode::Hosting) { result.insert(QStringLiteral("host"), hostState()); }
	if (m_mode == Mode::Joined) { result.insert(QStringLiteral("peer"), peerState()); }
	return result;
}

} // namespace lmms::collab
