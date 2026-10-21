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
	void init()
	{
		const QString reason = tlsPskUnavailableReason();
		if (!reason.isEmpty()) { QSKIP(qPrintable(reason)); }
		m_got = Received();
		m_host = std::make_unique<CollabHost>();
		m_editorKey = randomInviteKey();
		m_host->setKey(CollabRole::Editor, m_editorKey);
		m_host->setSnapshotProvider([](QString*) { return ProjectBytes; });
		m_host->setRequestHandler([this](const QByteArray& line, CollabRole role, const QString&) {
			m_got.requests.append(line);
			m_got.requestRoles.append(role);
			const int id = QJsonDocument::fromJson(line).object().value(QStringLiteral("id")).toInt();
			return QJsonDocument(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("ok"), true},
				{QStringLiteral("result"), QJsonObject{}}}).toJson(QJsonDocument::Compact);
		});
		QString error;
		QVERIFY2(m_host->start(QHostAddress(QHostAddress::LocalHost), 0, &error), qPrintable(error));
	}

	void cleanup()
	{
		if (m_session != nullptr && m_session->mode() == collab::CollabSession::Mode::Joined) { m_session->leave(); }
		if (m_session != nullptr && m_session->mode() == collab::CollabSession::Mode::Hosting) { m_session->stop(); }
		m_session = nullptr;
		if (m_engineStarted)
		{
			ControlRegistry::setReady(false);
			Engine::destroy();
			m_engineStarted = false;
		}
		m_host.reset();
	}

	//! A payload at the queue limit is refused because its framing newline would exceed the limit.
	void sendLineCountsItsFramingNewline()
	{
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress(QHostAddress::LocalHost), 0));
		auto* socket = new QueueReportingSslSocket(MaxQueuedBytes - 1);
		socket->connectToHost(QHostAddress(QHostAddress::LocalHost), server.serverPort());
		QVERIFY(socket->waitForConnected(5000));

		CollabLink link(socket, MaxPeerLineBytes);
		QSignalSpy closed(&link, &CollabLink::closed);
		link.sendLine(QByteArrayLiteral("x"));
		QCOMPARE(closed.size(), 1);
		QCOMPARE(closed.first().first().toString(), QStringLiteral("the other side stopped reading"));
	}

	//! The right key: the handshake completes, the peer is listed, and the FIRST thing it receives is the project.
	void rightKeyJoinsAndGetsTheSnapshotFirst()
	{
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Alice"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		QCOMPARE(m_got.snapshots.size(), 1);
		QCOMPARE(m_got.snapshots.first(), ProjectBytes);
		QVERIFY(m_got.commands.isEmpty());
		QTRY_COMPARE(m_host->peerCount(), 1);
		QTRY_COMPARE(m_host->peersJson().first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Alice"));
		QCOMPARE(client.role(), CollabRole::Editor);
	}

	//! A transport close removes the cached peer list and notifies its observers.
	void transportCloseClearsPeers()
	{
		CollabClient client;
		wire(client);
		QSignalSpy peersChanged(&client, &CollabClient::peersChanged);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Frank"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		QCOMPARE(client.peers().size(), 1);
		QVERIFY(client.peers().first().toObject().value(QStringLiteral("connected")).toBool());
		peersChanged.clear();

		const QList<CollabLink*> links = m_host->findChildren<CollabLink*>();
		QCOMPARE(links.size(), 1);
		links.first()->close(QStringLiteral("simulated host disconnect"));
		QTRY_COMPARE_WITH_TIMEOUT(client.state(), CollabClient::State::Failed, 10000);
		QVERIFY(client.peers().isEmpty());
		QCOMPARE(peersChanged.size(), 1);
	}

	//! A host goodbye removes the cached peer list and notifies its observers.
	void hostByeClearsPeers()
	{
		CollabClient client;
		wire(client);
		QSignalSpy peersChanged(&client, &CollabClient::peersChanged);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Grace"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		QCOMPARE(client.peers().size(), 1);
		QVERIFY(client.peers().first().toObject().value(QStringLiteral("connected")).toBool());
		peersChanged.clear();

		m_host->stop();
		QTRY_COMPARE_WITH_TIMEOUT(client.state(), CollabClient::State::Failed, 10000);
		QVERIFY(client.peers().isEmpty());
		QCOMPARE(peersChanged.size(), 1);
	}

	//! Pending handshakes reserve peer slots, even when later handshakes complete after other peers.
	void pendingHandshakesReservePeerCapacity()
	{
		std::vector<std::unique_ptr<QSslSocket>> pending;
		for (int i = 0; i < 2; ++i)
		{
			auto socket = std::make_unique<QSslSocket>();
			socket->setSslConfiguration(pskConfiguration());
			connect(socket.get(), &QSslSocket::preSharedKeyAuthenticationRequired, socket.get(), [this](auto* auth) {
				auth->setIdentity(QByteArrayLiteral("editor"));
				auth->setPreSharedKey(m_editorKey);
			});
			socket->connectToHost(QHostAddress(QHostAddress::LocalHost), m_host->port());
			QTRY_VERIFY_WITH_TIMEOUT(socket->state() == QAbstractSocket::ConnectedState, 5000);
			pending.push_back(std::move(socket));
		}

		std::vector<std::unique_ptr<CollabClient>> clients;
		for (int i = 0; i < 6; ++i)
		{
			auto client = std::make_unique<CollabClient>();
			wire(*client);
			client->join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("peer %1").arg(i));
			QTRY_VERIFY_WITH_TIMEOUT(joined(*client), 10000);
			clients.push_back(std::move(client));
		}

		CollabClient extra;
		wire(extra);
		extra.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("extra"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(extra) || extra.state() == CollabClient::State::Failed, 10000);

		for (const auto& socket : pending) { socket->startClientEncryption(); }
		QTRY_VERIFY_WITH_TIMEOUT(pending[0]->isEncrypted() && pending[1]->isEncrypted(), 10000);
		QTRY_VERIFY_WITH_TIMEOUT(m_host->peerCount() >= MaxPeers, 10000);
		QVERIFY2(m_host->peerCount() <= MaxPeers, qPrintable(QString::number(m_host->peerCount())));
		QCOMPARE(extra.state(), CollabClient::State::Failed);
	}

	//! A peer without a snapshot sink must not be told that the host's project was applied.
	void missingSnapshotSinkIsReportedAsAnApplicationFailure()
	{
		CollabClient client;
		client.setCommandSink([](const QString&, const QJsonObject&, QString*) { return true; });
		QSignalSpy applied(&client, &CollabClient::snapshotApplied);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("No snapshot sink"));
		QTRY_VERIFY_WITH_TIMEOUT(applied.size() == 1 || client.lastNotice().contains(QStringLiteral("snapshot sink")), 10000);
		QCOMPARE(applied.size(), 0);
		QVERIFY(!client.hasSnapshot());
		QVERIFY(client.lastNotice().contains(QStringLiteral("snapshot sink")));
	}

	//! A peer without a command sink must not be told that a streamed command was applied.
	void missingCommandSinkIsReportedAsAnApplicationFailure()
	{
		CollabClient client;
		client.setSnapshotSink([](const QByteArray&, QString*) { return true; });
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("No command sink"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		QSignalSpy applied(&client, &CollabClient::commandApplied);
		m_host->broadcastCommand(QStringLiteral("track.add"), {}, QStringLiteral("host"));
		QTRY_VERIFY_WITH_TIMEOUT(applied.size() == 1 || client.lastNotice().contains(QStringLiteral("command sink")), 10000);
		QCOMPARE(applied.size(), 0);
		QVERIFY(client.lastNotice().contains(QStringLiteral("command sink")));
	}

	//! A wrong key fails the TLS handshake: no peer, no snapshot, and the reason names the key.
	void wrongKeyIsRefused()
	{
		CollabClient client;
		wire(client);
		QByteArray wrong = m_editorKey;
		wrong[0] = static_cast<char>(wrong.at(0) ^ 0x01);
		client.join(inviteFor(CollabRole::Editor, wrong), QStringLiteral("Mallory"));
		QTRY_COMPARE_WITH_TIMEOUT(client.state(), CollabClient::State::Failed, 10000);
		QCOMPARE(m_host->peerCount(), 0);
		QVERIFY(m_got.snapshots.isEmpty());
		QVERIFY2(client.lastNotice().contains(QStringLiteral("key")) || client.lastNotice().contains(QStringLiteral("closed")),
			qPrintable(client.lastNotice()));
	}

	//! The role is bound to its key: the host holds no remote key, so a remote claim cannot complete a handshake
	//! even with the editor's key.
	void aRoleTheHostDidNotKeyCannotJoin()
	{
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Remote, m_editorKey), QStringLiteral("Eve"));
		QTRY_COMPARE_WITH_TIMEOUT(client.state(), CollabClient::State::Failed, 10000);
		QCOMPARE(m_host->peerCount(), 0);
	}

	//! A failed streamed command invalidates this copy until the resync snapshot arrives.
	void failedCommandWaitsForReplacementSnapshot()
	{
		CollabClient client;
		QList<QString> attemptedCommands;
		quint64 sequenceDuringFailedCommand = 0;
		client.setSnapshotSink([this](const QByteArray& project, QString*) {
			m_got.snapshots.append(project);
			return true;
		});
		client.setCommandSink([&](const QString& command, const QJsonObject& args, QString* error) {
			Q_UNUSED(args);
			attemptedCommands.append(command);
			if (command == QStringLiteral("track.reject"))
			{
				sequenceDuringFailedCommand = client.sequence();
				m_host->broadcastCommand(QStringLiteral("track.wait"), {}, QStringLiteral("host"));
				*error = QStringLiteral("could not apply");
				return false;
			}
			return true;
		});
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Erin"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		const quint64 initialSequence = client.sequence();

		m_host->broadcastCommand(QStringLiteral("track.reject"), {}, QStringLiteral("host"));
		QTRY_VERIFY_WITH_TIMEOUT(m_got.snapshots.size() == 2 && client.hasSnapshot(), 10000);

		const bool failedSequenceWasNotCommitted = sequenceDuringFailedCommand == initialSequence;
		const bool laterCommandWasIgnored = attemptedCommands == QList<QString>{QStringLiteral("track.reject")};
		QVERIFY2(failedSequenceWasNotCommitted && laterCommandWasIgnored,
			qPrintable(QStringLiteral("failed command sequence was %1 (expected %2); attempted commands: %3")
				.arg(static_cast<qulonglong>(sequenceDuringFailedCommand))
				.arg(static_cast<qulonglong>(initialSequence)).arg(attemptedCommands.join(QStringLiteral(", ")))));
		QCOMPARE(client.sequence(), m_host->sequence());
	}

	//! A second failed streamed command inside the host's resync interval still gets a replacement snapshot.
	void throttledFailedCommandGetsReplacementSnapshot()
	{
		CollabClient client;
		int failedCommands = 0;
		client.setSnapshotSink([this](const QByteArray& project, QString*) {
			m_got.snapshots.append(project);
			return true;
		});
		client.setCommandSink([&](const QString&, const QJsonObject&, QString* error) {
			++failedCommands;
			*error = QStringLiteral("could not apply");
			return false;
		});
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Repeat sync"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);

		m_host->broadcastCommand(QStringLiteral("track.reject"), {}, QStringLiteral("host"));
		QTRY_COMPARE_WITH_TIMEOUT(m_got.snapshots.size(), 2, 5000);
		QVERIFY(client.hasSnapshot());
		QElapsedTimer retryWindow;
		retryWindow.start();
		m_host->broadcastCommand(QStringLiteral("track.reject"), {}, QStringLiteral("host"));
		QTRY_COMPARE_WITH_TIMEOUT(failedCommands, 2, 5000);
		QVERIFY(retryWindow.elapsed() < ResyncIntervalMs);
		QTRY_VERIFY_WITH_TIMEOUT(m_got.snapshots.size() == 3 && client.hasSnapshot(), ResyncIntervalMs + 1000);
	}

	//! A plaintext client that writes a request before (instead of) the handshake reaches nothing.
	void plaintextReachesNothing()
	{
		QTcpSocket raw;
		raw.connectToHost(QHostAddress(QHostAddress::LocalHost), m_host->port());
		QVERIFY(raw.waitForConnected(5000));
		raw.write("{\"id\":1,\"cmd\":\"control.quit\",\"args\":{}}\n");
		raw.flush();
		QTRY_COMPARE_WITH_TIMEOUT(raw.state(), QAbstractSocket::UnconnectedState, 15000);
		QVERIFY(m_got.requests.isEmpty());
		QCOMPARE(m_host->peerCount(), 0);
	}

	//! Stopping closes every pending handshake and leaves the restarted host able to admit a fresh peer.
	void stoppingHostAbortsPendingHandshakes()
	{
		std::array<QTcpSocket, MaxPendingHandshakes + 1> sockets;
		for (int i = 0; i < MaxPendingHandshakes; ++i)
		{
			sockets[static_cast<size_t>(i)].connectToHost(QHostAddress(QHostAddress::LocalHost), m_host->port());
			QVERIFY(sockets[static_cast<size_t>(i)].waitForConnected(5000));
		}
		sockets[MaxPendingHandshakes].connectToHost(QHostAddress(QHostAddress::LocalHost), m_host->port());
		QTRY_COMPARE_WITH_TIMEOUT(sockets[MaxPendingHandshakes].state(), QAbstractSocket::UnconnectedState, 5000);

		m_host->stop();
		for (int i = 0; i < MaxPendingHandshakes; ++i)
		{
			QTRY_COMPARE_WITH_TIMEOUT(sockets[static_cast<size_t>(i)].state(), QAbstractSocket::UnconnectedState, 5000);
		}

		QString error;
		QVERIFY2(m_host->start(QHostAddress(QHostAddress::LocalHost), 0, &error), qPrintable(error));
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Restarted"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		QCOMPARE(m_host->peerCount(), 1);
	}

	//! The host's broadcast reaches the peer in order, with its arguments, after the snapshot.
	void hostBroadcastRoundTrip()
	{
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Bob"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		const QJsonObject first{{QStringLiteral("type"), QStringLiteral("instrument")}};
		const QJsonObject second{{QStringLiteral("track"), QStringLiteral("trk-3")}, {QStringLiteral("name"), QStringLiteral("Bass")}};
		m_host->broadcastCommand(QStringLiteral("track.add"), first, QStringLiteral("host"));
		m_host->broadcastCommand(QStringLiteral("track.rename"), second, QStringLiteral("host"));
		QTRY_COMPARE(m_got.commands.size(), 2);
		QCOMPARE(m_got.commands.at(0).first, QStringLiteral("track.add"));
		QCOMPARE(m_got.commands.at(0).second, first);
		QCOMPARE(m_got.commands.at(1).first, QStringLiteral("track.rename"));
		QCOMPARE(m_got.commands.at(1).second, second);
		QCOMPARE(client.sequence(), m_host->sequence());
	}

	//! The broadcast implementation must snapshot ids and re-look up each peer
	//! after sendLine can synchronously erase it.
	void peerBroadcastUsesStableIdSnapshot()
	{
		QFile hostSource(QStringLiteral(COLLAB_HOST_SOURCE_PATH));
		QVERIFY2(hostSource.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(hostSource.errorString()));
		const QString source = QString::fromUtf8(hostSource.readAll());
		const qsizetype start = source.indexOf(QStringLiteral("int CollabHost::sendToProjectPeers("));
		QVERIFY(start >= 0);
		const qsizetype end = source.indexOf(QStringLiteral("\n}"), start);
		QVERIFY(end > start);
		const QString implementation = source.mid(start, end - start);
		QVERIFY(implementation.contains(QStringLiteral("m_peers.keys()")));
		QVERIFY(implementation.contains(QStringLiteral("m_peers.find(peerId)")));
		QVERIFY(!implementation.contains(QStringLiteral("std::as_const(m_peers)")));
	}

	//! A queue-limit close erases a peer synchronously while a host broadcast is
	//! walking the peer set; every affected peer must still be removed safely.
	void oversizedBroadcastCanClosePeersDuringIteration()
	{
		CollabClient first;
		CollabClient second;
		wire(first);
		wire(second);
		first.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("First"));
		second.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Second"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(first) && joined(second), 10000);
		QTRY_COMPARE(m_host->peerCount(), 2);

		// The first sendLine exceeds the real per-peer queue limit and synchronously
		// emits closed. onPeerClosed erases that entry and emits peersChanged before
		// this broadcast returns.
		const QString oversized(static_cast<int>(MaxQueuedBytes + 1), QLatin1Char('x'));
		m_host->broadcastCommand(oversized, {}, QStringLiteral("host"));
		QTRY_COMPARE_WITH_TIMEOUT(m_host->peerCount(), 0, 5000);
	}

	//! A client that cannot apply streamed commands during a nested event loop is disconnected at the backlog cap.
	void backlogOverflowDisconnectsBusyClient()
	{
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Slow reader"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);

		QEventLoop outerLoop;
		QEventLoop busyLoop;
		QTimer flood;
		flood.setInterval(1);
		int sent = 0;
		int busyLoopLevel = 0;
		connect(&flood, &QTimer::timeout, this, [&] {
			if (sent == 0) { busyLoopLevel = QThread::currentThread()->loopLevel(); }
			m_host->broadcastCommand(QStringLiteral("backlog.test"), QJsonObject(), QStringLiteral("host"));
			if (++sent == 1100)
			{
				flood.stop();
				QTimer::singleShot(1000, &busyLoop, &QEventLoop::quit);
			}
		});
		connect(&client, &CollabClient::stateChanged, &busyLoop, [&] {
			if (client.state() == CollabClient::State::Failed) { flood.stop(); busyLoop.quit(); }
		});
		QTimer::singleShot(0, &outerLoop, [&] {
			QTimer::singleShot(10000, &busyLoop, [&] { flood.stop(); busyLoop.quit(); });
			flood.start();
			busyLoop.exec();
			outerLoop.quit();
		});
		outerLoop.exec();

		QVERIFY(sent >= 1024);
		QVERIFY(busyLoopLevel > 1);
		QCOMPARE(client.state(), CollabClient::State::Failed);
		QVERIFY2(client.lastNotice().contains(QStringLiteral("backlog")), qPrintable(client.lastNotice()));
	}

	//! A nested host loop cannot retain unbounded request bytes even when every line is below the transport cap.
	void peerRequestBacklogHasByteBudget()
	{
		CollabClient holder;
		CollabClient flooder;
		wire(holder);
		wire(flooder);
		holder.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Holder"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(holder), 10000);
		flooder.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Flooder"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(flooder), 10000);

		QEventLoop outer;
		QEventLoop nested;
		connect(&flooder, &CollabClient::stateChanged, &outer, [&] {
			if (flooder.state() == CollabClient::State::Failed) { outer.quit(); }
		});
		connect(&flooder, &CollabClient::stateChanged, &nested, [&] {
			if (flooder.state() == CollabClient::State::Failed) { nested.quit(); }
		});
		const QString payload(900 * 1024, QLatin1Char('x'));
		const QByteArray requestLine = QJsonDocument(QJsonObject{{QStringLiteral("id"), 1},
			{QStringLiteral("cmd"), QStringLiteral("note.add")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("payload"), payload}}}})
			.toJson(QJsonDocument::Compact);
		QVERIFY(requestLine.size() < MaxPeerLineBytes);
		QVERIFY(requestLine.size() * 10 > 8 * 1024 * 1024);
		bool allSent = true;
		bool holdEntered = false;
		m_host->setRequestHandler([&](const QByteArray& line, CollabRole, const QString&) {
			m_got.requests.append(line);
			const QJsonObject request = QJsonDocument::fromJson(line).object();
			if (request.value(QStringLiteral("cmd")).toString() == QLatin1String("hold"))
			{
				holdEntered = true;
				int sent = 0;
				QTimer sender;
				sender.setInterval(20);
				connect(&sender, &QTimer::timeout, &nested, [&] {
					if (sent == 10) { sender.stop(); return; }
					allSent &= flooder.request(QStringLiteral("note.add"), {{QStringLiteral("payload"), payload}}) > 0;
					++sent;
				});
				sender.start();
				QTimer::singleShot(3000, &nested, &QEventLoop::quit);
				nested.exec();
				sender.stop();
			}
			const int id = request.value(QStringLiteral("id")).toInt();
			return QJsonDocument(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("ok"), true},
				{QStringLiteral("result"), QJsonObject{}}}).toJson(QJsonDocument::Compact);
		});

		int triggerId = 0;
		QTimer::singleShot(0, &outer, [&] { triggerId = holder.request(QStringLiteral("hold"), {}); });
		QTimer::singleShot(5000, &outer, &QEventLoop::quit);
		outer.exec();
		QVERIFY(triggerId > 0);
		QVERIFY(allSent);
		QVERIFY(holdEntered);
		QCOMPARE(m_host->peerCount(), 1);
		QCOMPARE(flooder.state(), CollabClient::State::Failed);
		QCOMPARE(m_got.requests.size(), 1);
	}

	//! A peer's request reaches the host's handler with the peer's role, and the reply comes back to it.
	void peerRequestRoundTrip()
	{
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Carol"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		QSignalSpy replies(&client, &CollabClient::replied);
		const int id = client.request(QStringLiteral("note.add"), {{QStringLiteral("clip"), QStringLiteral("clip-1")}});
		QVERIFY(id > 0);
		QTRY_COMPARE(replies.size(), 1);
		QCOMPARE(replies.first().at(0).toInt(), id);
		QVERIFY(replies.first().at(1).toBool());
		QCOMPARE(m_got.requests.size(), 1);
		QCOMPARE(m_got.requestRoles.first(), CollabRole::Editor);
		const QJsonObject sent = QJsonDocument::fromJson(m_got.requests.first()).object();
		QCOMPARE(sent.value(QStringLiteral("cmd")).toString(), QStringLiteral("note.add"));
	}

	//! Request IDs wrap to a positive value and skip an ID that is still in flight.
	void requestIdsWrapWithoutReusingInflightIds()
	{
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Carol"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		CollabClientTestAccess::prepareRequestIdWrap(client);

		QSignalSpy replies(&client, &CollabClient::replied);
		const int id = client.request(QStringLiteral("note.add"), {{QStringLiteral("clip"), QStringLiteral("clip-2")}});
		QCOMPARE(id, 2);
		QTRY_COMPARE(replies.size(), 1);
		QCOMPARE(replies.first().at(0).toInt(), id);
		QCOMPARE(m_got.requests.size(), 1);
		const QJsonObject sent = QJsonDocument::fromJson(m_got.requests.first()).object();
		QCOMPARE(sent.value(QStringLiteral("id")).toInt(), id);
		QVERIFY(CollabClientTestAccess::hasInflight(client, 1));
	}

	//! Requests arriving at the limit keep their original command association until the host replies.
	void reachingInflightLimitPreservesPendingRequestCommands()
	{
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Limit"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		m_host->setRequestHandler([](const QByteArray& line, CollabRole, const QString&) {
			const QJsonObject request = QJsonDocument::fromJson(line).object();
			return QJsonDocument(QJsonObject{{QStringLiteral("id"), request.value(QStringLiteral("id"))},
				{QStringLiteral("ok"), false}, {QStringLiteral("error"),
					QJsonObject{{QStringLiteral("message"), QStringLiteral("delayed rejection")}}}})
				.toJson(QJsonDocument::Compact);
		});
		QSignalSpy replies(&client, &CollabClient::replied);
		QSignalSpy notices(&client, &CollabClient::notice);
		const QString firstCommand = QStringLiteral("test.command.first");
		const int firstId = client.request(firstCommand, {});
		QVERIFY(firstId > 0);
		for (int i = 1; i < InflightRequestLimit; ++i)
		{
			QVERIFY(client.request(QStringLiteral("test.command.pending"), {}) > 0);
		}
		const int overflowId = client.request(QStringLiteral("test.command.overflow"), {});

		QTRY_VERIFY_WITH_TIMEOUT(!replies.isEmpty(), 10000);
		QCOMPARE(replies.first().at(0).toInt(), firstId);
		QVERIFY(!replies.first().at(1).toBool());
		QTRY_VERIFY_WITH_TIMEOUT(!notices.isEmpty(), 10000);
		QVERIFY2(notices.first().at(0).toString().contains(firstCommand),
			qPrintable(notices.first().at(0).toString()));
		QCOMPARE(overflowId, 0);
	}

	//! A later joiner gets the CURRENT project, and a re-sync reaches everyone who holds one.
	void snapshotOnJoinAndResync()
	{
		CollabClient first;
		wire(first);
		first.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("One"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(first), 10000);
		const QByteArray changed = ProjectBytes + QByteArrayLiteral("<!-- edited -->");
		m_host->setSnapshotProvider([changed](QString*) { return changed; });
		CollabClient second;
		wire(second);
		second.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Two"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(second), 10000);
		QCOMPARE(m_got.snapshots.last(), changed);
		m_host->broadcastSnapshot(QStringLiteral("test"));
		QTRY_COMPARE(m_got.snapshots.size(), 4);
		QCOMPARE(m_got.snapshots.at(2), changed);
		QCOMPARE(m_got.snapshots.at(3), changed);
	}

	//! A re-sync with no project-holding peers reports that, instead of claiming success.
	void broadcastSnapshotReportsRecipients()
	{
		QSignalSpy notices(m_host.get(), &CollabHost::notice);
		m_host->broadcastSnapshot(QStringLiteral("none"));
		QTRY_VERIFY(!notices.isEmpty());
		QVERIFY2(notices.last().at(0).toString().contains(QStringLiteral("no project peers")),
			qPrintable(notices.last().at(0).toString()));

		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Solo"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);
		notices.clear();
		m_host->broadcastSnapshot(QStringLiteral("one"));
		QTRY_VERIFY(!notices.isEmpty());
		QVERIFY2(notices.last().at(0).toString().contains(QStringLiteral("1 recipient(s)")),
			qPrintable(notices.last().at(0).toString()));
	}

	//! A blank collaboration line is answered with the malformed-wire id:-1 reply.
	void blankCollaborationLineReceivesMalformedReply()
	{
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Blank"));
		QTRY_VERIFY_WITH_TIMEOUT(joined(client), 10000);

		m_host->setRequestHandler([this](const QByteArray& line, CollabRole role, const QString&) {
			m_got.requests.append(line);
			if (line.trimmed().isEmpty())
			{
				return QJsonDocument(QJsonObject{{QStringLiteral("id"), -1}, {QStringLiteral("ok"), false},
					{QStringLiteral("error"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("invalid_args")},
						{QStringLiteral("message"), QStringLiteral("malformed JSON request")}}}}).toJson(QJsonDocument::Compact);
			}
			const int id = QJsonDocument::fromJson(line).object().value(QStringLiteral("id")).toInt();
			return QJsonDocument(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("ok"), true},
				{QStringLiteral("result"), QJsonObject{}}}).toJson(QJsonDocument::Compact);
		});

		QSignalSpy replies(&client, &CollabClient::replied);
		CollabClientTestAccess::link(client)->sendLine(QByteArray());
		QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 1, 5000);
		QCOMPARE(replies.first().at(0).toInt(), -1);
		QVERIFY(!replies.first().at(1).toBool());
		QVERIFY(m_got.requests.contains(QByteArray()));
	}

	//! An oversized snapshot is refused before it is base64-encoded into a frame.
	void oversizedSnapshotIsRefusedBeforeEncoding()
	{
		m_host->setSnapshotProvider([](QString*) { return QByteArray(MaxHostLineBytes + 1, 'x'); });
		QSignalSpy notices(m_host.get(), &CollabHost::notice);
		m_host->broadcastSnapshot(QStringLiteral("huge"));
		QTRY_VERIFY(!notices.isEmpty());
		QVERIFY2(notices.last().at(0).toString().contains(QStringLiteral("too large")),
			qPrintable(notices.last().at(0).toString()));
	}

	//! The size check must precede the base64 encoding in the host source.
	void snapshotSizeCheckPrecedesEncodingInSource()
	{
		QFile hostSource(QStringLiteral(COLLAB_HOST_SOURCE_PATH));
		QVERIFY2(hostSource.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(hostSource.errorString()));
		const QString source = QString::fromUtf8(hostSource.readAll());
		const qsizetype check = source.indexOf(QStringLiteral("base64Size + 64 >= MaxHostLineBytes"));
		const qsizetype encode = source.indexOf(QStringLiteral("project.toBase64()"));
		QVERIFY2(check >= 0, "early size check missing");
		QVERIFY2(encode > check, "encoding precedes the size check");
	}

	//! Blank lines must not be dropped before the request handler in the host source.
	void blankLinesAreDispatchedInSource()
	{
		QFile hostSource(QStringLiteral(COLLAB_HOST_SOURCE_PATH));
		QVERIFY2(hostSource.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(hostSource.errorString()));
		const QString source = QString::fromUtf8(hostSource.readAll());
		QVERIFY2(!source.contains(QStringLiteral("if (line.trimmed().isEmpty()) { return; }")),
			"blank lines are still dropped before dispatch");
	}
	//! An untracked edit seen during the peer's resync throttle is retried after the interval.
	void throttledUntrackedEditIsRetried()
	{
		Engine::init(true);
		m_engineStarted = true;
		ControlRegistry::setReady(true);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		Song* song = Engine::getSong();
		const int hostTempo = song->tempoModel().value();
		const int resyncTempo = hostTempo + 11;
		const int localTempo = hostTempo + 23;
		const QString initialPath = directory.filePath(QStringLiteral("host.mmpz"));
		const QString resyncPath = directory.filePath(QStringLiteral("resync.mmpz"));
		song->tempoModel().setValue(hostTempo);
		QVERIFY(song->saveProjectFile(initialPath));
		song->tempoModel().setValue(resyncTempo);
		QVERIFY(song->saveProjectFile(resyncPath));
		song->tempoModel().setValue(localTempo);
		Engine::projectJournal()->clearJournal();
		QFile initialFile(initialPath);
		QVERIFY(initialFile.open(QIODevice::ReadOnly));
		const QByteArray initialSnapshot = initialFile.readAll();
		QFile resyncFile(resyncPath);
		QVERIFY(resyncFile.open(QIODevice::ReadOnly));
		const QByteArray resyncSnapshot = resyncFile.readAll();
		int snapshotsSent = 0;
		m_host->setSnapshotProvider([&](QString*) {
			return snapshotsSent++ == 0 ? initialSnapshot : resyncSnapshot;
		});

		m_session = collab::CollabSession::instance();
		const ControlResult joined = m_session->join(inviteFor(CollabRole::Editor, m_editorKey).encode(), QStringLiteral("Sync test"));
		QVERIFY2(joined.ok, qPrintable(joined.errorMessage));
		QTRY_COMPARE_WITH_TIMEOUT(snapshotsSent, 1, 10000);
		QTRY_COMPARE_WITH_TIMEOUT(song->getTempo(), hostTempo, 10000);
		QTest::qWait(1600);  // The snapshot grace period has elapsed.

		Engine::projectJournal()->addJournalAction([] {}, [] {});
		QTRY_COMPARE_WITH_TIMEOUT(snapshotsSent, 2, 5000);
		QTRY_COMPARE_WITH_TIMEOUT(song->getTempo(), resyncTempo, 5000);  // The first resync snapshot arrived.
		QTest::qWait(1600);  // Expire snapshot grace while staying inside the three-second request interval.

		Engine::projectJournal()->addJournalAction([] {}, [] {});
		QTest::qWait(350);  // Let the journal watch observe it while the request is throttled.
		QCOMPARE(snapshotsSent, 2);
		QTRY_COMPARE_WITH_TIMEOUT(snapshotsSent, 3, 2500);
	}

	//! A complete line over the configured cap closes the link before it reaches the receiver.
	void completeOversizedLineIsRejected()
	{
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost, 0));
		auto socket = std::make_unique<QSslSocket>();
		socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
		QVERIFY(socket->waitForConnected(5000));
		QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
		auto peer = std::unique_ptr<QTcpSocket>(server.nextPendingConnection());
		CollabLink link(socket.release(), 4);
		QSignalSpy received(&link, &CollabLink::received);
		QSignalSpy closed(&link, &CollabLink::closed);

		QCOMPARE(peer->write(QByteArrayLiteral("12345\n")), qint64(6));
		QVERIFY(peer->waitForBytesWritten(5000));
		QTRY_VERIFY_WITH_TIMEOUT(!received.isEmpty() || !closed.isEmpty(), 5000);
		QCOMPARE(closed.size(), 1);
		QVERIFY(received.isEmpty());
		QCOMPARE(closed.first().first().toString(), QStringLiteral("a message was over the size limit"));
	}

	//! A host that cannot write its project turns the joiner away with the reason instead of a broken copy.
	void aSnapshotFailureEndsTheJoinWithTheReason()
	{
		m_host->setSnapshotProvider([](QString* error) { *error = QStringLiteral("disk full"); return QByteArray(); });
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Dave"));
		QTRY_COMPARE_WITH_TIMEOUT(client.state(), CollabClient::State::Failed, 10000);
		QVERIFY2(client.lastNotice().contains(QStringLiteral("disk full")), qPrintable(client.lastNotice()));
		QVERIFY(m_got.snapshots.isEmpty());
	}

	//! A negative sequence is not a position in the stream: the snapshot is refused and the client asks again.
	void rejectsNegativeSnapshotSequence()
	{
		const QByteArray key = randomInviteKey();
		CraftedPeer host(key);
		host.setInitialFrames({snapshotFrame(-1.0)});
		QVERIFY(host.start());
		CollabClient client;
		wire(client);
		QSignalSpy applied(&client, &CollabClient::snapshotApplied);
		Invite invite;
		invite.address = QHostAddress(QHostAddress::LocalHost);
		invite.port = host.port();
		invite.key = key;
		client.join(invite, QStringLiteral("Eve"));
		QTRY_VERIFY_WITH_TIMEOUT(host.requestedResync(), 1500);
		QVERIFY(!client.hasSnapshot());
		QVERIFY(m_got.snapshots.isEmpty());
		QCOMPARE(applied.size(), 0);
	}

	//! An out-of-range command sequence is refused before the command runs; the sequence the client holds is kept.
	void rejectsOutOfRangeCommandSequence()
	{
		const QByteArray key = randomInviteKey();
		CraftedPeer host(key);
		host.setInitialFrames({snapshotFrame(1.0)});
		QVERIFY(host.start());
		CollabClient client;
		wire(client);
		Invite invite;
		invite.address = QHostAddress(QHostAddress::LocalHost);
		invite.port = host.port();
		invite.key = key;
		client.join(invite, QStringLiteral("Frank"));
		QTRY_VERIFY_WITH_TIMEOUT(client.hasSnapshot(), 5000);
		QCOMPARE(client.sequence(), quint64(1));
		host.send({{QStringLiteral("type"), QStringLiteral("cmd")}, {QStringLiteral("seq"), 1e300},
			{QStringLiteral("cmd"), QStringLiteral("track.add")}, {QStringLiteral("args"), QJsonObject{}}});
		QTRY_VERIFY_WITH_TIMEOUT(host.requestedResync(), 1500);
		QVERIFY(m_got.commands.isEmpty());
		QCOMPARE(client.sequence(), quint64(1));
	}

	//! A failed promotion leaves a remote peer remote until the host can provide its project.
	void failedPromotionKeepsPeerRemote()
	{
		const QByteArray remoteKey = randomInviteKey();
		m_host->setKey(CollabRole::Remote, remoteKey);
		m_host->setSnapshotProvider([](QString* error) {
			*error = QStringLiteral("serialization failed");
			return QByteArray();
		});
		CollabClient client;
		wire(client);
		client.join(inviteFor(CollabRole::Remote, remoteKey), QStringLiteral("Remote"));
		QTRY_COMPARE_WITH_TIMEOUT(client.state(), CollabClient::State::Joined, 10000);
		QTRY_COMPARE(m_host->peerCount(), 1);
		const int peerId = m_host->peersJson().first().toObject().value(QStringLiteral("id")).toInt();

		QVERIFY(!m_host->setPeerRole(peerId, CollabRole::Editor));
		QCOMPARE(m_host->peersJson().first().toObject().value(QStringLiteral("role")).toString(),
			QStringLiteral("remote"));
		QTest::qWait(100);
		QCOMPARE(client.role(), CollabRole::Remote);
		QVERIFY(!client.hasSnapshot());
	}

	//! Snapshot refusal is distinct from a missing peer through the session API.
	void sessionReportsFailedPromotionAndKeepsPeerRemote()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		Engine::init(true);
		m_engineStarted = true;
		ControlRegistry::setReady(true);
		Song* song = Engine::getSong();
		const QString projectPath = directory.filePath(QStringLiteral("partial.mmp"));
		QVERIFY(song->saveProjectFile(projectPath));
		QFile projectFile(projectPath);
		QVERIFY(projectFile.open(QIODevice::ReadOnly));
		QByteArray project = projectFile.readAll();
		projectFile.close();
		const qsizetype closingSong = project.lastIndexOf("</song>");
		QVERIFY(closingSong >= 0);
		project.insert(closingSong, "<newsection/>");
		QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(projectFile.write(project), project.size());
		projectFile.close();
		song->loadProject(projectPath, {QStringLiteral("newsection")});
		QVERIFY(song->isPartialLoad());

		m_session = CollabSession::instance();
		const ControlResult hosted = m_session->host(0, false, QStringLiteral("Host"));
		QVERIFY2(hosted.ok, qPrintable(hosted.errorMessage));
		const ControlResult invitation = m_session->invite(CollabRole::Remote, QStringLiteral("local"));
		QVERIFY2(invitation.ok, qPrintable(invitation.errorMessage));
		Invite remoteInvite;
		QString decodeError;
		QVERIFY2(Invite::decode(invitation.result.value(QStringLiteral("invite")).toString(), &remoteInvite,
			&decodeError), qPrintable(decodeError));

		CollabClient client;
		wire(client);
		client.join(remoteInvite, QStringLiteral("Remote"));
		QTRY_COMPARE_WITH_TIMEOUT(client.state(), CollabClient::State::Joined, 10000);
		QTRY_COMPARE(m_session->state().value(QStringLiteral("host")).toObject()
			.value(QStringLiteral("peers")).toArray().size(), 1);
		const QJsonObject peer = m_session->state().value(QStringLiteral("host")).toObject()
			.value(QStringLiteral("peers")).toArray().first().toObject();

		const ControlResult refused = m_session->setRole(peer.value(QStringLiteral("id")).toInt(), CollabRole::Editor);
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY(refused.errorMessage.contains(QStringLiteral("newsection")));
		QCOMPARE(m_session->setRole(-1, CollabRole::Editor).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(m_session->state().value(QStringLiteral("host")).toObject().value(QStringLiteral("peers"))
			.toArray().first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("remote"));
		QTest::qWait(100);
		QCOMPARE(client.role(), CollabRole::Remote);
		QVERIFY(!client.hasSnapshot());
	}

	//! The join limiter: attempts and failures per address are bounded, and a ban expires.
	void joinLimiterBoundsAttemptsAndFailures()
	{
		JoinLimiter limiter;
		const QString a = QStringLiteral("198.51.100.7");
		for (int i = 0; i < JoinLimiter::MaxAttempts; ++i) { QVERIFY(limiter.admit(a, 1000)); }
		QVERIFY(!limiter.admit(a, 1000));
		QVERIFY(!limiter.admit(a, 1000 + JoinLimiter::BanMs - 1));
		QVERIFY(limiter.admit(a, 1000 + JoinLimiter::BanMs + JoinLimiter::WindowMs + 1));

		JoinLimiter failures;
		const QString b = QStringLiteral("198.51.100.8");
		for (int i = 0; i < JoinLimiter::MaxFailures; ++i) { QVERIFY(failures.admit(b, 50)); failures.recordFailure(b, 50); }
		QVERIFY(!failures.admit(b, 60));
		QVERIFY(failures.admit(QStringLiteral("198.51.100.9"), 60));
	}
};

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
