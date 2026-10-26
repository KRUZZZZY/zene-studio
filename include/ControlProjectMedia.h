/*
 * ControlProjectMedia.h - how often a project uses each of its files, which
 *                         files in its managed media folder are never used, and
 *                         the cleanup that removes only those unused files.
 *
 * This is the engine half of the Project Media completion (Q050-121). It builds
 * on ControlProjectAssets.h: the same per-reference scan of the saved project
 * document, grouped by the file each reference resolves to, plus the files in
 * the project's own media folder that no reference names.
 *
 * The distinction the view needs is explicit in the data, not inferred from a
 * count: a local file with usage_count 0 is an unreferenced managed asset; a
 * reference that resolves to nothing is missing; one that resolves outside the
 * media folder is external. Only the first kind is ever a cleanup candidate.
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

#ifndef LMMS_CONTROL_PROJECT_MEDIA_H
#define LMMS_CONTROL_PROJECT_MEDIA_H

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ControlProjectAssets.h"
#include "lmms_export.h"

namespace lmms
{
namespace control
{

/*! One file the project media view shows: either a file a reference resolves
 *  to (used, missing or external) or a file in the managed media folder that no
 *  reference names (unused).
 *
 *  `path` is the resolved absolute path for a reference, and the absolute path
 *  of the file for an unused one. An unresolved reference has an empty path and
 *  keeps the stored value in `raw`; it is never a cleanup candidate. */
struct ProjectMediaFile
{
	QString path;
	QString raw;
	QString name;
	int usageCount = 0;
	bool exists = false;
	bool missing = false;
	bool unresolved = false;
	bool external = false;
	//! True only for an existing file inside the managed media folder with no references.
	bool cleanupCandidate = false;
	QStringList tags;
	QVector<int> referenceIndexes;

	//! "used" | "unused" | "missing" | "external" | "unresolved".
	QString state() const;
};

//! The whole answer for one project file and its media folder.
struct ProjectMediaScan
{
	QString file;
	QString format;
	//! The managed media folder, absolute; `mediaDirExists` says whether it is there.
	QString mediaDir;
	bool mediaDirExists = false;
	//! True when the media folder resolves inside the project's own folder. A
	//! folder that is a link elsewhere has no cleanup candidates.
	bool mediaDirContained = false;
	QVector<ProjectAssetReference> references;
	QVector<ProjectMediaFile> files;
	//! The cleanup candidates, in path order.
	QVector<ProjectMediaFile> unused;
	int missingCount = 0;
	int externalCount = 0;
	int unresolvedCount = 0;
	int usageTotal = 0;
};

/*! Reads \a projectPath (a saved .mmp/.mmpz) and groups every reference by the
 *  file it resolves to, counting every occurrence - repeated clips and repeated
 *  instrument references each add one. Then lists the files in the project's
 *  managed \a folderName (default "media") that no existing reference names.
 *  Reads only; nothing is written and no session is needed. */
LMMS_EXPORT bool controlScanProjectMedia(const QString& projectPath, const QString& folderName,
	ProjectMediaScan* out, ControlResult* error);

/*! Removes the files in \a paths that are, at the moment of the call, unused
 *  files inside the managed \a folderName folder of \a projectPath (the same
 *  folder controlScanProjectMedia listed, default "media"). Every requested
 *  path is rechecked against a fresh scan of the project document; a file that
 *  is referenced, missing, external, a symlink, or outside the folder is
 *  refused and left alone.
 *
 *  \a dryRun reports what would be removed and writes nothing. Otherwise the
 *  file is deleted with QFile::remove and cannot be restored by project undo,
 *  so the command that calls this is classified irreversible.
 *
 *  The caller is responsible for refusing a dirty open project before this
 *  runs; cleanupProjectMedia itself only sees the saved document. */
LMMS_EXPORT bool controlCleanupProjectMedia(const QString& projectPath, const QString& folderName,
	const QStringList& paths, bool dryRun, QJsonArray* removed, QJsonArray* refused,
	ProjectMediaScan* before, ControlResult* error);

//! The media folder name \a requested checked against the same containment
//! rules project.collect_media uses: relative, not empty, no "..". Defaults to
//! "media". False with a typed invalid_args result when it is not.
LMMS_EXPORT bool projectMediaFolderName(const QString& requested, QString* out, ControlResult* error);

} // namespace control
} // namespace lmms

#endif // LMMS_CONTROL_PROJECT_MEDIA_H
