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
bool copyRevision(const QString& from, const QString& to, bool* refused)
{
	QFileInfo source(from);
	if (source.size() > ProjectRevisionPolicy::MaxRevisionBytes)
	{
		*refused = true;
		return false;
	}
	QFile::remove(to);
	return QFile::copy(from, to);
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
		bool ignored = false;
		copyRevision(older, projectRevisionPath(projectPath, revision), &ignored);
	}
	bool ignored = false;
	copyRevision(projectPath, projectRevisionPath(projectPath, 0), &ignored);
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

	QFile::remove(projectPath);
	if (!QFile::copy(staging, projectPath))
	{
		*error = QStringLiteral("could not copy %1 over %2").arg(source, projectPath);
		QFile::remove(staging);
		return false;
	}
	QFile::remove(staging);
	return true;
}

} // namespace control
} // namespace lmms
