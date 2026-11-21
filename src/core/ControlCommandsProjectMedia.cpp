/*
 * ControlCommandsProjectMedia.cpp - project.media_usage / project.media_cleanup:
 *                                   the Project Media view's agent surface.
 *
 * The read verb answers with one row per file the saved project references,
 * with the number of references to it, plus the files in the managed media
 * folder that no reference names. Missing and external references are distinct
 * states and are never cleanup candidates. The write verb removes only a
 * selected set of still-unused local files and is classified irreversible
 * because project undo cannot restore a deleted media file.
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

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlProjectAssets.h"
#include "ControlProjectMedia.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject mediaFileJson(const ProjectMediaFile& file)
{
	QJsonObject out;
	out.insert(QStringLiteral("path"), file.path);
	out.insert(QStringLiteral("raw"), file.raw);
	out.insert(QStringLiteral("name"), file.name);
	out.insert(QStringLiteral("usage_count"), file.usageCount);
	out.insert(QStringLiteral("state"), file.state());
	out.insert(QStringLiteral("exists"), file.exists);
	out.insert(QStringLiteral("missing"), file.missing || (!file.exists && !file.unresolved));
	out.insert(QStringLiteral("external"), file.external);
	out.insert(QStringLiteral("unresolved"), file.unresolved);
	out.insert(QStringLiteral("cleanup_candidate"), file.cleanupCandidate);
	out.insert(QStringLiteral("tags"), QJsonArray::fromStringList(file.tags));
	QJsonArray indexes;
	for (int index : file.referenceIndexes) { indexes.append(index); }
	out.insert(QStringLiteral("reference_indexes"), indexes);
	return out;
}

QJsonArray mediaFilesJson(const QVector<ProjectMediaFile>& files)
{
	QJsonArray out;
	for (const ProjectMediaFile& file : files) { out.append(mediaFileJson(file)); }
	return out;
}

QJsonObject mediaScanJson(const ProjectMediaScan& scan)
{
	QJsonObject out;
	out.insert(QStringLiteral("file"), scan.file);
	out.insert(QStringLiteral("format"), scan.format);
	out.insert(QStringLiteral("media_dir"), scan.mediaDir);
	out.insert(QStringLiteral("media_dir_exists"), scan.mediaDirExists);
	out.insert(QStringLiteral("media_dir_contained"), scan.mediaDirContained);
	out.insert(QStringLiteral("reference_count"), scan.references.size());
	out.insert(QStringLiteral("usage_total"), scan.usageTotal);
	out.insert(QStringLiteral("file_count"), scan.files.size());
	out.insert(QStringLiteral("unused_count"), scan.unused.size());
	out.insert(QStringLiteral("missing_count"), scan.missingCount);
	out.insert(QStringLiteral("external_count"), scan.externalCount);
	out.insert(QStringLiteral("unresolved_count"), scan.unresolvedCount);
	out.insert(QStringLiteral("files"), mediaFilesJson(scan.files));
	out.insert(QStringLiteral("unused"), mediaFilesJson(scan.unused));
	return out;
}

//! The project path from the args, refusing an empty one before reading anything.
bool projectPathArg(const QJsonObject& args, QString* path, ControlResult* error)
{
	*path = args.value(QStringLiteral("project")).toString();
	if (path->isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'project' is the project file to read (.mmp or .mmpz); it cannot be empty"));
		return false;
	}
	return true;
}

//! True when \a project is the open project and that session has unsaved changes.
//! The scan reads the saved document, so deleting from an unsaved session could
//! remove a file the live session has just started to reference.
bool openProjectIsDirty(const QString& project)
{
	Song* song = Engine::getSong();
	if (song == nullptr || !song->isModified()) { return false; }
	const QString open = song->projectFileName();
	if (open.isEmpty()) { return false; }
	return QFileInfo(open).canonicalFilePath() == QFileInfo(project).canonicalFilePath();
}

ControlResult mediaUsage(const QJsonObject& args)
{
	QString project;
	ControlResult error;
	if (!projectPathArg(args, &project, &error)) { return error; }
	QString folderName;
	if (!projectMediaFolderName(args.value(QStringLiteral("dir")).toString(), &folderName, &error)) { return error; }
	ProjectMediaScan scan;
	if (!controlScanProjectMedia(project, folderName, &scan, &error)) { return error; }
	return ControlResult::success(mediaScanJson(scan));
}

bool readCleanupPaths(const QJsonObject& args, QStringList* paths, ControlResult* error)
{
	const QJsonValue pathsValue = args.value(QStringLiteral("paths"));
	if (!pathsValue.isArray())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'paths' is the array of absolute files to clean up (project.media_usage names them)"));
		return false;
	}
	for (const QJsonValue& value : pathsValue.toArray())
	{
		if (!value.isString())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'paths' must hold strings"));
			return false;
		}
		paths->append(value.toString());
	}
	return true;
}

//! The irreversible record's before-state: every selected path as it was.
QJsonArray cleanupBeforeState(const QStringList& paths)
{
	QJsonArray recorded;
	for (const QString& path : paths)
	{
		const QFileInfo info(path);
		recorded.append(QJsonObject{{QStringLiteral("path"), path},
			{QStringLiteral("existed"), info.exists()},
			{QStringLiteral("bytes"), info.exists() ? info.size() : qint64(-1)},
			{QStringLiteral("mtime_ms"), info.exists() ? info.lastModified().toMSecsSinceEpoch() : qint64(-1)}});
	}
	return recorded;
}

ControlResult mediaCleanup(const QJsonObject& args)
{
	QString project;
	ControlResult error;
	if (!projectPathArg(args, &project, &error)) { return error; }
	QStringList paths;
	if (!readCleanupPaths(args, &paths, &error)) { return error; }
	QString folderName;
	if (!projectMediaFolderName(args.value(QStringLiteral("dir")).toString(), &folderName, &error)) { return error; }
	if (openProjectIsDirty(project))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is the open project and has unsaved changes; save it before cleaning up "
				"media, so a file the unsaved session now references cannot be removed").arg(project));
	}
	const bool dryRun = args.value(QStringLiteral("dry_run")).toBool(false);
	if (!dryRun && !args.value(QStringLiteral("confirm")).toBool(false))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("removing media is irreversible: show the paths to a human, then pass "
				"confirm: true (or pass dry_run: true to preview what would be removed)"));
	}
	QJsonArray removed;
	QJsonArray refused;
	ProjectMediaScan before;
	if (!controlCleanupProjectMedia(project, folderName, paths, dryRun, &removed, &refused, &before, &error)) { return error; }
	ProjectMediaScan after;
	if (!controlScanProjectMedia(project, folderName, &after, &error)) { return error; }
	QJsonObject result;
	result.insert(QStringLiteral("file"), project);
	result.insert(QStringLiteral("media_dir"), after.mediaDir);
	result.insert(QStringLiteral("dry_run"), dryRun);
	result.insert(QStringLiteral("removed"), removed);
	result.insert(QStringLiteral("refused"), refused);
	result.insert(QStringLiteral("unused_after"), mediaFilesJson(after.unused));
	result.insert(QStringLiteral("cleanup_candidates_before"), before.unused.size());
	if (!dryRun)
	{
		result.insert(QStringLiteral("__transaction"), QJsonObject{
			{QStringLiteral("before"), cleanupBeforeState(paths)},
			{QStringLiteral("reversible"), false},
			{QStringLiteral("mechanism"), QStringLiteral("none: removing a media file deletes bytes project "
				"undo does not carry. FALLBACK: restore the files from a backup or their source; the "
				"before-state records every selected path, its size and mtime")}});
	}
	return ControlResult::success(result);
}

void registerMediaUsage(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.media_usage");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("media_usage");
	cmd.description = QStringLiteral("The Project Media inventory of a saved project file: one row per "
		"file the project references, with a usage count that adds every clip and every supported "
		"instrument reference (repeated references to the same file add up), and one row for each "
		"file in the project's managed 'media' folder that nothing references. Missing references, "
		"unresolved paths and files referenced outside the folder are reported as distinct states "
		"and are never cleanup candidates. Reads the project FILE; nothing is written. 'dir' names "
		"the managed folder (default \"media\").");
	cmd.argsSchema = objectSchema({{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("dir"), stringProperty()}}, {QStringLiteral("project")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("media_dir"), stringProperty()},
		{QStringLiteral("media_dir_exists"), booleanProperty()},
		{QStringLiteral("media_dir_contained"), booleanProperty()},
		{QStringLiteral("reference_count"), integerProperty()},
		{QStringLiteral("usage_total"), integerProperty()},
		{QStringLiteral("file_count"), integerProperty()},
		{QStringLiteral("unused_count"), integerProperty()},
		{QStringLiteral("missing_count"), integerProperty()},
		{QStringLiteral("external_count"), integerProperty()},
		{QStringLiteral("unresolved_count"), integerProperty()},
		{QStringLiteral("files"), arrayProperty()},
		{QStringLiteral("unused"), arrayProperty()},
	});
	cmd.mutating = false;
	cmd.handler = mediaUsage;
	registry.registerCommand(cmd);
}

void registerMediaCleanup(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.media_cleanup");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("media_cleanup");
	cmd.description = QStringLiteral("Remove selected files from a project's managed media folder, "
		"but only the ones that are STILL unused: called with the paths a project.media_usage preview "
		"listed, it rescans the project document and refuses every file a reference now names, every "
		"missing or external path, and every symbolic link. 'dir' names the same managed folder the "
		"preview listed (default \"media\"). 'dry_run' reports exactly what would be removed and writes "
		"nothing; without it each selected file is deleted. A real removal also requires 'confirm': true "
		"- pass it only after showing the paths to a human and getting their authorization. IRREVERSIBLE: a "
		"deleted media file cannot be restored by control.undo, so preview first and keep a backup if the "
		"file matters. Refuses while the open project is the named one and has unsaved changes, because the "
		"saved-file scan would not see them.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("dir"), stringProperty()},
		{QStringLiteral("paths"), arrayProperty()},
		{QStringLiteral("dry_run"), booleanProperty()},
		{QStringLiteral("confirm"), booleanProperty()},
	}, {QStringLiteral("project"), QStringLiteral("paths")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("media_dir"), stringProperty()},
		{QStringLiteral("dry_run"), booleanProperty()},
		{QStringLiteral("removed"), arrayProperty()},
		{QStringLiteral("refused"), arrayProperty()},
		{QStringLiteral("unused_after"), arrayProperty()},
		{QStringLiteral("cleanup_candidates_before"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = mediaCleanup;
	registry.registerCommand(cmd);
}

} // namespace

void registerProjectMediaCommands(ControlRegistry& registry)
{
	registerMediaUsage(registry);
	registerMediaCleanup(registry);
}

} // namespace lmms
