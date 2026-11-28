/*
 * ControlCommandsCollectMedia.cpp - project.collect_media: the media a project uses, copied beside it
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

#include <memory>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "BounceInPlace.h" // namesTheSameFile: is this the file the session has open?
#include "ControlDeviceSupport.h" // controlReadFileBytes / controlWriteFileBytes
#include "ControlEdit.h"
#include "ControlProjectAssets.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

QString fileSha256(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(&file);
	return QString::fromLatin1(hash.result().toHex());
}

//! Where @a source's copy goes in @a folder: its own file name, or `name (2).ext` ... when a
//! DIFFERENT file holds the name. A file with the same content is reused, not copied again.
QString destinationFor(const QDir& folder, const QString& source, const QString& sha256)
{
	const QFileInfo info(source);
	for (int n = 1; n < 1000; ++n)
	{
		const QString name = n == 1 ? info.fileName()
			: QStringLiteral("%1 (%2).%3").arg(info.completeBaseName()).arg(n).arg(info.suffix());
		const QString candidate = folder.filePath(name);
		if (!QFileInfo::exists(candidate) || fileSha256(candidate) == sha256) { return candidate; }
	}
	return QString();
}

//! @a path with every symlink on it resolved: the canonical path when the file exists, the
//! absolute one when it does not (nothing to resolve yet). Both sides of a containment check
//! go through this, so a link cannot spell a path inside the project and lead elsewhere.
QString resolvedPath(const QString& path)
{
	const QFileInfo info(path);
	const QString canonical = info.canonicalFilePath();
	return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

//! Whether @a path really lies inside @a folder, symlinks followed rather than trusted.
bool insideFolder(const QString& path, const QDir& folder)
{
	return resolvedPath(path).startsWith(resolvedPath(folder.absolutePath()) + QLatin1Char('/'));
}

//! The media folder @a folderPath is to be written to stays inside the project's folder once
//! every link on it is followed; false (with @a why) when it does not - a project whose `media`
//! is a link elsewhere must not have copies written through it.
bool mediaFolderIsContained(const QString& folderPath, const QDir& projectDir, QString* why)
{
	if (insideFolder(folderPath, projectDir)) { return true; }
	*why = QStringLiteral("'%1' resolves to '%2', which is outside the project's folder '%3'")
		.arg(QDir(projectDir).relativeFilePath(folderPath), QFileInfo(folderPath).canonicalFilePath(),
			QFileInfo(projectDir.absolutePath()).canonicalFilePath());
	return false;
}

//! Copy one reference's file and point the project at the copy; false (with @a why) when either
//! step fails. The copy is stored as `local:<folder>/<name>`, relative to the project.
bool collectOne(const QString& project, const QDir& projectDir, const QDir& folder,
	const ProjectAssetReference& ref, QString* to, QString* why)
{
	*to = destinationFor(folder, ref.path, ref.sha256);
	if (to->isEmpty()) { *why = QStringLiteral("no free name in the media folder"); return false; }
	if (!QFileInfo::exists(*to) && !QFile::copy(ref.path, *to)) { *why = QStringLiteral("the copy failed"); return false; }
	ProjectAssetRelink relink;
	ControlResult error;
	const QString stored = QStringLiteral("local:") + projectDir.relativeFilePath(*to);
	if (!controlRelinkProjectAsset(project, ref.raw, *to, QString(), false, &relink, &error, stored))
	{
		*why = error.errorMessage;
		return false;
	}
	return true;
}

//! The media folder `dir` names, relative to the project's folder; false (with @a error) when it would
//! leave it. An absolute folder is accepted when it is inside the project's folder (the Lua sandbox
//! hands `dir` over resolved); anything else must stay beside the project.
bool mediaFolderName(const QJsonObject& args, const QDir& projectDir, QString* out, ControlResult* error)
{
	QString folderName = args.value(QStringLiteral("dir")).toString(QStringLiteral("media"));
	if (QDir::isAbsolutePath(folderName) && insideFolder(folderName, projectDir))
	{
		folderName = projectDir.relativeFilePath(folderName);
	}
	if (folderName.isEmpty() || QDir::isAbsolutePath(folderName) || folderName.contains(QStringLiteral("..")))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'dir' is a folder inside the project's folder, got '%1'").arg(folderName));
		return false;
	}
	*out = folderName;
	return true;
}

//! Copy every external, existing reference of @a scan into @a folder once; what was copied and what failed.
void collectAll(const QString& project, const QDir& projectDir, const QDir& folder, const ProjectAssetScan& scan,
	QJsonArray* copied, QJsonArray* failed)
{
	QSet<QString> done;
	for (const ProjectAssetReference& ref : scan.references)
	{
		if (ref.embedded || !ref.exists || ref.path.isEmpty() || done.contains(ref.raw)) { continue; }
		done.insert(ref.raw);
		if (insideFolder(ref.path, projectDir)) { continue; }  // already beside the project
		QString to;
		QString why;
		if (collectOne(project, projectDir, folder, ref, &to, &why))
		{
			copied->append(QJsonObject{{QStringLiteral("from"), ref.raw}, {QStringLiteral("to"), to}});
		}
		else { failed->append(QJsonObject{{QStringLiteral("from"), ref.raw}, {QStringLiteral("why"), why}}); }
	}
}

/*! The open session a collection has to keep in step, when @a project is the
 *  file it is working on: the song, the session as it was captured BEFORE the
 *  command runs (the undo half), and the sections a partial session omitted.
 *  `song` is nullptr for a project that is not open - a plain file, which this
 *  verb has always handled and still does. */
struct OpenSession
{
	Song* song = nullptr;
	std::shared_ptr<const QByteArray> before;
	QStringList skipped;
};

/*! Whether @a project is the file the session has open, and the session state the
 *  collected document is adopted with. Refused - BEFORE anything is written -
 *  when the session holds changes the rewrite would drop, or when the session
 *  cannot be captured for the one step that undoes the command: a collection
 *  that cannot be taken back must not start. */
bool prepareOpenSession(const QString& project, OpenSession* open, ControlResult* error)
{
	Song* const song = Engine::getSong();
	if (song == nullptr || song->projectFileName().isEmpty()
		|| !BounceInPlace::namesTheSameFile(song->projectFileName(), project))
	{
		return true;
	}
	open->song = song;
	if (song->isModified())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' is the project this session has open, and the session has unsaved "
				"changes: collecting media rewrites the file and then reopens the collected document in "
				"the session, so those changes would be lost. Save the project first (project.save), then "
				"collect again").arg(project));
		return false;
	}
	ProjectJournal* const journal = Engine::projectJournal();
	if (journal == nullptr || !journal->isJournalling())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' is the project this session has open and this instance's undo journal "
				"is not recording, so the session could not be put back after the collection; nothing "
				"was written").arg(project));
		return false;
	}
	open->before = song->captureUndoSnapshot(&open->skipped);
	if (!open->before)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' is the project this session has open and the session could not be "
				"captured for undo (a load is running); nothing was written").arg(project));
		return false;
	}
	return true;
}

/*! The collected document, adopted by the session that has @a project open, and the
 *  ONE recorded step for the whole command.
 *
 *  The session is LOADED from the file the command just wrote - the load
 *  project.open runs - and not restored from a snapshot: the collected document
 *  names its copies as project-relative `local:` paths, which the engine
 *  resolves against the open project's own directory, and a snapshot is loaded
 *  from a temporary file where that base would point somewhere else. The
 *  undo/redo history the load clears is held across it and put back, so the
 *  command costs one step of the history it found instead of all of it.
 *
 *  The step restores BOTH halves: the file's previous bytes and the session
 *  they were collected from, so one control.undo leaves disk and memory saying
 *  the same thing - which is the whole point of the fix (BUG 13). A collected
 *  document that cannot be reopened or re-captured puts the file back and
 *  refuses, so the call never leaves half of itself done.
 *
 *  A project that is not open records exactly the step it always did: the
 *  file's bytes, and the copies stay. */
bool recordCollectionStep(const OpenSession& open, const QString& project, const QByteArray& previous,
	const QByteArray& written, QJsonObject* transaction, ControlResult* error)
{
	const QJsonObject about{{QStringLiteral("project"), project}, {QStringLiteral("bytes"), previous.size()}};
	if (open.song == nullptr)
	{
		addUndoStep(
			[project, previous]() { ControlResult ignored; controlWriteFileBytes(project, previous, true, &ignored); },
			[project, written]() { ControlResult ignored; controlWriteFileBytes(project, written, true, &ignored); });
		*transaction = transactionPayload(about,
			QStringLiteral("recorded action step: the project file's previous bytes"), QJsonObject(), true,
			QStringLiteral("action checkpoint (the project file's previous bytes, written back; the copies stay)"));
		return true;
	}

	ProjectJournal* const journal = Engine::projectJournal();
	ProjectJournal::DetachedHistory history = journal->detachHistory();
	open.song->loadProject(project);
	const QString refusal = open.song->loadRefusal();
	QStringList skippedAfter;
	const std::shared_ptr<const QByteArray> after = refusal.isEmpty()
		? open.song->captureUndoSnapshot(&skippedAfter) : nullptr;
	journal->reattachHistory(std::move(history));
	if (after == nullptr)
	{
		ControlResult ignored;
		controlWriteFileBytes(project, previous, true, &ignored);
		*error = ControlResult::failure(ControlErrorKind::Refused, refusal.isEmpty()
			? QStringLiteral("the collected document of '%1' could not be captured for undo, so the file "
				"was put back").arg(project)
			: QStringLiteral("the collected document of '%1' could not be reopened (%2); the file was put "
				"back").arg(project, refusal));
		return false;
	}

	// The session and its file are ONE document again: the captured snapshot is what a save
	// of this session writes, so the flag goes back to the state an open leaves (the capture
	// itself marks the session modified, as every document build does).
	open.song->noteProjectSaved(project);

	const auto restoreBoth = [project](const QByteArray& bytes, const std::shared_ptr<const QByteArray>& session,
		const QStringList& skipped)
	{
		ControlResult ignored;
		if (!controlWriteFileBytes(project, bytes, true, &ignored))
		{
			recordActionRestoreFailure(QStringLiteral("the project file %1 could NOT be written back during "
				"the undo of project.collect_media - the file still holds the collected bytes").arg(project));
		}
		if (!Engine::getSong()->restoreUndoSnapshot(*session, project, skipped))
		{
			recordActionRestoreFailure(QStringLiteral("the session could NOT be restored during the undo of "
				"project.collect_media - it still holds the collected references"));
		}
		// The session and its file are one document again (a restore marks the session modified).
		Engine::getSong()->noteProjectSaved(project);
	};
	addStructuralUndoStep(
		[restoreBoth, previous, before = open.before, skipped = open.skipped]() {
			restoreBoth(previous, before, skipped);
		},
		[restoreBoth, written, after, skippedAfter]() { restoreBoth(written, after, skippedAfter); },
		open.before->size() + after->size());
	*transaction = transactionPayload(about,
		QStringLiteral("recorded action step: the project file's previous bytes and the session they were "
			"collected from"), QJsonObject(), true,
		QStringLiteral("action checkpoint (the collected document is the session's own state; one step puts "
			"the file's previous bytes back AND reopens the session it had, so disk and memory agree again; "
			"the copies stay)"));
	return true;
}

ControlResult collectMedia(const QJsonObject& args)
{
	const QString project = args.value(QStringLiteral("project")).toString();
	const QDir projectDir = QFileInfo(project).absoluteDir();
	ControlResult error;
	OpenSession open;
	if (!prepareOpenSession(project, &open, &error)) { return error; }
	QString folderName;
	if (!mediaFolderName(args, projectDir, &folderName, &error)) { return error; }
	ProjectAssetScan scan;
	if (!controlHashProjectAssets(project, &scan, &error)) { return error; }
	QByteArray previous;
	if (!controlReadFileBytes(project, &previous, &error,
		ControlFileReadPurpose::ProjectDocument)) { return error; }
	if (!projectDir.mkpath(folderName))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot create '%1'").arg(folderName));
	}
	const QDir folder(projectDir.filePath(folderName));
	QString escaped;
	if (!mediaFolderIsContained(folder.absolutePath(), projectDir, &escaped))
	{
		return ControlResult::failure(ControlErrorKind::Refused, escaped);
	}
	QJsonArray copied;
	QJsonArray failed;
	collectAll(project, projectDir, folder, scan, &copied, &failed);
	QByteArray written;
	// Read back for the undo step: one that cannot be read is reported, not recorded as an empty file a redo would write.
	if (!controlReadFileBytes(project, &written, &error,
		ControlFileReadPurpose::ProjectDocument)) { return error; }
	QJsonObject result{{QStringLiteral("project"), project}, {QStringLiteral("dir"), folder.absolutePath()},
		{QStringLiteral("copied"), copied}, {QStringLiteral("failed"), failed},
		{QStringLiteral("missing"), scan.missingCount()}};
	// An unchanged project file records no step, and so no transaction claims one (the registry records nothing).
	if (written == previous) { return ControlResult::success(result); }
	QJsonObject transaction;
	if (!recordCollectionStep(open, project, previous, written, &transaction, &error)) { return error; }
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

} // namespace

namespace control
{

bool collectProjectMediaInto(const QString& project, const QString& folderName, QJsonArray* copied, QJsonArray* failed,
	int* missing, ControlResult* error)
{
	const QDir projectDir = QFileInfo(project).absoluteDir();
	ProjectAssetScan scan;
	if (!controlHashProjectAssets(project, &scan, error)) { return false; }
	if (!projectDir.mkpath(folderName))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot create '%1'").arg(folderName));
		return false;
	}
	const QDir folder(projectDir.filePath(folderName));
	QString escaped;
	if (!mediaFolderIsContained(folder.absolutePath(), projectDir, &escaped))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, escaped);
		return false;
	}
	collectAll(project, projectDir, folder, scan, copied, failed);
	*missing = scan.missingCount();
	return true;
}

} // namespace control

void registerCollectMediaCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.collect_media");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("collect_media");
	cmd.description = QStringLiteral("Make a project portable: copy every file the project file "
		"references from outside its own folder into `dir` (default \"media\") beside it, and point "
		"each reference at its copy as a project-relative `local:` path, so the folder can be moved "
		"or shared whole. A copy whose content matches a file already there is reused; a name held "
		"by a different file gets ` (2)`. Missing media is reported, not copied. Reversible: one "
		"control.undo puts the project file's previous bytes back (the copies stay on disk).");
	cmd.argsSchema = objectSchema({{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("dir"), stringProperty()}}, {QStringLiteral("project")});
	cmd.resultSchema = objectSchema({{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("dir"), stringProperty()}, {QStringLiteral("copied"), arrayProperty()},
		{QStringLiteral("failed"), arrayProperty()}, {QStringLiteral("missing"), integerProperty()}});
	cmd.mutating = true;
	cmd.handler = collectMedia;
	registry.registerCommand(cmd);
}

} // namespace lmms
