/*
 * ControlProjectMedia.cpp - usage counts per project media file, the unused
 *                          files in the project's managed media folder, and the
 *                          rechecked removal of a selected set of them.
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

#include "ControlProjectMedia.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QSet>

namespace lmms
{
namespace control
{

namespace
{

//! @a path with every link on it followed: the canonical path when it exists,
//! the absolute one when it does not. Both sides of a containment check use it,
//! so a link cannot spell a path inside the project and lead elsewhere.
QString resolvedPath(const QString& path)
{
	const QFileInfo info(path);
	const QString canonical = info.canonicalFilePath();
	return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool insideFolder(const QString& path, const QString& folder)
{
	return resolvedPath(path).startsWith(resolvedPath(folder) + QLatin1Char('/'));
}

//! One file being grouped from the reference list.
struct Group
{
	ProjectMediaFile file;
	QString key;
};

//! The identity a reference is grouped by: the canonical file when it exists,
//! the clean resolved path when it is missing, and the stored value when it
//! cannot be resolved at all. Two references that name the same file therefore
//! share one row whatever spelling they used.
QString referenceKey(const ProjectAssetReference& ref)
{
	if (!ref.resolved) { return QStringLiteral("raw:") + ref.raw; }
	if (ref.exists) { return QStringLiteral("file:") + resolvedPath(ref.path); }
	return QStringLiteral("missing:") + QDir::cleanPath(ref.path);
}

QString identityOfPath(const QString& path)
{
	return QStringLiteral("file:") + resolvedPath(path);
}

void addReferenceToGroup(Group* group, const ProjectAssetReference& ref, const QString& mediaDir,
	bool mediaContained)
{
	ProjectMediaFile& file = group->file;
	if (file.raw.isEmpty()) { file.raw = ref.raw; }
	++file.usageCount;
	file.referenceIndexes.append(ref.index);
	if (!ref.resolved)
	{
		file.unresolved = true;
		return;
	}
	if (file.path.isEmpty()) { file.path = ref.path; }
	file.exists = file.exists || ref.exists;
	file.missing = file.missing || !ref.exists;
	if (ref.exists && mediaContained && insideFolder(ref.path, mediaDir))
	{
		file.external = false;
	}
	else if (ref.exists)
	{
		file.external = true;
	}
}

//! The files in the managed folder that no reference names, sorted by path.
QVector<ProjectMediaFile> unusedFilesIn(const ProjectMediaScan& scan, const QSet<QString>& referenced)
{
	QVector<ProjectMediaFile> out;
	if (!scan.mediaDirExists || !scan.mediaDirContained) { return out; }
	QDirIterator entries(scan.mediaDir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
		QDirIterator::Subdirectories);
	while (entries.hasNext())
	{
		entries.next();
		const QFileInfo info = entries.fileInfo();
		// A link is not the project's own asset: deleting it would either remove
		// nothing but the link or, if followed, a file somewhere else. Neither is
		// what "clean up the media folder" promised.
		if (info.isSymLink() || !info.isFile()) { continue; }
		if (!insideFolder(info.absoluteFilePath(), scan.mediaDir)) { continue; }
		const QString key = identityOfPath(info.absoluteFilePath());
		if (referenced.contains(key)) { continue; }
		ProjectMediaFile file;
		file.path = info.absoluteFilePath();
		file.name = info.fileName();
		file.exists = true;
		file.cleanupCandidate = true;
		out.append(file);
	}
	std::sort(out.begin(), out.end(), [](const ProjectMediaFile& a, const ProjectMediaFile& b) {
		return a.path < b.path;
	});
	return out;
}

//! The candidate with \a path, or nullptr. Compares resolved paths, so a caller
//! that spells a link to a media file is answered about the media file.
const ProjectMediaFile* candidateWithPath(const ProjectMediaScan& scan, const QString& path)
{
	const QString wanted = resolvedPath(path);
	for (const ProjectMediaFile& file : scan.unused)
	{
		if (resolvedPath(file.path) == wanted) { return &file; }
	}
	return nullptr;
}

QJsonObject refusedJson(const QString& path, const QString& why)
{
	return QJsonObject{{QStringLiteral("path"), path}, {QStringLiteral("why"), why}};
}

} // namespace

QString ProjectMediaFile::state() const
{
	if (unresolved) { return QStringLiteral("unresolved"); }
	if (missing || !exists) { return QStringLiteral("missing"); }
	if (external) { return QStringLiteral("external"); }
	if (usageCount == 0) { return QStringLiteral("unused"); }
	return QStringLiteral("used");
}

bool projectMediaFolderName(const QString& requested, QString* out, ControlResult* error)
{
	const QString name = requested.isEmpty() ? QStringLiteral("media") : requested;
	if (name.isEmpty() || QDir::isAbsolutePath(name) || name.contains(QStringLiteral("..")))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'dir' is a folder inside the project's folder, got '%1'").arg(name));
		return false;
	}
	*out = name;
	return true;
}

//! Groups every non-embedded reference by the file it resolves to. A repeated
//! clip or instrument reference adds one to the same row.
void groupReferences(ProjectMediaScan* out)
{
	QVector<Group> groups;
	QHash<QString, int> groupAt;
	for (const ProjectAssetReference& ref : out->references)
	{
		if (ref.embedded) { continue; }
		++out->usageTotal;
		const QString key = referenceKey(ref);
		int at = groupAt.value(key, -1);
		if (at < 0)
		{
			at = groups.size();
			groupAt.insert(key, at);
			groups.append(Group{});
			groups[at].key = key;
			groups[at].file.name = ref.raw.isEmpty() ? QString() : QFileInfo(ref.path).fileName();
		}
		addReferenceToGroup(&groups[at], ref, out->mediaDir, out->mediaDirContained);
	}
	for (const Group& group : groups)
	{
		ProjectMediaFile file = group.file;
		if (file.name.isEmpty() && !file.path.isEmpty()) { file.name = QFileInfo(file.path).fileName(); }
		file.cleanupCandidate = false;
		out->files.append(file);
	}
}

//! The per-state counts and the identity set of every present reference.
QSet<QString> countStates(ProjectMediaScan* out)
{
	QSet<QString> referenced;
	for (const ProjectMediaFile& file : out->files)
	{
		if (!file.path.isEmpty() && file.exists) { referenced.insert(identityOfPath(file.path)); }
		if (file.unresolved) { ++out->unresolvedCount; }
		if (file.missing) { ++out->missingCount; }
		if (file.external) { ++out->externalCount; }
	}
	return referenced;
}

bool controlScanProjectMedia(const QString& projectPath, const QString& folderName,
	ProjectMediaScan* out, ControlResult* error)
{
	ProjectAssetScan assets;
	if (!controlScanProjectAssets(projectPath, &assets, error)) { return false; }

	const QDir projectDir(QFileInfo(projectPath).absolutePath());
	const QDir mediaFolder(projectDir.filePath(folderName));
	out->file = assets.file;
	out->format = assets.format;
	out->references = assets.references;
	out->mediaDir = mediaFolder.absolutePath();
	out->mediaDirExists = QFileInfo(out->mediaDir).isDir();
	out->mediaDirContained = out->mediaDirExists && insideFolder(out->mediaDir, projectDir.absolutePath());
	out->files.clear();
	out->unused.clear();
	out->missingCount = 0;
	out->externalCount = 0;
	out->unresolvedCount = 0;
	out->usageTotal = 0;

	groupReferences(out);
	const QSet<QString> referenced = countStates(out);
	out->unused = unusedFilesIn(*out, referenced);
	for (const ProjectMediaFile& file : out->unused) { out->files.append(file); }
	return true;
}

//! How many references (if any) name the file at \a resolved.
int usageOfPath(const ProjectMediaScan& scan, const QString& resolved)
{
	for (const ProjectMediaFile& file : scan.files)
	{
		if (file.cleanupCandidate || !file.exists || file.path.isEmpty()) { continue; }
		if (resolvedPath(file.path) == resolved) { return file.usageCount; }
	}
	return 0;
}

//! The path-shape checks: absolute, and not a symbolic link. Resolves it too.
bool pathShapeOk(const QString& path, QString* resolved, QString* why)
{
	if (path.isEmpty() || !QFileInfo(path).isAbsolute())
	{
		*why = QStringLiteral("not an absolute path");
		return false;
	}
	if (QFileInfo(path).isSymLink())
	{
		*why = QStringLiteral("a symbolic link is not a project media asset");
		return false;
	}
	*resolved = resolvedPath(path);
	return true;
}

//! True when the managed folder is usable and \a resolved lies inside it.
bool insideManagedFolder(const ProjectMediaScan& scan, const QString& resolved)
{
	return scan.mediaDirExists && scan.mediaDirContained && insideFolder(resolved, scan.mediaDir);
}

//! Whether \a path may be removed: absolute, not a link, existing, inside the
//! managed folder, and a candidate of the fresh scan. False with the reason.
bool checkCleanupCandidate(const ProjectMediaScan& scan, const QString& path, QString* resolved, QString* why)
{
	if (!pathShapeOk(path, resolved, why)) { return false; }
	if (!insideManagedFolder(scan, *resolved))
	{
		*why = QStringLiteral("outside the project's managed media folder");
		return false;
	}
	const QFileInfo info(path);
	if (!info.exists() || !info.isFile())
	{
		*why = QStringLiteral("not an existing file");
		return false;
	}
	if (candidateWithPath(scan, path) != nullptr) { return true; }
	const int references = usageOfPath(scan, *resolved);
	*why = references > 0
		? QStringLiteral("still referenced by %1 project reference(s)").arg(references)
		: QStringLiteral("not an unreferenced file in the managed media folder");
	return false;
}

bool controlCleanupProjectMedia(const QString& projectPath, const QString& folderName,
	const QStringList& paths, bool dryRun, QJsonArray* removed, QJsonArray* refused,
	ProjectMediaScan* before, ControlResult* error)
{
	ProjectMediaScan scan;
	if (!controlScanProjectMedia(projectPath, folderName, &scan, error)) { return false; }
	if (before != nullptr) { *before = scan; }

	QSet<QString> seen;
	for (const QString& path : paths)
	{
		QString resolved;
		QString why;
		if (!checkCleanupCandidate(scan, path, &resolved, &why))
		{
			refused->append(refusedJson(path, why));
			continue;
		}
		if (seen.contains(resolved))
		{
			refused->append(refusedJson(path, QStringLiteral("named more than once")));
			continue;
		}
		seen.insert(resolved);
		const QFileInfo info(resolved);
		const qint64 bytes = info.size();  // measured before the removal, not after
		if (dryRun)
		{
			removed->append(QJsonObject{{QStringLiteral("path"), resolved},
				{QStringLiteral("would_remove"), true}, {QStringLiteral("bytes"), bytes}});
			continue;
		}
		if (!QFile::remove(info.absoluteFilePath()))
		{
			refused->append(refusedJson(path, QStringLiteral("the file could not be removed (check its permissions)")));
			continue;
		}
		removed->append(QJsonObject{{QStringLiteral("path"), resolved},
			{QStringLiteral("removed"), true}, {QStringLiteral("bytes"), bytes}});
	}
	return true;
}

} // namespace control
} // namespace lmms
