/*
 * CollabSession.h - the instance's one collaboration session: hosting or
 *                   joined, the registry tap, and the copies kept in step
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

#ifndef LMMS_COLLAB_SESSION_H
#define LMMS_COLLAB_SESSION_H

#include <memory>

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "CollabPolicy.h"
#include "ControlRegistry.h"
#include "lmms_export.h"

class QTimer;

namespace lmms
{
class ControlServer;
}

namespace lmms::portmap
{
class PortMapping;
}

namespace lmms::collab
{

class CollabClient;
class CollabHost;

/*! Off by default: nothing is created that listens, maps a port or opens a connection until host() or join()
 *  is called (the collab.* commands; in a GUI session collab.host also asks the person first).
 *
 *  Keeping the copies in step. Every edit made through a registered command is streamed by the host in its
 *  order. An edit made some other way - a GUI gesture that does not go through the registry yet - shows up
 *  as a journal step no command explains; the host then re-sends its whole project (debounced), and a peer
 *  asks for it, because a peer's such edit cannot be expressed to the host. Undo/redo are the HOST's history,
 *  shared by everyone (per-originator undo would need per-peer journals; it is not offered), and every undo
 *  re-syncs the peers with a snapshot rather than replaying it. */
class LMMS_EXPORT CollabSession : public QObject
{
	Q_OBJECT
public:
	enum class Mode
	{
		Idle,
		Hosting,
		Joined,  //!< connecting, joined, or failed and not yet left
	};

	static constexpr quint16 DefaultPort = 41740;

	static CollabSession* instance();

	ControlResult host(quint16 port, bool mapPort, const QString& name);
	ControlResult stop();
	//! \a scope: "auto", "public" (the router's mapping), "lan" or "local" (127.0.0.1, one machine).
	ControlResult invite(CollabRole role, const QString& scope);
	ControlResult join(const QString& inviteText, const QString& name);
	ControlResult leave();
	ControlResult setRole(int peerId, CollabRole role);
	ControlResult resync();

	Mode mode() const { return m_mode; }
	QJsonObject state() const;
	QString lastNotice() const { return m_notice; }

signals:
	void changed();

private:
	CollabSession();
	~CollabSession() override;

	// CollabSessionSync.cpp: the tap, the request handler, snapshots and the untracked-edit watch.
	void installTap();
	bool intercept(const QString& id, bool mutating, const QJsonObject& args, ControlResult* result);
	void observe(const QString& id, const QJsonObject& args, bool mutating, bool changed);
	QByteArray handlePeerRequest(const QByteArray& line, CollabRole role, const QString& origin);
	QByteArray writeSnapshot(QString* error) const;
	bool applySnapshot(const QByteArray& project, QString* error);
	bool applyCommand(const QString& command, const QJsonObject& args, QString* error);
	void startWatch();
	void pollJournal();
	bool trackUntrackedEdit(const QByteArray& fingerprint);
	void recordFingerprint();
	QByteArray journalFingerprint() const;
	void scheduleResync(const QString& reason, int delayMs);

	void setNotice(const QString& text);
	void resetToIdle();
	QJsonObject hostState() const;
	QJsonObject peerState() const;
	QString inviteAddress(const QString& scope, QString* warning, ControlResult* refusal) const;

	Mode m_mode = Mode::Idle;
	QString m_name;
	QString m_origin;  //!< the peer whose request is being applied (host side)
	QString m_resyncReason;
	QString m_notice;
	QByteArray m_fingerprint;
	qint64 m_graceUntilMs = 0;
	qint64 m_lastAutoResyncMs = 0;
	bool m_autoResyncPending = false;
	std::unique_ptr<CollabHost> m_host;
	std::unique_ptr<CollabClient> m_client;
	std::unique_ptr<portmap::PortMapping> m_mapping;
	std::unique_ptr<ControlServer> m_dispatcher;
	QTimer* m_watch;
	QTimer* m_resync;
};

} // namespace lmms::collab

#endif // LMMS_COLLAB_SESSION_H
