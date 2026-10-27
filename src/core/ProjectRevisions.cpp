/*
 * ProjectRevisions.cpp - the bounded previous-revision set of a project file
 *                       (SPEC A16 deliverable 4).
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
 */

#include "ProjectRevisions.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QUuid>

namespace lmms
{
namespace control
{

namespace
{

QString sha256OfFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Algorithm::Sha256);
	if (!hash.addData(&file)) { return QString(); }
	return QString::fromLatin1(hash.result().toHex());
}

//! Test seam: while set, the replacement step of copyRevision fails once its staged copy is complete,
//! so a test can prove the slot it would have replaced keeps its bytes. Never set outside tests.
std::atomic<bool> g_failReplacementForTest{false};

//! Moves the complete staged copy \a staged over \a to. std::filesystem::rename replaces an existing
//! file in one step (rename(2) on POSIX, MoveFileEx with replace on Windows), so \a to is never
//! absent: a failure leaves it exactly as it was.
bool replaceWithStaged(const QString& staged, const QString& to)
{
	if (g_failReplacementForTest.load()) { return false; }
#ifdef Q_OS_WIN
	// Wide paths: the narrow overload converts through the active ANSI code page, which replaces any
	// character that page cannot represent, so the rename would address the wrong path or fail.
	std::error_code ec;
	std::filesystem::rename(std::filesystem::path(staged.toStdWString()),
		std::filesystem::path(to.toStdWString()), ec);
	return !ec;
#else
	// rename(2) itself, which is what std::filesystem::rename is on POSIX: std::filesystem is unavailable
	// below macOS 10.15, the deployment target of the hosted macos-x86_64 job.
	return std::rename(QFile::encodeName(staged).constData(), QFile::encodeName(to).constData()) == 0;
#endif
}

//! Writes a copy of \a from into a new sibling named by \a pattern (its trailing XXXXXX becomes a unique
//! suffix) and keeps \a from's permissions. Returns the new file's path, or an empty string when the copy
//! failed, having removed whatever it had made. \a from is only read.
QString stageCopy(const QString& from, const QString& pattern)
{
	QFile in(from);
	if (!in.open(QIODevice::ReadOnly)) { return QString(); }
	const QByteArray bytes = in.readAll();
	if (in.error() != QFileDevice::NoError) { return QString(); }

	QTemporaryFile staged(pattern);
	staged.setAutoRemove(false);
	if (!staged.open()) { return QString(); }
	const QString path = staged.fileName();
	const bool written = staged.write(bytes) == bytes.size() && staged.flush();
	staged.close();
	if (!written || staged.error() != QFileDevice::NoError
		|| !QFile::setPermissions(path, QFileInfo(from).permissions()))
	{
		QFile::remove(path);
		return QString();
	}
	return path;
}

//! Copies \a from onto \a to, replacing it. The copy is bounded: a source
//! larger than the policy's per-revision cap is refused, never truncated.
//!
//! Luna cluster 5236 / 2026-10-08 rework: the destination is never removed before
//! the copy is known to be good. The bytes land in a uniquely named sibling
//! (QTemporaryFile, so the name is ours and cannot take a file that is not ours),
//! and that sibling is moved over \a to in one replacing rename. A failure at any
//! step leaves whatever was in \a to exactly as it was, and removes only the
//! staging file this call created.
bool copyRevision(const QString& from, const QString& to, bool* refused)
{
	const QFileInfo source(from);
	if (source.size() > ProjectRevisionPolicy::MaxRevisionBytes)
	{
		*refused = true;
		return false;
	}
	const QString staged = stageCopy(from, to + QStringLiteral(".rotate-XXXXXX"));
	if (staged.isEmpty()) { return false; }
	if (!replaceWithStaged(staged, to))
	{
		QFile::remove(staged);
		return false;
	}
	return true;
}

//! The staging file a restore of \a revision left behind, when an earlier
//! attempt failed after it took the revision out of the set: the fixed
//! '.restore' name the older code used, or one of the unique '.restore.<uuid>'
//! names. Empty when there is none.
QString pendingRestoreStagingPath(const QString& projectPath, int revision)
{
	const QString revisionPath = projectRevisionPath(projectPath, revision);
	const QString legacyStaging = revisionPath + QStringLiteral(".restore");
	if (QFileInfo(legacyStaging).isFile()) { return legacyStaging; }

	const QFileInfo revisionInfo(revisionPath);
	QDir directory(revisionInfo.absolutePath());
	// The revision's file name is matched LITERALLY, never as a wildcard: a
	// project file may hold glob characters ('song[1].mmp'), and a pattern
	// would read the brackets as a character class - the stage of such a
	// project would never be found, and the retry would stage the changed
	// revision slot instead.
	const QString prefix = revisionInfo.fileName() + QStringLiteral(".restore.");
	// Hidden files too: a project whose file name starts with a dot has hidden
	// revision and staging files, and the pending target is one of them.
	const QStringList candidates = directory.entryList(QDir::Files | QDir::Hidden, QDir::Name);
	for (const QString& name : candidates)
	{
		if (name.startsWith(prefix)) { return directory.absoluteFilePath(name); }
	}
	return QString();
}

//! The staging path of a NEW restore attempt: a path of its own, so a second
//! attempt never overwrites the bytes an earlier one staged.
QString newRestoreStagingPath(const QString& projectPath, int revision)
{
	return projectRevisionPath(projectPath, revision) + QStringLiteral(".restore.")
		+ QUuid::createUuid().toString(QUuid::WithoutBraces);
}

//! Stages the bytes a restore of \a revision will install, into \a staging: the pending stage an earlier
//! attempt left, or a fresh copy of the revision. The per-revision cap binds those bytes, so they are
//! checked BEFORE any copy is made and before the live file is rotated - a refusal after the rotation
//! would already have moved the retained set. False with \a error set when they are over the cap or
//! cannot be staged.
bool stageRestoreTarget(const QString& projectPath, int revision, QString* staging, QString* error)
{
	const QString source = projectRevisionPath(projectPath, revision);
	*staging = pendingRestoreStagingPath(projectPath, revision);
	const QString installed = staging->isEmpty() ? source : *staging;
	const qint64 size = QFileInfo(installed).size();
	if (size > ProjectRevisionPolicy::MaxRevisionBytes)
	{
		*error = QStringLiteral("%1 is %2 bytes, over the %3-byte per-revision cap of policy '%4': "
			"revision %5 was not restored, because a restore installs the whole file and the cap "
			"bounds every retained revision")
			.arg(installed).arg(size).arg(ProjectRevisionPolicy::MaxRevisionBytes)
			.arg(QStringLiteral("keep-%1").arg(ProjectRevisionPolicy::Keep)).arg(revision);
		return false;
	}
	if (!staging->isEmpty()) { return true; }

	*staging = newRestoreStagingPath(projectPath, revision);
	if (!QFile::copy(source, *staging))
	{
		*error = QStringLiteral("could not stage %1").arg(source);
		return false;
	}
	return true;
}

//! Drops the revisions past the policy's \c Keep slots.
bool trimRevisionSet(const QString& projectPath, QString* failedPath)
{
	bool removedAll = true;
	for (int revision = ProjectRevisionPolicy::Keep; ; ++revision)
	{
		const QString stale = projectRevisionPath(projectPath, revision);
		if (!QFileInfo::exists(stale)) { break; }
		if (!QFile::remove(stale))
		{
			if (removedAll) { *failedPath = stale; }
			removedAll = false;
		}
	}
	return removedAll;
}

//! Shifts every retained revision one slot up (rev(n-1) -> rev(n)), newest slot
//! last. False with \a refused set when a shift could not be made: the copy
//! left the destination backup intact, so reporting the refusal costs the
//! caller its claim of an inverse and nothing else.
bool shiftRevisionsUp(const QString& projectPath, bool* refused, QString* refusedReason)
{
	for (int revision = ProjectRevisionPolicy::Keep - 1; revision > 0; --revision)
	{
		const QString older = projectRevisionPath(projectPath, revision - 1);
		if (!QFileInfo::exists(older)) { continue; }
		// Luna cluster 5236: a shift that fails is a refusal, not something to
		// step over.
		bool shiftRefused = false;
		if (!copyRevision(older, projectRevisionPath(projectPath, revision), &shiftRefused))
		{
			*refused = true;
			*refusedReason = shiftRefused
				? QStringLiteral("could not shift %1 to revision %2: that file is over the "
					"%3-byte per-revision cap, so the retained set was left as it was")
					.arg(older).arg(revision).arg(ProjectRevisionPolicy::MaxRevisionBytes)
				: QStringLiteral("could not shift %1 to revision %2: the copy failed, so the "
					"retained set is incomplete - the revision it would have replaced is still "
					"there, but the older one was not kept").arg(older).arg(revision);
			return false;
		}
	}
	return true;
}

//! The number of revisions the policy's slots hold right now.
int countRetainedRevisions(const QString& projectPath)
{
	int kept = 0;
	for (int revision = 0; revision < ProjectRevisionPolicy::Keep; ++revision)
	{
		if (QFileInfo::exists(projectRevisionPath(projectPath, revision))) { ++kept; }
	}
	return kept;
}

} // namespace

void setRevisionReplacementFailureForTest(bool fail)
{
	g_failReplacementForTest.store(fail);
}

QString projectRevisionPath(const QString& projectPath, int revision)
{
	return projectPath + QStringLiteral(".rev") + QString::number(revision);
}

int rotateProjectRevision(const QString& projectPath, bool* refused, QString* refusedReason)
{
	*refused = false;
	refusedReason->clear();
	if (!QFileInfo::exists(projectPath))
	{
		// A first save has no previous revision; not a refusal, just nothing to
		// rotate.
		return 0;
	}

	const QFileInfo source(projectPath);
	if (source.size() > ProjectRevisionPolicy::MaxRevisionBytes)
	{
		*refused = true;
		*refusedReason = QStringLiteral("the project file is %1 bytes, over the %2-byte "
			"per-revision cap of policy '%3': no revision was kept, because a truncated "
			"project file is a corrupt revision rather than a recoverable one")
			.arg(source.size()).arg(ProjectRevisionPolicy::MaxRevisionBytes)
			.arg(QStringLiteral("keep-") + QString::number(ProjectRevisionPolicy::Keep));
		return 0;
	}

	// Shift rev(n-1) -> rev(n), newest slot last, then store the file about to
	// be replaced as rev0.
	if (!shiftRevisionsUp(projectPath, refused, refusedReason))
	{
		return 0;
	}
	bool ignored = false;
	if (!copyRevision(projectPath, projectRevisionPath(projectPath, 0), &ignored))
	{
		// Nothing of the file about to be replaced was kept: a refusal, so a save does not claim an
		// inverse and a restore does not go on to replace the live file.
		*refused = true;
		*refusedReason = QStringLiteral("could not keep %1 as revision 0: the copy failed, so no "
			"revision of the file about to be replaced was kept").arg(projectPath);
		return 0;
	}
	QString failedStalePath;
	if (!trimRevisionSet(projectPath, &failedStalePath))
	{
		*refused = true;
		*refusedReason = QStringLiteral("could not remove stale revision %1 while enforcing policy '%2'")
			.arg(failedStalePath, QStringLiteral("keep-")
				+ QString::number(ProjectRevisionPolicy::Keep));
	}

	return countRetainedRevisions(projectPath);
}

QJsonObject projectRevisionState(const QString& projectPath)
{
	QJsonArray revisions;
	qint64 totalBytes = 0;
	for (int revision = 0; revision < ProjectRevisionPolicy::Keep; ++revision)
	{
		const QString path = projectRevisionPath(projectPath, revision);
		QFileInfo info(path);
		if (!info.exists()) { continue; }
		QJsonObject entry;
		entry.insert(QStringLiteral("revision"), revision);
		entry.insert(QStringLiteral("path"), path);
		entry.insert(QStringLiteral("bytes"), static_cast<qint64>(info.size()));
		entry.insert(QStringLiteral("sha256"), sha256OfFile(path));
		revisions.append(entry);
		totalBytes += info.size();
	}

	QJsonObject out;
	out.insert(QStringLiteral("file"), projectPath);
	out.insert(QStringLiteral("policy"), QStringLiteral("keep-%1")
		.arg(ProjectRevisionPolicy::Keep));
	out.insert(QStringLiteral("revisions"), revisions);
	out.insert(QStringLiteral("count"), revisions.size());
	out.insert(QStringLiteral("retained_bytes"), totalBytes);
	out.insert(QStringLiteral("max_revision_bytes"),
		static_cast<qint64>(ProjectRevisionPolicy::MaxRevisionBytes));
	out.insert(QStringLiteral("max_total_bytes"),
		static_cast<qint64>(ProjectRevisionPolicy::Keep * ProjectRevisionPolicy::MaxRevisionBytes));
	return out;
}

bool restoreProjectRevision(const QString& projectPath, int revision, QString* error)
{
	error->clear();
	if (revision < 0 || revision >= ProjectRevisionPolicy::Keep)
	{
		*error = QStringLiteral("revision %1 is outside the '%2' policy's 0..%3")
			.arg(revision).arg(QStringLiteral("keep-%1")
				.arg(ProjectRevisionPolicy::Keep)).arg(ProjectRevisionPolicy::Keep - 1);
		return false;
	}
	const QString source = projectRevisionPath(projectPath, revision);
	if (!QFileInfo::exists(source))
	{
		*error = QStringLiteral("no revision %1 of %2 (the retained set is reported by "
			"project.get_state's revisions)").arg(revision).arg(projectPath);
		return false;
	}

	// Take the revision OUT of the set FIRST. Rotating the live file in (below)
	// shifts rev0 -> rev1, so reading the revision after the rotation would
	// restore the file the restore is meant to replace - measured: the file came
	// back as the revision it was supposed to discard. A staging file, not an
	// in-memory copy, because a revision may be up to the policy cap.
	//
	// A stage an earlier attempt left behind is REUSED: the rotation may
	// already have replaced the revision that was asked for, so the staged bytes
	// are then the only copy of the target left. Staging the revision slot
	// again would restore the file the rotation put there instead - the bytes
	// the failed attempt was asked for would be gone for good.
	QString staging;
	if (!stageRestoreTarget(projectPath, revision, &staging, error)) { return false; }

	// Rotate the live file in, so the restore itself is recoverable: the file
	// it replaces becomes revision 0.
	if (QFileInfo::exists(projectPath))
	{
		bool refused = false;
		QString reason;
		rotateProjectRevision(projectPath, &refused, &reason);
		if (refused)
		{
			// The stage survives: it holds the bytes this restore took out of
			// the set, and a retry has to be able to find them again.
			*error = reason;
			return false;
		}
	}

	if (!replaceProjectWithStaged(projectPath, staging))
	{
		*error = QStringLiteral("could not copy %1 over %2").arg(source, projectPath);
		return false;
	}
	return true;
}

bool replaceProjectWithStaged(const QString& projectPath, const QString& staging)
{
	// Luna cluster 4352 / 2026-10-08 rework: the live file is never removed. The staged bytes are copied
	// to a unique sibling first (a copy that fails leaves the live project and the staging file exactly as
	// they were), then moved over the live file in one replacing rename. A failed rename leaves the live
	// project as it was, so there is no recovery copy to fail. The staging file is removed only once the
	// replacement is in place; until then it still holds the revision the restore took out of the set.
	const QString pending = stageCopy(staging, projectPath + QStringLiteral(".replace-XXXXXX"));
	if (pending.isEmpty()) { return false; }
	if (!replaceWithStaged(pending, projectPath))
	{
		QFile::remove(pending);
		return false;
	}
	QFile::remove(staging);
	return true;
}

} // namespace control
} // namespace lmms
