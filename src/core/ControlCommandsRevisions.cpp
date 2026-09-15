/*
 * ControlCommandsRevisions.cpp - the `revisions.*` command group (SPEC A11-A16):
 *                                the in-app revision timeline over the artefacts
 *                                this engine already writes (feature-list row 76).
 *
 * THE ITEM THIS CLOSES. OWNER-31 item 30 (feature-list row 76) is the revision
 * timeline: list the revisions a project has, with their source and timestamp,
 * compare one against another, and restore one. The artefacts are already on
 * disk and are written by three different subsystems for their own reasons - the
 * keep-3 rotation `project.save` performs, the `<file>.bak` DataFile::writeFile
 * makes on every interface save, the `recover.mmp` (+ `.info` sidecar) autosave,
 * and the project's own git history where it lives in a repository. This group is
 * the missing SURFACE over them; include/RevisionTimeline.h holds the engine half
 * and states what the timeline deliberately is not (it is not `mmpz-git diff`).
 *
 * A16, honestly, because this is the part that is easy to overclaim:
 *   revisions.list     not_mutating  - it reads files, their metadata and git
 *                                      history and writes nothing
 *   revisions.compare  not_mutating  - it reads two documents and counts their
 *                                      elements
 *   revisions.restore  true_inverse  - an action checkpoint: the live file is
 *                                      rotated into the keep-3 set before the
 *                                      staged bytes replace it, so the recorded
 *                                      undo step restores revision 0 - and where
 *                                      there was no live file the recorded step
 *                                      REMOVES the file the restore created.
 *                                      A live file over the policy's 8 MiB cap
 *                                      cannot be rotated, and then the restore is
 *                                      REFUSED, typed, before anything is written
 *                                      (the named fallback: the file on disk is
 *                                      the one a user already has, and the
 *                                      revision is still listed and readable).
 *
 * The session in memory is NOT reloaded by a restore - the file on disk is what
 * is restored, exactly as project.restore_revision states for its own restore -
 * so `project.open` is how a caller works on the restored bytes.
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

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "ProjectRevisions.h"
#include "RevisionTimeline.h"
#include "Song.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The project a call is about: the `project` argument, else the session's own
//! file. Empty means there is nothing to list, which is a typed refusal rather
//! than an empty timeline nobody can tell from "no revisions".
QString targetProject(const QJsonObject& args, QString* error)
{
	QString target = args.value(QStringLiteral("project")).toString();
	if (target.isEmpty()) { target = Engine::getSong()->projectFileName(); }
	if (target.isEmpty())
	{
		*error = QStringLiteral("no 'project' given and the session has no project file yet");
	}
	return target;
}

//! The autosave file this build looks at: ConfigManager's own recovery file,
//! read here rather than assumed, so a test can point the timeline elsewhere.
QString autosaveFile()
{
	return ConfigManager::inst()->recoveryFile();
}

QString sha256OfFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Algorithm::Sha256);
	if (!hash.addData(&file)) { return QString(); }
	return QString::fromLatin1(hash.result().toHex());
}

// ---------------------------------------------------------------------------
// revisions.list
// ---------------------------------------------------------------------------

void registerRevisionsList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("revisions.list");
	cmd.group = QStringLiteral("revisions");
	cmd.verb = QStringLiteral("list");
	cmd.description = QStringLiteral("The project's revision timeline: every revision this machine "
		"still holds, newest first, each with the SOURCE it came from ('rotation' - the keep-3 "
		"revision set project.save rotates, '<file>.rev0..rev2'; 'backup' - the '<file>.bak' a save "
		"from the interface writes; 'autosave' - the periodic recover.mmp and the recover.mmp.bak "
		"it replaced; 'git' - the commits that touched the file where the project lives in a git "
		"repository), its timestamp (UTC; an autosave reports its sidecar's recorded savedUTC), its "
		"size and its sha256. Read-only: nothing is written and no new store is created. The `id` "
		"each entry carries is what revisions.compare and revisions.restore take. The `git` half is "
		"bounded and optional: 'include_git' false, no git on the machine, or no repository, and the "
		"`git` object says which - without failing the list.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("include_git"), booleanProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("sources"), objectProperty()},
		{QStringLiteral("revisions"), arrayProperty()},
		{QStringLiteral("git"), objectProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		QString error;
		const QString target = targetProject(args, &error);
		if (target.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
		}
		const bool includeGit = args.contains(QStringLiteral("include_git"))
			? args.value(QStringLiteral("include_git")).toBool()
			: true;
		return ControlResult::success(
			revisionTimelineState(target, autosaveFile(), includeGit));
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// revisions.compare
// ---------------------------------------------------------------------------

void registerRevisionsCompare(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("revisions.compare");
	cmd.group = QStringLiteral("revisions");
	cmd.verb = QStringLiteral("compare");
	cmd.description = QStringLiteral("Compare two revisions of a project, or one revision "
		"against the file as it is on disk now (the id 'live', which only this command accepts - "
		"revisions.list does not report the working file as a revision). The result reports each "
		"side's id, source, timestamp, size and sha256, whether the two are byte-identical, and a "
		"STRUCTURAL comparison of the two documents: how many elements of each tag each side holds, "
		"the element totals and the tags that differ. That is a summary, not a semantic diff - the "
		"musical diff of two project documents is tools/mmpz-git's (`mmpz-git diff`), outside this "
		"process. Read-only.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("a"), stringProperty()},
		{QStringLiteral("b"), stringProperty()},
	}, {QStringLiteral("a")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("a"), objectProperty()},
		{QStringLiteral("b"), objectProperty()},
		{QStringLiteral("comparison"), objectProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		QString error;
		const QString target = targetProject(args, &error);
		if (target.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
		}
		const QString leftId = args.value(QStringLiteral("a")).toString();
		QString rightId = args.value(QStringLiteral("b")).toString();
		if (rightId.isEmpty()) { rightId = revisionLiveId(); }

		const QString recoveryFile = autosaveFile();
		QByteArray leftBytes;
		QByteArray rightBytes;
		RevisionEntry left;
		RevisionEntry right;
		if (!readRevisionBytes(target, recoveryFile, leftId, &leftBytes, &left, &error))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("'a' could not be read: %1").arg(error));
		}
		if (!readRevisionBytes(target, recoveryFile, rightId, &rightBytes, &right, &error))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("'b' could not be read: %1").arg(error));
		}

		QJsonObject result;
		result.insert(QStringLiteral("file"), target);
		result.insert(QStringLiteral("a"), revisionEntryJson(left));
		result.insert(QStringLiteral("b"), revisionEntryJson(right));
		result.insert(QStringLiteral("comparison"),
			compareRevisionDocuments(leftBytes, rightBytes));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// revisions.restore
// ---------------------------------------------------------------------------

void registerRevisionsRestore(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("revisions.restore");
	cmd.group = QStringLiteral("revisions");
	cmd.verb = QStringLiteral("restore");
	cmd.description = QStringLiteral("Restore one revision of the project OVER the file on disk, "
		"by the `id` revisions.list reports (rev0..rev2, 'backup', 'autosave', 'autosave_prev' or "
		"'git:<sha>'). The live file is rotated into the keep-3 revision set BEFORE it is replaced, "
		"so the restore is itself recoverable - control.undo puts the replaced file back, or, when "
		"there was no file on disk yet, removes the one this call created. Refused, typed and "
		"without writing anything, when the id names no revision, when the source exceeds the "
		"policy's 8 MiB per-revision cap, or when the live file does (its rotation would have to be "
		"bounded by the same cap, and a truncated project is a corrupt revision). The session in "
		"memory is NOT reloaded: call project.open afterwards to work on the restored bytes.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("id"), stringProperty()},
	}, {QStringLiteral("id")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("id"), stringProperty()},
		{QStringLiteral("source"), stringProperty()},
		{QStringLiteral("restored"), booleanProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("revisions"), objectProperty()},
		{QStringLiteral("recovered_by"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		QString error;
		const QString target = targetProject(args, &error);
		if (target.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
		}
		const QString id = args.value(QStringLiteral("id")).toString();
		if (id.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("no 'id' given: revisions.list reports the ids this project has"));
		}
		const QString recoveryFile = autosaveFile();

		// Read the revision FIRST: an unknown id, a vanished artefact or one over
		// the cap must refuse before the undo step is recorded and before
		// anything is written.
		QByteArray bytes;
		RevisionEntry entry;
		if (!readRevisionBytes(target, recoveryFile, id, &bytes, &entry, &error))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("revisions.restore: %1").arg(error));
		}

		const bool liveExisted = QFileInfo::exists(target);
		const qint64 replacedBytes = liveExisted ? QFileInfo(target).size() : 0;
		// ONE action step, recorded before the write: restore revision 0 (the file
		// this call rotated in) - or, when the restore CREATED the file, remove it,
		// because a file that never existed cannot be restored.
		control::addUndoStep(
			[target, liveExisted]() {
				if (liveExisted)
				{
					QString ignored;
					control::restoreProjectRevision(target, 0, &ignored);
					return;
				}
				QFile::remove(target);
			},
			[target, recoveryFile, id]() {
				QString ignored;
				control::restoreTimelineRevision(target, recoveryFile, id, &ignored);
			});

		if (!control::restoreTimelineRevision(target, recoveryFile, id, &error))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("revisions.restore: %1").arg(error));
		}

		QJsonObject result;
		result.insert(QStringLiteral("file"), target);
		result.insert(QStringLiteral("id"), id);
		result.insert(QStringLiteral("source"), entry.source.isEmpty()
				? QStringLiteral("rotation") : entry.source);
		result.insert(QStringLiteral("restored"), true);
		result.insert(QStringLiteral("bytes"), static_cast<qint64>(bytes.size()));
		result.insert(QStringLiteral("sha256"), sha256OfFile(target));
		result.insert(QStringLiteral("revisions"), control::projectRevisionState(target));
		result.insert(QStringLiteral("recovered_by"),
			liveExisted ? QStringLiteral("revision 0 of the keep-3 set (the file this restore "
				"replaced), through control.undo or project.restore_revision")
						: QStringLiteral("nothing: the restore created the file, so control.undo "
				"removes it again"));
		result.insert(QStringLiteral("__transaction"), transactionPayload(
			QJsonObject{{QStringLiteral("file"), target}, {QStringLiteral("id"), id},
				{QStringLiteral("live_file_existed"), liveExisted},
				{QStringLiteral("replaced_bytes"), replacedBytes}},
			QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("path"), target}, {QStringLiteral("id"), id}},
			true,
			liveExisted
				? QStringLiteral("action checkpoint: the live file was rotated into the 'keep-3' "
					"revision set before the staged revision replaced it, and the recorded undo "
					"step restores revision 0 (the file this call replaced). The redo re-restores "
					"the revision this call chose, from its own artefact - which is why a "
					"file-level inverse is a COMMAND here and not a journal step (see "
					"docs/A16-REVERSIBILITY.md)")
				: QStringLiteral("action checkpoint: the project file did not exist before this "
					"call, so the recorded undo step REMOVES the file the restore created - a "
					"file that never existed cannot be restored. The redo re-restores the "
					"revision from its own artefact")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerRevisionsCommands(ControlRegistry& registry)
{
	registerRevisionsList(registry);
	registerRevisionsCompare(registry);
	registerRevisionsRestore(registry);
}

} // namespace lmms
