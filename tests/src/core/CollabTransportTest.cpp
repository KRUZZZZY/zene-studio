/*
 * CollabTransportTest.cpp - two in-process collaboration endpoints on
 *                           127.0.0.1 with TLS-PSK (0.5.0 F6, WF-050/052)
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

/*! No router: CollabHost and CollabClient talk over loopback with the real TLS stack, the snapshot provider and
 *  sinks stand in for the project, and the request handler stands in for the registry. One engine-backed case
 *  also proves the joined session retries an untracked edit seen inside its resync throttle. Other cases prove
 *  the right key joins and receives the snapshot first; a wrong key is refused before application bytes; plaintext
 *  reaches nothing; the host's broadcast and a peer's request round-trip; and a role is bound to its key. */

#include "lmmsconfig.h"

#include <QtTest>

#include <array>

#ifdef ZENE_HAVE_COLLAB

#include <QElapsedTimer>
#include <QFile>
#include <QEventLoop>
#include <QJsonDocument>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QSslSocket>
#include <QTcpServer>
#include <QSslPreSharedKeyAuthenticator>
#include <QSslSocket>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QScopeGuard>

#include "ConfigManager.h"

#include <vector>

#include <limits>

#include "CollabClient.h"
#include "CollabHost.h"
#include "CollabInvite.h"
#include "CollabLink.h"
#include "CollabSession.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::collab;

namespace lmms::collab
{

struct CollabClientTestAccess
{
	static void prepareRequestIdWrap(CollabClient& client)
	{
		client.m_nextRequest = std::numeric_limits<int>::max();
		client.m_inflight.insert(1, QStringLiteral("still pending"));
	}

	static bool hasInflight(const CollabClient& client, int id)
	{
		return client.m_inflight.contains(id);
	}

	static CollabLink* link(CollabClient& client)
	{
		return client.m_link;
	}
};

} // namespace lmms::collab

namespace
{

const QByteArray ProjectBytes = QByteArrayLiteral("<?xml version=\"1.0\"?><lmms-project/>");
constexpr int InflightRequestLimit = 256;

struct Received
{
	QList<QPair<QString, QJsonObject>> commands;
	QList<QByteArray> snapshots;
	QList<QByteArray> requests;
	QList<CollabRole> requestRoles;
};

class QueueReportingSslSocket final : public QSslSocket
{
public:
	explicit QueueReportingSslSocket(qint64 queuedBytes) : m_queuedBytes(queuedBytes) {}

	qint64 bytesToWrite() const override { return m_queuedBytes; }

private:
	qint64 m_queuedBytes;
};

QJsonObject snapshotFrame(double sequence)
{
	return {{QStringLiteral("type"), QStringLiteral("snapshot")},
		{QStringLiteral("data"), QString::fromLatin1(ProjectBytes.toBase64())},
		{QStringLiteral("bytes"), ProjectBytes.size()}, {QStringLiteral("seq"), sequence}};
}

//! A TLS-PSK host that sends hand-made frames, so a test can put values on the wire that a real host never sends.
class CraftedPeer : public QTcpServer
{
public:
	explicit CraftedPeer(QByteArray key) : m_key(std::move(key)) {}

	bool start() { return listen(QHostAddress(QHostAddress::LocalHost), 0); }
	quint16 port() const { return serverPort(); }
	void setInitialFrames(QList<QJsonObject> frames) { m_initialFrames = std::move(frames); }
	bool requestedResync() const { return m_requestedResync; }

	void send(const QJsonObject& message)
	{
		if (m_socket == nullptr || !m_socket->isEncrypted()) { return; }
		m_socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
	}

protected:
	void incomingConnection(qintptr descriptor) override
	{
		m_socket = new QSslSocket(this);
		if (!m_socket->setSocketDescriptor(descriptor)) { m_socket->deleteLater(); m_socket = nullptr; return; }
		m_socket->setSslConfiguration(pskConfiguration());
		connect(m_socket, &QSslSocket::preSharedKeyAuthenticationRequired, this,
			[this](QSslPreSharedKeyAuthenticator* authenticator) { authenticator->setPreSharedKey(m_key); });
		connect(m_socket, &QSslSocket::encrypted, this, [this] {
			send({{QStringLiteral("type"), QStringLiteral("welcome")}, {QStringLiteral("peer"), 1},
				{QStringLiteral("role"), QStringLiteral("editor")}, {QStringLiteral("proto"), 1}});
			for (const QJsonObject& frame : m_initialFrames) { send(frame); }
		});
		connect(m_socket, &QIODevice::readyRead, this, [this] {
			m_incoming.append(m_socket->readAll());
			qsizetype newline = -1;
			while ((newline = m_incoming.indexOf('\n')) >= 0)
			{
				const QJsonObject message = QJsonDocument::fromJson(m_incoming.left(newline)).object();
				m_incoming.remove(0, newline + 1);
				m_requestedResync |= message.value(QStringLiteral("type")).toString() == QLatin1String("resync");
			}
		});
		m_socket->startServerEncryption();
	}

private:
	QByteArray m_key;
	QList<QJsonObject> m_initialFrames;
	QSslSocket* m_socket = nullptr;
	QByteArray m_incoming;
	bool m_requestedResync = false;
};

} // namespace

class CollabTransportTest : public QObject
{
	Q_OBJECT
private:
	std::unique_ptr<CollabHost> m_host;
	QByteArray m_editorKey;
	Received m_got;
	collab::CollabSession* m_session = nullptr;
	bool m_engineStarted = false;

	Invite inviteFor(CollabRole role, const QByteArray& key) const
	{
		Invite invite;
		invite.address = QHostAddress(QHostAddress::LocalHost);
		invite.port = m_host->port();
		invite.role = role;
		invite.key = key;
		return invite;
	}

	void wire(CollabClient& client)
	{
		client.setSnapshotSink([this](const QByteArray& project, QString*) { m_got.snapshots.append(project); return true; });
		client.setCommandSink([this](const QString& command, const QJsonObject& args, QString*) {
			m_got.commands.append({command, args});
			return true;
		});
	}

	bool joined(CollabClient& client)
	{
		return client.state() == CollabClient::State::Joined && client.hasSnapshot();
	}

private slots:
	void init();

	void cleanup();

	//! A payload at the queue limit is refused because its framing newline would exceed the limit.
	void sendLineCountsItsFramingNewline();

	//! The right key: the handshake completes, the peer is listed, and the FIRST thing it receives is the project.
	void rightKeyJoinsAndGetsTheSnapshotFirst();

	//! A transport close removes the cached peer list and notifies its observers.
	void transportCloseClearsPeers();

	//! A host goodbye removes the cached peer list and notifies its observers.
	void hostByeClearsPeers();

	//! Pending handshakes reserve peer slots, even when later handshakes complete after other peers.
	void pendingHandshakesReservePeerCapacity();

	//! A peer without a snapshot sink must not be told that the host's project was applied.
	void missingSnapshotSinkIsReportedAsAnApplicationFailure();

	//! A peer without a command sink must not be told that a streamed command was applied.
	void missingCommandSinkIsReportedAsAnApplicationFailure();

	//! A wrong key fails the TLS handshake: no peer, no snapshot, and the reason names the key.
	void wrongKeyIsRefused();

	//! The role is bound to its key: the host holds no remote key, so a remote claim cannot complete a handshake
	//! even with the editor's key.
	void aRoleTheHostDidNotKeyCannotJoin();

	//! A failed streamed command invalidates this copy until the resync snapshot arrives.
	void failedCommandWaitsForReplacementSnapshot();

	//! A second failed streamed command inside the host's resync interval still gets a replacement snapshot.
	void throttledFailedCommandGetsReplacementSnapshot();

	//! A plaintext client that writes a request before (instead of) the handshake reaches nothing.
	void plaintextReachesNothing();

	//! Stopping closes every pending handshake and leaves the restarted host able to admit a fresh peer.
	void stoppingHostAbortsPendingHandshakes();

	//! A port that is already taken refuses the host with false; the error sink is optional, so nullptr must not crash.
	void occupiedPortRefusedWithoutErrorSink();

	//! The same refusal fills the error sink with the port that could not be bound.
	void occupiedPortReportsErrorToSink();

	//! The host's broadcast reaches the peer in order, with its arguments, after the snapshot.
	void hostBroadcastRoundTrip();

	//! The broadcast implementation must snapshot ids and re-look up each peer
	//! after sendLine can synchronously erase it.
	void peerBroadcastUsesStableIdSnapshot();

	//! A queue-limit close erases a peer synchronously while a host broadcast is
	//! walking the peer set; every affected peer must still be removed safely.
	void oversizedBroadcastCanClosePeersDuringIteration();

	//! A client that cannot apply streamed commands during a nested event loop is disconnected at the backlog cap.
	void backlogOverflowDisconnectsBusyClient();

	//! A nested host loop cannot retain unbounded request bytes even when every line is below the transport cap.
	void peerRequestBacklogHasByteBudget();

	//! A peer's request reaches the host's handler with the peer's role, and the reply comes back to it.
	void peerRequestRoundTrip();

	//! Request IDs wrap to a positive value and skip an ID that is still in flight.
	void requestIdsWrapWithoutReusingInflightIds();

	//! Requests arriving at the limit keep their original command association until the host replies.
	void reachingInflightLimitPreservesPendingRequestCommands();

	//! A later joiner gets the CURRENT project, and a re-sync reaches everyone who holds one.
	void snapshotOnJoinAndResync();

	//! A re-sync with no project-holding peers reports that, instead of claiming success.
	void broadcastSnapshotReportsRecipients();

	//! A blank collaboration line is answered with the malformed-wire id:-1 reply.
	void blankCollaborationLineReceivesMalformedReply();

	//! An oversized snapshot is refused before it is base64-encoded into a frame.
	void oversizedSnapshotIsRefusedBeforeEncoding();

	//! The early size check must preserve a project whose exact snapshot frame is below the wire limit.
	void snapshotFrameAcceptsPayloadThatFits();

	//! The size check must precede the base64 encoding in the host source.
	void snapshotSizeCheckPrecedesEncodingInSource();

	//! Huge projects are rejected before header work, and their byte counts never narrow to int.
	void snapshotSizesAreCheckedBeforeHeaderSerialization();

	//! A journal change during the snapshot grace window must remain visible after the window expires.
	void snapshotGraceDoesNotConsumeAnUntrackedEdit();

	//! Snapshot open and read failures must expose the QFile reason to the caller.
	void snapshotReadFailuresPreserveFileReason();

	//! Blank lines must not be dropped before the request handler in the host source.
	void blankLinesAreDispatchedInSource();
	//! An untracked edit seen during the peer's resync throttle is retried after the interval.
	void throttledUntrackedEditIsRetried();

	//! A complete line over the configured cap closes the link before it reaches the receiver.
	void completeOversizedLineIsRejected();

	//! A host that cannot write its project turns the joiner away with the reason instead of a broken copy.
	void aSnapshotFailureEndsTheJoinWithTheReason();

	//! A negative sequence is not a position in the stream: the snapshot is refused and the client asks again.
	void rejectsNegativeSnapshotSequence();

	//! An out-of-range command sequence is refused before the command runs; the sequence the client holds is kept.
	void rejectsOutOfRangeCommandSequence();

	//! A failed promotion leaves a remote peer remote until the host can provide its project.
	void failedPromotionKeepsPeerRemote();

	//! Snapshot refusal is distinct from a missing peer through the session API.
	void sessionReportsFailedPromotionAndKeepsPeerRemote();

	//! The join limiter: attempts and failures per address are bounded, and a ban expires.
	void joinLimiterBoundsAttemptsAndFailures();
};

#include "CollabTransportTestParts1.h"
#include "CollabTransportTestParts2.h"

QTEST_GUILESS_MAIN(CollabTransportTest)
#include "CollabTransportTest.moc"

#else // !ZENE_HAVE_COLLAB

class CollabTransportTest : public QObject
{
	Q_OBJECT
private slots:
	void compiledOut() { QSKIP("this build has no collaboration transport (ZENE_HAVE_COLLAB is not defined)"); }
};

QTEST_GUILESS_MAIN(CollabTransportTest)
#include "CollabTransportTest.moc"

#endif // ZENE_HAVE_COLLAB
