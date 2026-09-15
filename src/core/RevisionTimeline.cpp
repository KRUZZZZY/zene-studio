/*
 * RevisionTimeline.cpp - the in-app revision timeline over the artefacts this
 *                        engine already writes (feature-list row 76).
 *
 * The four artefacts, and where the bytes come from:
 *   rotation     control::projectRevisionPath() - `<file>.rev0..rev2`, the
 *                A16 keep-3 set include/ProjectRevisions.h owns.
 *   backup       `<file>.bak`, written by DataFile::writeFile on every save.
 *   autosave     ConfigManager::recoveryFile() and `<recovery>.bak`, with the
 *                `.info` sidecar include/ProjectRecovery.h owns as the source of
 *                the recorded time and of the project the autosave came from.
 *   git          `git log` over the project's path, and `git cat-file blob` for
 *                the bytes of one commit - both bounded by RevisionTimelineBounds
 *                ::GitTimeoutMs, both optional (no git, no repository, no
 *                entries: the report says which).
 *
 * No new store is written and no project state is touched: reading the timeline
 * is file/metadata work, and the ONE write (restore) is the same
 * rotate-then-replace shape control::restoreProjectRevision already uses, so the
 * file it replaces stays recoverable.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "RevisionTimeline.h"

#include <algorithm>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QStringList>

#include "ProjectRecovery.h"
#include "RevisionTimelineGit.h"
#include "ProjectRevisions.h"

namespace lmms
{
namespace control
{

namespace
{

using Bounds = RevisionTimelineBounds;

//! The source name of an id's family, so a caller never parses an id to learn
//! where a revision came from.
QString sourceOfSlot(const QString& id)
{
	if (id == QLatin1String("backup")) { return QStringLiteral("backup"); }
	if (id == QLatin1String("autosave") || id == QLatin1String("autosave_prev"))
	{
		return QStringLiteral("autosave");
	}
	if (id.startsWith(QLatin1String("git:"))) { return QStringLiteral("git"); }
	return QStringLiteral("rotation");
}

//! One resolved revision: the id, the artefact it names, and where its bytes
//! are. A slot is not yet a revision - whether it EXISTS is a file test.
struct RevisionSlot
{
	QString id;
	QString source;
	//! Where the bytes live; empty for a git slot (they come from the repository).
	QString path;
	//! The commit a git slot names, empty otherwise.
	QString commit;
};

//! "<file>.rev<n>" for n in the keep-3 policy's range, or false.
bool isRotationId(const QString& id, int* revision)
{
	if (!id.startsWith(QLatin1String("rev"))) { return false; }
	bool ok = false;
	const int value = id.mid(3).toInt(&ok);
	if (!ok || value < 0 || value >= ProjectRevisionPolicy::Keep) { return false; }
	*revision = value;
	return true;
}

//! The slot \a id names, whether or not its artefact is still on disk. False when
//! the id names nothing this timeline can ever hold.
bool resolveSlot(const QString& projectPath, const QString& recoveryFile, const QString& id,
	RevisionSlot* slot, QString* error)
{
	int revision = 0;
	if (isRotationId(id, &revision))
	{
		slot->id = id;
		slot->source = sourceOfSlot(id);
		slot->path = projectRevisionPath(projectPath, revision);
		return true;
	}
	if (id == QLatin1String("backup") || id == QLatin1String("autosave")
		|| id == QLatin1String("autosave_prev"))
	{
		slot->id = id;
		slot->source = sourceOfSlot(id);
		slot->path = (id == QLatin1String("backup")) ? projectPath + QStringLiteral(".bak")
			: (id == QLatin1String("autosave")) ? recoveryFile
												: recoveryFile + QStringLiteral(".bak");
		return true;
	}
	if (id.startsWith(QLatin1String("git:")))
	{
		slot->id = id;
		slot->source = sourceOfSlot(id);
		slot->commit = id.mid(4);
		if (slot->commit.isEmpty())
		{
			*error = QStringLiteral("'%1' names no commit: the git ids are 'git:<sha>'").arg(id);
			return false;
		}
		return true;
	}
	*error = QStringLiteral("'%1' is not a revision id: the timeline's ids are rev0..rev%2, "
		"'backup', 'autosave', 'autosave_prev' and 'git:<sha>' (revisions.list reports them)")
		.arg(id).arg(ProjectRevisionPolicy::Keep - 1);
	return false;
}

QByteArray readFileBytes(const QString& path, qint64 cap, QString* error)
{
	const QFileInfo info(path);
	if (!info.exists() || !info.isFile())
	{
		*error = QStringLiteral("no such revision artefact: %1").arg(path);
		return QByteArray();
	}
	if (info.size() > cap)
	{
		*error = QStringLiteral("%1 is %2 bytes, over the %3-byte revision cap: it is reported "
			"and not read, because a truncated project document is a corrupt one")
			.arg(path).arg(info.size()).arg(cap);
		return QByteArray();
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = QStringLiteral("could not read %1").arg(path);
		return QByteArray();
	}
	return file.readAll();
}

QString sha256Of(const QByteArray& bytes)
{
	return QString::fromLatin1(
		QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

//! sha256 of a file's bytes, streamed. Empty when it cannot be read.
QString sha256OfFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Algorithm::Sha256);
	if (!hash.addData(&file)) { return QString(); }
	return QString::fromLatin1(hash.result().toHex());
}

/*! An autosave's own provenance: the `.info` sidecar's recorded time and the
 *  project it came from. Absent or unparsable, the file's own mtime stands and
 *  no note is added - a recovery written by upstream has no sidecar and is still
 *  a revision. */
void applyAutosaveIdentity(RevisionEntry* entry)
{
	QFile sidecar(ProjectRecovery::recoveryInfoPath(entry->path));
	if (!sidecar.open(QIODevice::ReadOnly)) { return; }
	QString from;
	QDateTime savedAt;
	const bool parsed = ProjectRecovery::parseRecoveryIdentity(
		QString::fromUtf8(sidecar.readAll()), from, savedAt);
	if (!parsed) { return; }
	if (savedAt.isValid()) { entry->timestamp = savedAt.toUTC(); }
	entry->note = QStringLiteral("autosave of %1").arg(from);
}

//! Newest first; ties keep the artefact order the list was built in, so the
//! order is total and reproducible.
bool entryIsNewer(const RevisionEntry& left, const RevisionEntry& right)
{
	if (left.timestamp == right.timestamp)
	{
		if (left.bytes == right.bytes) { return left.id < right.id; }
		return left.bytes > right.bytes;
	}
	return left.timestamp > right.timestamp;
}

} // namespace

QString revisionLiveId()
{
	return QStringLiteral("live");
}

RevisionEntry findRevision(const QString& projectPath, const QString& recoveryFile,
	const QString& id)
{
	RevisionEntry entry;
	RevisionSlot slot;
	QString ignored;
	if (id == revisionLiveId())
	{
		entry.id = id;
		entry.source = QStringLiteral("project");
		entry.path = projectPath;
		entry.bytes = QFileInfo(projectPath).size();
		entry.timestamp = QFileInfo(projectPath).lastModified().toUTC();
		return entry;
	}
	if (!resolveSlot(projectPath, recoveryFile, id, &slot, &ignored)) { return entry; }
	if (slot.source == QStringLiteral("git")) { return entry; } // resolved by readRevisionBytes
	QFileInfo info(slot.path);
	if (!info.exists() || !info.isFile()) { return entry; }
	entry.id = slot.id;
	entry.source = slot.source;
	entry.path = slot.path;
	entry.bytes = info.size();
	entry.timestamp = info.lastModified().toUTC();
	// The hash is what a caller compares two revisions by without reading them,
	// so the list carries it. An artefact over the per-revision cap is listed
	// WITHOUT a hash rather than hashed and then refused on read: the same bound
	// decides both halves, so a listed entry with a hash is always readable.
	if (info.size() <= Bounds::MaxRevisionBytes) { entry.sha256 = sha256OfFile(slot.path); }
	if (slot.source == QStringLiteral("autosave")) { applyAutosaveIdentity(&entry); }
	return entry;
}

QVector<RevisionEntry> listProjectRevisions(const QString& projectPath,
	const QString& recoveryFile, bool includeGit, QJsonObject* gitReport)
{
	QVector<RevisionEntry> entries;
	const QStringList ids{QStringLiteral("rev0"), QStringLiteral("rev1"), QStringLiteral("rev2"),
		QStringLiteral("backup"), QStringLiteral("autosave"), QStringLiteral("autosave_prev")};
	for (const QString& id : ids)
	{
		const RevisionEntry entry = findRevision(projectPath, recoveryFile, id);
		if (!entry.id.isEmpty()) { entries.append(entry); }
	}
	if (includeGit)
	{
		if (projectPath.isEmpty())
		{
			gitReport->insert(QStringLiteral("in_repository"), false);
			gitReport->insert(QStringLiteral("reason"),
				QStringLiteral("the session has no project file, so there is no path to ask a "
					"repository about"));
		}
		else
		{
			entries.append(timelineGit::entries(projectPath, gitReport));
		}
	}
	else
	{
		gitReport->insert(QStringLiteral("in_repository"), false);
		gitReport->insert(QStringLiteral("reason"),
			QStringLiteral("not asked for: 'include_git' was false, so the timeline lists the "
				"artefacts on disk only"));
	}
	if (entries.size() > Bounds::MaxEntries)
	{
		std::sort(entries.begin(), entries.end(), entryIsNewer);
		entries.resize(Bounds::MaxEntries);
		return entries;
	}
	std::sort(entries.begin(), entries.end(), entryIsNewer);
	return entries;
}

QJsonObject revisionEntryJson(const RevisionEntry& entry)
{
	QJsonObject out;
	out.insert(QStringLiteral("id"), entry.id);
	out.insert(QStringLiteral("source"), entry.source);
	out.insert(QStringLiteral("timestamp"),
		entry.timestamp.isValid() ? entry.timestamp.toUTC().toString(Qt::ISODate)
								  : QString());
	out.insert(QStringLiteral("bytes"), entry.bytes);
	out.insert(QStringLiteral("path"), entry.path);
	out.insert(QStringLiteral("sha256"), entry.sha256);
	if (!entry.note.isEmpty()) { out.insert(QStringLiteral("detail"), entry.note); }
	return out;
}

QJsonObject revisionTimelineState(const QString& projectPath, const QString& recoveryFile,
	bool includeGit)
{
	QJsonObject gitReport;
	gitReport.insert(QStringLiteral("in_repository"), false);
	gitReport.insert(QStringLiteral("reason"), QString());
	QVector<RevisionEntry> entries =
		listProjectRevisions(projectPath, recoveryFile, includeGit, &gitReport);

	QJsonObject sourceCounts;
	QJsonArray revisions;
	for (const RevisionEntry& entry : entries)
	{
		revisions.append(revisionEntryJson(entry));
		sourceCounts.insert(entry.source, sourceCounts.value(entry.source).toInt() + 1);
	}
	QJsonObject out;
	out.insert(QStringLiteral("file"), projectPath);
	out.insert(QStringLiteral("count"), revisions.size());
	out.insert(QStringLiteral("sources"), sourceCounts);
	out.insert(QStringLiteral("revisions"), revisions);
	out.insert(QStringLiteral("git"), gitReport);
	return out;
}

bool readRevisionBytes(const QString& projectPath, const QString& recoveryFile, const QString& id,
	QByteArray* bytes, RevisionEntry* entry, QString* error)
{
	RevisionSlot slot;
	if (id == revisionLiveId())
	{
		slot.id = id;
		slot.source = QStringLiteral("project");
		slot.path = projectPath;
	}
	else if (!resolveSlot(projectPath, recoveryFile, id, &slot, error))
	{
		return false;
	}

	if (slot.source == QStringLiteral("git"))
	{
		if (!timelineGit::bytesOfCommit(projectPath, slot.commit, bytes, error)) { return false; }
		*entry = RevisionEntry();
		entry->id = slot.id;
		entry->source = slot.source;
		entry->note = slot.commit;
		entry->bytes = bytes->size();
		return true;
	}
	*bytes = readFileBytes(slot.path, Bounds::MaxRevisionBytes, error);
	if (bytes->isEmpty() && !error->isEmpty()) { return false; }
	*entry = findRevision(projectPath, recoveryFile, slot.id);
	if (entry->id.isEmpty()) { entry->id = slot.id; entry->source = slot.source; }
	entry->bytes = bytes->size();
	entry->sha256 = sha256Of(*bytes);
	return true;
}

bool restoreTimelineRevision(const QString& projectPath, const QString& recoveryFile,
	const QString& id, QString* error)
{
	error->clear();
	if (projectPath.isEmpty())
	{
		*error = QStringLiteral("no project file to restore into");
		return false;
	}
	int revision = 0;
	if (isRotationId(id, &revision))
	{
		// The keep-3 policy's own restore: it takes the revision out of the set
		// before rotating the live file in, a fix the rotation order needs and
		// that has exactly one implementation.
		return restoreProjectRevision(projectPath, revision, error);
	}

	RevisionSlot slot;
	if (!resolveSlot(projectPath, recoveryFile, id, &slot, error)) { return false; }

	// Stage FIRST, so nothing is written before the bytes are known readable.
	QByteArray bytes;
	RevisionEntry entry;
	if (!readRevisionBytes(projectPath, recoveryFile, id, &bytes, &entry, error)) { return false; }
	const QString staging = projectPath + QStringLiteral(".timeline-stage");
	QFile::remove(staging);
	QFile stage(staging);
	if (!stage.open(QIODevice::WriteOnly | QIODevice::Truncate)
		|| stage.write(bytes) != bytes.size())
	{
		*error = QStringLiteral("could not stage %1 bytes for %2").arg(bytes.size()).arg(id);
		stage.close();
		QFile::remove(staging);
		return false;
	}
	stage.close();

	// Rotate the live file into the keep-3 set, so the restore is itself
	// recoverable: the file it replaces becomes revision 0. A live file over the
	// policy's cap cannot be rotated, and then the restore is refused rather
	// than performed without an inverse.
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
	if (!QFile::rename(staging, projectPath))
	{
		*error = QStringLiteral("could not move the staged revision over %1").arg(projectPath);
		QFile::remove(staging);
		return false;
	}
	return true;
}

} // namespace control
} // namespace lmms
