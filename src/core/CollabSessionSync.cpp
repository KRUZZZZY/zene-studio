/*
 * CollabSessionSync.cpp - the collaboration session's sync half: the registry
 *                         tap, peer requests, snapshots and the watch for
 *                         edits no command explains (0.5.0 F6, WF-050/052)
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

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>

#include "CollabClient.h"
#include "CollabCommandTap.h"
#include "CollabHost.h"
#include "CollabLink.h"
#include "ConfigManager.h"
#include "ControlServer.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms::collab
{

namespace
{

//! Debounce for re-syncing after edits no command explains (a drag is many steps; one snapshot follows it).
constexpr int UntrackedResyncMs = 750;
//! Right after a snapshot loads, journal movement is the load settling, not an edit.
constexpr qint64 SnapshotGraceMs = 1500;
//! A peer asks for its own re-sync at most this often.
constexpr qint64 AutoResyncIntervalMs = 3000;

qint64 nowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

QByteArray refusalLine(const QJsonValue& id, const QString& message)
{
	const QJsonObject error{{QStringLiteral("kind"), controlErrorKindName(ControlErrorKind::Refused)},
		{QStringLiteral("message"), message}};
	return QJsonDocument(QJsonObject{{QStringLiteral("id"), id.isDouble() ? id : QJsonValue(-1)},
		{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}).toJson(QJsonDocument::Compact);
}

//! A joiner does not trust a host with its machine either: only what the host may stream is applied.
QString streamedRefusal(const QString& command, const QJsonObject& args)
{
	const ControlCommand* registered = ControlRegistry::instance()->command(command);
	if (registered == nullptr) { return QStringLiteral("no command '%1' in this build").arg(command); }
	const bool streamable = registered->mutating && registered->requiresDecl.isEmpty()
		&& hostBroadcast(command, true, true, args) == HostBroadcast::Stream;
	return streamable ? QString() : QStringLiteral("the host streamed '%1', which a peer does not apply").arg(command);
}

QString snapshotPathFor(const QString& hostLabel)
{
	QString label = hostLabel;
	for (QChar& c : label)
	{
		if (!c.isLetterOrNumber()) { c = QLatin1Char('-'); }
	}
	return ConfigManager::inst()->userProjectsDir() + QStringLiteral("collab/joined-%1.mmpz").arg(label);
}

} // namespace

void CollabSession::installTap()
{
	CommandTap::install(
		[this](const QString& id, bool mutating, const QJsonObject& args, ControlResult* result) {
			return intercept(id, mutating, args, result);
		},
		[this](const QString& id, const QJsonObject& args, bool mutating, bool changed) { observe(id, args, mutating, changed); });
}

bool CollabSession::intercept(const QString& id, bool mutating, const QJsonObject& args, ControlResult* result)
{
	if (m_mode != Mode::Joined) { return false; }
	const RouteDecision decision = peerRoute(id, mutating, args, m_client->role());
	if (decision.route == PeerRoute::Local) { return false; }
	if (decision.route == PeerRoute::Refuse)
	{
		*result = ControlResult::failure(ControlErrorKind::Refused, decision.reason);
		return true;
	}
	const bool ready = m_client->state() == CollabClient::State::Joined
		&& (m_client->hasSnapshot() || m_client->role() == CollabRole::Remote);
	const int request = ready ? m_client->request(id, args) : 0;
	*result = request == 0
		? ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("not in step with the host yet (joining, or disconnected)"))
		: ControlResult::success({{QStringLiteral("forwarded"), true}, {QStringLiteral("request"), request},
			{QStringLiteral("host"), m_client->hostAddress()},
			{QStringLiteral("note"), QStringLiteral("the host applies it and streams it back; its answer arrives as a notice")}});
	return true;
}

void CollabSession::observe(const QString& id, const QJsonObject& args, bool mutating, bool changed)
{
	if (m_mode == Mode::Hosting)
	{
		const HostBroadcast action = hostBroadcast(id, mutating, changed, args);
		if (action == HostBroadcast::Stream) { m_host->broadcastCommand(id, args, m_origin.isEmpty() ? m_name : m_origin); }
		if (action == HostBroadcast::Resync) { scheduleResync(id, 0); }
	}
	// Whatever the command did to the journal, a command did it: only movement between commands is untracked.
	recordFingerprint();
}

QByteArray CollabSession::handlePeerRequest(const QByteArray& line, CollabRole role, const QString& origin)
{
	const QByteArray trimmed = line.trimmed();
	if (trimmed.isEmpty())
	{
		QJsonParseError parseError;
		QJsonDocument::fromJson(trimmed, &parseError);
		const QString message = parseError.errorString().isEmpty()
			? QStringLiteral("malformed JSON request")
			: QStringLiteral("malformed JSON request: %1").arg(parseError.errorString());
		return QJsonDocument(QJsonObject{{QStringLiteral("id"), -1}, {QStringLiteral("ok"), false},
			{QStringLiteral("error"), QJsonObject{{QStringLiteral("kind"), controlErrorKindName(ControlErrorKind::InvalidArgs)},
				{QStringLiteral("message"), message}}}}).toJson(QJsonDocument::Compact);
	}
	const QJsonDocument document = QJsonDocument::fromJson(trimmed);
	// A line that is not a request object is answered by the dispatcher's own typed refusal.
	if (!document.isObject()) { return m_dispatcher->dispatchLine(line); }
	const QJsonObject request = document.object();
	const QString command = request.value(QStringLiteral("cmd")).toString();
	const QJsonObject args = request.value(QStringLiteral("args")).toObject();
	const QString refusal = hostRefusal(command, ControlRegistry::instance()->command(command), args, role);
	if (!refusal.isEmpty()) { return refusalLine(request.value(QStringLiteral("id")), refusal); }
	// The same path a control-socket client takes: schema validation, requires, readiness, the handler.
	m_origin = origin;
	const QByteArray reply = m_dispatcher->dispatchLine(line);
	m_origin.clear();
	return reply;
}

QByteArray CollabSession::writeSnapshot(QString* error) const
{
	Song* song = Engine::getSong();
	QTemporaryDir dir;
	if (song == nullptr || !dir.isValid()) { *error = QStringLiteral("no project to share"); return QByteArray(); }
	// saveProjectFile() writes the document and touches neither the file name nor the modified flag - the
	// path render.stems and the mastering run take for their temporary copies.
	const QString path = dir.filePath(QStringLiteral("snapshot.mmpz"));
	if (!song->saveProjectFile(path))
	{
		*error = song->saveRefusal().isEmpty() ? QStringLiteral("the project could not be written") : song->saveRefusal();
		return QByteArray();
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { *error = QStringLiteral("the project could not be written"); return QByteArray(); }
	// Refuse before reading the whole file into memory: the snapshot is base64-framed, so an oversized
	// project cannot fit the host's line limit anyway.
	const qint64 base64Size = ((file.size() + 2) / 3) * 4;
	if (base64Size + 64 >= MaxHostLineBytes)
	{
		*error = QStringLiteral("the project is too large to share (%1 MB; the limit is %2 MB)")
			.arg(file.size() / (1024 * 1024)).arg(MaxHostLineBytes / (1024 * 1024) * 3 / 4);
		return QByteArray();
	}
	return file.readAll();
}

bool CollabSession::applySnapshot(const QByteArray& project, QString* error)
{
	// Kept beside the user's projects, not in a temporary folder: the joined copy is the open project, and a
	// save while joined must land somewhere that still exists afterwards.
	const QString path = snapshotPathFor(m_client->hostAddress());
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(project) != project.size() || !file.commit())
	{
		*error = QStringLiteral("could not write %1").arg(path);
		return false;
	}
	ControlResult opened;
	{
		RemoteApplyScope applying;
		opened = ControlRegistry::instance()->invoke(QStringLiteral("project.open"), {{QStringLiteral("path"), path}});
	}
	m_graceUntilMs = nowMs() + SnapshotGraceMs;
	recordFingerprint();
	m_autoResyncPending = false;
	if (!opened.ok) { *error = opened.errorMessage; }
	return opened.ok;
}

bool CollabSession::applyCommand(const QString& command, const QJsonObject& args, QString* error)
{
	*error = streamedRefusal(command, args);
	if (!error->isEmpty()) { return false; }
	RemoteApplyScope applying;
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (!result.ok) { *error = result.errorMessage; }
	return result.ok;
}

void CollabSession::startWatch()
{
	recordFingerprint();
	m_watch->start();
}

QByteArray CollabSession::journalFingerprint() const
{
	const ProjectJournal* journal = Engine::projectJournal();
	if (journal == nullptr) { return QByteArray(); }
	return QByteArray::number(journal->topStepSerial()) + ':' + QByteArray::number(journal->undoDepth()) + ':'
		+ QByteArray::number(journal->redoDepth());
}

void CollabSession::recordFingerprint()
{
	m_fingerprint = journalFingerprint();
}

bool CollabSession::trackUntrackedEdit(const QByteArray& fingerprint)
{
	m_fingerprint = fingerprint;
	if (nowMs() < m_graceUntilMs) { return false; }
	if (m_mode == Mode::Hosting)
	{
		scheduleResync(QStringLiteral("an edit made outside the command surface"), UntrackedResyncMs);
		return false;
	}
	const bool joined = m_mode == Mode::Joined && m_client->hasSnapshot();
	if (!joined) { return false; }
	m_autoResyncPending = true;
	return true;
}

void CollabSession::pollJournal()
{
	const QByteArray now = journalFingerprint();
	const bool changed = now != m_fingerprint;
	if (!changed && !m_autoResyncPending) { return; }
	if (changed && !trackUntrackedEdit(now)) { return; }
	if (m_mode != Mode::Joined || !m_client->hasSnapshot()) { return; }
	const qint64 nowTime = nowMs();
	if (nowTime - m_lastAutoResyncMs < AutoResyncIntervalMs) { return; }
	m_autoResyncPending = false;
	m_lastAutoResyncMs = nowTime;
	m_client->requestResync(QStringLiteral("a local edit outside the command surface"));
	setNotice(QStringLiteral("That edit was made with a tool that does not go through commands yet, so it cannot be shared; "
		"your copy is being re-synced from the host."));
}

void CollabSession::scheduleResync(const QString& reason, int delayMs)
{
	m_resyncReason = reason;
	m_resync->start(delayMs);
}

} // namespace lmms::collab
