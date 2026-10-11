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

/*! No router and no engine: a CollabHost and CollabClient talk over loopback with the real TLS stack, the
 *  snapshot provider and sinks stand in for the project, and the request handler stands in for the registry.
 *  What is proved: the right key joins and receives the snapshot first; a wrong key is refused before any
 *  application byte; a plaintext client reaches nothing; the host's broadcast and a peer's request both make
 *  the round trip; a role is bound to its key. */

#include "lmmsconfig.h"

#include <QtTest>

#ifdef ZENE_HAVE_COLLAB

#include <QJsonDocument>
#include <QTcpSocket>

#include <limits>

#include "CollabClient.h"
#include "CollabHost.h"
#include "CollabInvite.h"
#include "CollabLink.h"

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
};

} // namespace lmms::collab

namespace
{

const QByteArray ProjectBytes = QByteArrayLiteral("<?xml version=\"1.0\"?><lmms-project/>");

struct Received
{
	QList<QPair<QString, QJsonObject>> commands;
	QList<QByteArray> snapshots;
	QList<QByteArray> requests;
	QList<CollabRole> requestRoles;
};

} // namespace

class CollabTransportTest : public QObject
{
	Q_OBJECT
private:
	std::unique_ptr<CollabHost> m_host;
	QByteArray m_editorKey;
	Received m_got;

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

	void cleanup() { m_host.reset(); }

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
