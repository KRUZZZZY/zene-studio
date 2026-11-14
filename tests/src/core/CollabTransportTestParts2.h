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

void CollabTransportTest::peerRequestRoundTrip() {
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
void CollabTransportTest::requestIdsWrapWithoutReusingInflightIds() {
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
void CollabTransportTest::reachingInflightLimitPreservesPendingRequestCommands() {
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
void CollabTransportTest::snapshotOnJoinAndResync() {
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
void CollabTransportTest::broadcastSnapshotReportsRecipients() {
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
void CollabTransportTest::blankCollaborationLineReceivesMalformedReply() {
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
void CollabTransportTest::oversizedSnapshotIsRefusedBeforeEncoding() {
	m_host->setSnapshotProvider([](QString*) { return QByteArray(MaxHostLineBytes + 1, 'x'); });
	QSignalSpy notices(m_host.get(), &CollabHost::notice);
	m_host->broadcastSnapshot(QStringLiteral("huge"));
	QTRY_VERIFY(!notices.isEmpty());
	QVERIFY2(notices.last().at(0).toString().contains(QStringLiteral("too large")),
		qPrintable(notices.last().at(0).toString()));
}
void CollabTransportTest::snapshotFrameAcceptsPayloadThatFits() {
	// With sequence 0, this payload produces a frame two bytes below MaxHostLineBytes.
	m_host->setSnapshotProvider([](QString*) { return QByteArray(50'331'606, 'x'); });
	QSignalSpy notices(m_host.get(), &CollabHost::notice);
	m_host->broadcastSnapshot(QStringLiteral("boundary"));
	QCOMPARE(notices.size(), 1);
	QVERIFY2(notices.last().at(0).toString().contains(QStringLiteral("no project peers")),
		qPrintable(notices.last().at(0).toString()));
}
void CollabTransportTest::snapshotSizeCheckPrecedesEncodingInSource() {
	QFile hostSource(QStringLiteral(COLLAB_HOST_SOURCE_PATH));
	QVERIFY2(hostSource.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(hostSource.errorString()));
	const QString source = QString::fromUtf8(hostSource.readAll());
	const qsizetype check = source.indexOf(QStringLiteral("base64Size + frameHeader.size() >= MaxHostLineBytes"));
	const qsizetype encode = source.indexOf(QStringLiteral("project.toBase64()"));
	QVERIFY2(check >= 0, "early size check missing");
	QVERIFY2(encode > check, "encoding precedes the size check");

	QFile sessionSyncSource(QStringLiteral(COLLAB_SESSION_SYNC_SOURCE_PATH));
	QVERIFY2(sessionSyncSource.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(sessionSyncSource.errorString()));
	const QString sessionSync = QString::fromUtf8(sessionSyncSource.readAll());
	const qsizetype fileCheck = sessionSync.indexOf(QStringLiteral("base64Size + frameHeader.size() >= MaxHostLineBytes"));
	const qsizetype readAll = sessionSync.indexOf(QStringLiteral("file.readAll();"));
	QVERIFY2(fileCheck >= 0, "temporary file size check missing");
	QVERIFY2(readAll > fileCheck, "temporary file is read before the size check");
}
void CollabTransportTest::snapshotSizesAreCheckedBeforeHeaderSerialization() {
	QFile hostSource(QStringLiteral(COLLAB_HOST_SOURCE_PATH));
	QVERIFY2(hostSource.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(hostSource.errorString()));
	const QString host = QString::fromUtf8(hostSource.readAll());
	const qsizetype hostGuard = host.indexOf(QStringLiteral("if (project.size() >= MaxHostLineBytes)"));
	const qsizetype hostHeader = host.indexOf(QStringLiteral("QJsonObject frameObject"));
	QVERIFY2(hostGuard >= 0, "host has no raw size guard before serializing frame metadata");
	QVERIFY2(hostHeader > hostGuard, "host serializes frame metadata before its raw size guard");
	QVERIFY2(host.contains(QStringLiteral("{QStringLiteral(\"bytes\"), project.size()}")),
		"host frame byte count does not preserve the project's wide size");
	QVERIFY2(!host.contains(QStringLiteral("static_cast<int>(project.size())")), "host narrows the frame byte count to int");

	QFile sessionSyncSource(QStringLiteral(COLLAB_SESSION_SYNC_SOURCE_PATH));
	QVERIFY2(sessionSyncSource.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(sessionSyncSource.errorString()));
	const QString sessionSync = QString::fromUtf8(sessionSyncSource.readAll());
	const qsizetype snapshotStart = sessionSync.indexOf(QStringLiteral("QByteArray CollabSession::writeSnapshot"));
	const qsizetype fileGuard = sessionSync.indexOf(QStringLiteral("if (fileSize >= MaxHostLineBytes)"), snapshotStart);
	const qsizetype fileHeader = sessionSync.indexOf(QStringLiteral("const QByteArray frameHeader"), snapshotStart);
	QVERIFY2(fileGuard > snapshotStart, "saved snapshot has no raw size guard");
	QVERIFY2(fileHeader > fileGuard, "saved snapshot serializes frame metadata before its raw size guard");
	QVERIFY2(sessionSync.contains(QStringLiteral("{QStringLiteral(\"bytes\"), fileSize}")),
		"saved snapshot frame byte count does not preserve the file's wide size");
	QVERIFY2(!sessionSync.contains(QStringLiteral("static_cast<int>(file.size())")), "saved snapshot narrows the frame byte count to int");
}
void CollabTransportTest::snapshotGraceDoesNotConsumeAnUntrackedEdit() {
	QFile sessionSyncSource(QStringLiteral(COLLAB_SESSION_SYNC_SOURCE_PATH));
	QVERIFY2(sessionSyncSource.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(sessionSyncSource.errorString()));
	const QString source = QString::fromUtf8(sessionSyncSource.readAll());
	const qsizetype trackStart = source.indexOf(QStringLiteral("bool CollabSession::trackUntrackedEdit"));
	const qsizetype trackEnd = source.indexOf(QStringLiteral("void CollabSession::pollJournal"), trackStart);
	QVERIFY2(trackStart >= 0 && trackEnd > trackStart, "untracked edit tracker is missing");
	const QString tracker = source.mid(trackStart, trackEnd - trackStart);
	const qsizetype graceCheck = tracker.indexOf(QStringLiteral("if (nowMs() < m_graceUntilMs)"));
	const qsizetype fingerprintUpdate = tracker.indexOf(QStringLiteral("m_fingerprint = fingerprint"));
	QVERIFY2(graceCheck >= 0, "snapshot grace check is missing");
	QVERIFY2(fingerprintUpdate > graceCheck, "the grace check consumes a fingerprint before it can be reconsidered");
}
void CollabTransportTest::snapshotReadFailuresPreserveFileReason() {
	QFile sessionSyncSource(QStringLiteral(COLLAB_SESSION_SYNC_SOURCE_PATH));
	QVERIFY2(sessionSyncSource.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(sessionSyncSource.errorString()));
	const QString source = QString::fromUtf8(sessionSyncSource.readAll());
	const qsizetype readStart = source.indexOf(QStringLiteral("QByteArray CollabSession::writeSnapshot"));
	const qsizetype readEnd = source.indexOf(QStringLiteral("bool CollabSession::applySnapshot"), readStart);
	QVERIFY2(readStart >= 0 && readEnd > readStart, "snapshot writer is missing");
	const QString writer = source.mid(readStart, readEnd - readStart);
	const qsizetype openFailure = writer.indexOf(QStringLiteral("if (!file.open(QIODevice::ReadOnly))"));
	const qsizetype openReason = writer.indexOf(QStringLiteral("file.errorString()"), openFailure);
	const qsizetype read = writer.indexOf(QStringLiteral("file.readAll()"));
	const qsizetype readFailure = writer.indexOf(QStringLiteral("file.error() != QFileDevice::NoError"), read);
	QVERIFY2(openFailure >= 0, "snapshot file open check is missing");
	QVERIFY2(openReason > openFailure, "snapshot open failure discards QFile's error reason");
	QVERIFY2(read >= 0 && readFailure > read, "snapshot read errors are not checked after readAll");
}
void CollabTransportTest::blankLinesAreDispatchedInSource() {
	QFile hostSource(QStringLiteral(COLLAB_HOST_SOURCE_PATH));
	QVERIFY2(hostSource.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(hostSource.errorString()));
	const QString source = QString::fromUtf8(hostSource.readAll());
	QVERIFY2(!source.contains(QStringLiteral("if (line.trimmed().isEmpty()) { return; }")),
		"blank lines are still dropped before dispatch");
}
void CollabTransportTest::throttledUntrackedEditIsRetried() {
	Engine::init(true);
	m_engineStarted = true;
	ControlRegistry::setReady(true);
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString oldWorkingDir = ConfigManager::inst()->workingDir();
	const auto restoreWorkingDir = qScopeGuard([oldWorkingDir] {
		ConfigManager::inst()->setWorkingDir(oldWorkingDir);
	});
	ConfigManager::inst()->setWorkingDir(directory.path());
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
void CollabTransportTest::completeOversizedLineIsRejected() {
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
void CollabTransportTest::aSnapshotFailureEndsTheJoinWithTheReason() {
	m_host->setSnapshotProvider([](QString* error) { *error = QStringLiteral("disk full"); return QByteArray(); });
	CollabClient client;
	wire(client);
	client.join(inviteFor(CollabRole::Editor, m_editorKey), QStringLiteral("Dave"));
	QTRY_COMPARE_WITH_TIMEOUT(client.state(), CollabClient::State::Failed, 10000);
	QVERIFY2(client.lastNotice().contains(QStringLiteral("disk full")), qPrintable(client.lastNotice()));
	QVERIFY(m_got.snapshots.isEmpty());
}
void CollabTransportTest::rejectsNegativeSnapshotSequence() {
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
void CollabTransportTest::rejectsOutOfRangeCommandSequence() {
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
void CollabTransportTest::failedPromotionKeepsPeerRemote() {
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
void CollabTransportTest::sessionReportsFailedPromotionAndKeepsPeerRemote() {
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
void CollabTransportTest::joinLimiterBoundsAttemptsAndFailures() {
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
