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

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

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

//! Copies \a from onto \a to, replacing it. The copy is bounded: a source
//! larger than the policy's per-revision cap is refused, never truncated.
//!
//! Luna cluster 5236: the destination is NOT removed first. The copy lands in a
//! sibling temporary file and is renamed over \a to only once it is complete, so
//! a failed copy leaves whatever revision was in \a to exactly as it was - a
//! backup is never destroyed by a rotation that then fails.
bool copyRevision(const QString& from, const QString& to, bool* refused)
{
	QFileInfo source(from);
	if (source.size() > ProjectRevisionPolicy::MaxRevisionBytes)
	{
		*refused = true;
		return false;
	}
	const QString staged = to + QStringLiteral(".rotate");
	QFile::remove(staged);
	if (!QFile::copy(from, staged))
	{
		QFile::remove(staged);
		return false;
	}
	QFile::remove(to);
	if (!QFile::rename(staged, to))
	{
		QFile::remove(staged);
		return false;
	}
	return true;
}

//! Drops the revisions past the policy's \c Keep slots.
void trimRevisionSet(const QString& projectPath)
{
	for (int revision = ProjectRevisionPolicy::Keep; ; ++revision)
	{
		const QString stale = projectRevisionPath(projectPath, revision);
		if (!QFileInfo::exists(stale)) { break; }
		QFile::remove(stale);
	}
}

} // namespace

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
	for (int revision = ProjectRevisionPolicy::Keep - 1; revision > 0; --revision)
	{
		const QString older = projectRevisionPath(projectPath, revision - 1);
		if (!QFileInfo::exists(older)) { continue; }
		// Luna cluster 5236: a shift that fails is a refusal, not something to
		// step over. The copy left the destination backup intact, so reporting
		// it costs the caller its claim of an inverse and nothing else.
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
			return 0;
		}
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
	trimRevisionSet(projectPath);

	int kept = 0;
	for (int revision = 0; revision < ProjectRevisionPolicy::Keep; ++revision)
	{
		if (QFileInfo::exists(projectRevisionPath(projectPath, revision))) { ++kept; }
	}
	return kept;
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
	const QString staging = projectRevisionPath(projectPath, revision) + QStringLiteral(".restore");
	QFile::remove(staging);
	if (!QFile::copy(source, staging))
	{
		*error = QStringLiteral("could not stage %1").arg(source);
		return false;
	}

	// Rotate the live file in, so the restore itself is recoverable: the file
	// it replaces becomes revision 0.
	if (QFileInfo::exists(projectPath))
	{
		bool refused = false;
		QString reason;
		rotateProjectRevision(projectPath, &refused, &reason);
		if (refused)
		{
			QFile::remove(staging);
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
	const bool hadLive = QFileInfo::exists(projectPath);
	QFile::remove(projectPath);
	if (QFile::rename(staging, projectPath)) { return true; }
	QFile::remove(staging);
	// The rotation just kept the live file as revision 0: put it back rather than leave no project.
	if (hadLive) { QFile::copy(projectRevisionPath(projectPath, 0), projectPath); }
	return false;
}

} // namespace control
} // namespace lmms
