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

// Complete fixture method definitions; included once after the fixture declaration.

void CollabTransportTest::init() {
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
void CollabTransportTest::cleanup() {
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
void CollabTransportTest::sendLineCountsItsFramingNewline() {
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
void CollabTransportTest::rightKeyJoinsAndGetsTheSnapshotFirst() {
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
void CollabTransportTest::transportCloseClearsPeers() {
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
void CollabTransportTest::hostByeClearsPeers() {
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
void CollabTransportTest::pendingHandshakesReservePeerCapacity() {
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
void CollabTransportTest::missingSnapshotSinkIsReportedAsAnApplicationFailure() {
	CollabClient client;
	client.setCommandSink([](const QString&, const QJsonObject&, QString*) { return true; });
	QSignalSpy applied(&client, &CollabClient::snapshotApplied);
	client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("No snapshot sink"));
	QTRY_VERIFY_WITH_TIMEOUT(applied.size() == 1 || client.lastNotice().contains(QStringLiteral("snapshot sink")), 10000);
	QCOMPARE(applied.size(), 0);
	QVERIFY(!client.hasSnapshot());
	QVERIFY(client.lastNotice().contains(QStringLiteral("snapshot sink")));
}
void CollabTransportTest::missingCommandSinkIsReportedAsAnApplicationFailure() {
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
void CollabTransportTest::wrongKeyIsRefused() {
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
void CollabTransportTest::aRoleTheHostDidNotKeyCannotJoin() {
	CollabClient client;
	wire(client);
	client.join(inviteFor(CollabRole::Remote, m_editorKey), QStringLiteral("Eve"));
	QTRY_COMPARE_WITH_TIMEOUT(client.state(), CollabClient::State::Failed, 10000);
	QCOMPARE(m_host->peerCount(), 0);
}
void CollabTransportTest::failedCommandWaitsForReplacementSnapshot() {
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
void CollabTransportTest::throttledFailedCommandGetsReplacementSnapshot() {
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
void CollabTransportTest::plaintextReachesNothing() {
	QTcpSocket raw;
	raw.connectToHost(QHostAddress(QHostAddress::LocalHost), m_host->port());
	QVERIFY(raw.waitForConnected(5000));
	raw.write("{\"id\":1,\"cmd\":\"control.quit\",\"args\":{}}\n");
	raw.flush();
	QTRY_COMPARE_WITH_TIMEOUT(raw.state(), QAbstractSocket::UnconnectedState, 15000);
	QVERIFY(m_got.requests.isEmpty());
	QCOMPARE(m_host->peerCount(), 0);
}
void CollabTransportTest::stoppingHostAbortsPendingHandshakes() {
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
void CollabTransportTest::occupiedPortRefusedWithoutErrorSink() {
	QTcpServer occupier;
	QVERIFY(occupier.listen(QHostAddress(QHostAddress::LocalHost), 0));
	CollabHost host;
	QVERIFY(!host.start(QHostAddress(QHostAddress::LocalHost), occupier.serverPort(), nullptr));
	QVERIFY(!host.isListening());
}
void CollabTransportTest::occupiedPortReportsErrorToSink() {
	QTcpServer occupier;
	QVERIFY(occupier.listen(QHostAddress(QHostAddress::LocalHost), 0));
	CollabHost host;
	QString error;
	QVERIFY(!host.start(QHostAddress(QHostAddress::LocalHost), occupier.serverPort(), &error));
	QVERIFY2(error.contains(QString::number(occupier.serverPort())), qPrintable(error));
	QVERIFY(!host.isListening());
}
void CollabTransportTest::hostBroadcastRoundTrip() {
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
void CollabTransportTest::peerBroadcastUsesStableIdSnapshot() {
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
void CollabTransportTest::oversizedBroadcastCanClosePeersDuringIteration() {
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
void CollabTransportTest::backlogOverflowDisconnectsBusyClient() {
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
void CollabTransportTest::peerRequestBacklogHasByteBudget() {
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
