/*
 * ControlCommandsProjectFiles.cpp - the project file commands: open, save,
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

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"

#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectIds.h"
#include "ProjectRevisions.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The file hash of a saved/restored revision, reported to the caller.
QString sha256OfFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Algorithm::Sha256);
	if (!hash.addData(&file)) { return QString(); }
	return QString::fromLatin1(hash.result().toHex());
}

// ---------------------------------------------------------------------------
// project.save (SPEC A16 deliverable 4: the previous revision is kept)
// ---------------------------------------------------------------------------


void registerProjectSave(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.save");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("save");
	cmd.description = QStringLiteral("Save the session. With no path, saves over the project's own "
		"file. Before the write, the file being replaced is rotated into the named 'keep-3' "
		"revision set (<file>.rev0..rev2, each capped at 8 MiB), so the previous revision is "
		"recoverable through project.restore_revision - and control.undo dispatches exactly that "
		"for the save it recorded.");
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("saved"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		{QStringLiteral("revision_kept"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		{QStringLiteral("revisions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		Song* song = Engine::getSong();
		QString target = args.value(QStringLiteral("path")).toString();
		if (target.isEmpty()) { target = song->projectFileName(); }
		if (target.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("no 'path' given and the session has no project file yet"));
		}

		// SPEC A16 deliverable 4: keep the previous revision BEFORE it is
		// overwritten. A file over the per-revision cap is not copied at all (a
		// truncated project is a corrupt one) and the result says so.
		bool refused = false;
		QString refusedReason;
		control::rotateProjectRevision(target, &refused, &refusedReason);

		if (!song->saveProjectFile(target))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the engine refused to write %1").arg(target));
		}

		QJsonObject result;
		result.insert(QStringLiteral("file"), target);
		result.insert(QStringLiteral("saved"), true);
		result.insert(QStringLiteral("revision_kept"), !refused);
		result.insert(QStringLiteral("revisions"), control::projectRevisionState(target));
		if (refused) { result.insert(QStringLiteral("revision_skipped"), refusedReason); }

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("file"), target},
				{QStringLiteral("previous_revision_replaced"), refused ? -1 : 0}});
		// The inverse is a COMMAND, not a live object: the file is not project
		// state and has no JournallingObject, so control.undo dispatches this
		// instead of unwinding the model's undo stack (see
		// docs/A16-REVERSIBILITY.md for why a file write stays off the GUI
		// stack).
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("project.restore_revision")},
				{QStringLiteral("applies"), QStringLiteral("command")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("path"), target},
						{QStringLiteral("revision"), 0}}}});
		transaction.insert(QStringLiteral("reversible"), !refused);
		transaction.insert(QStringLiteral("mechanism"), refused
			? QStringLiteral("snapshot only: the file is over the 'keep-3' policy's per-revision "
				"cap, so no revision was kept and no inverse is offered")
			: QStringLiteral("file revision: the replaced file is kept as revision 0 of the "
				"named 'keep-3' policy and project.restore_revision restores it"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


// ---------------------------------------------------------------------------
// project.restore_revision: the restore path, reachable through the control
// surface
// ---------------------------------------------------------------------------

void registerProjectRestoreRevision(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.restore_revision");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("restore_revision");
	cmd.description = QStringLiteral("Restore a retained revision of a project file OVER the live "
		"file (policy 'keep-3': revision 0 is the revision the last save replaced). The live file "
		"is rotated in first, so the restore is itself recoverable. The session in memory is NOT "
		"reloaded: call project.open afterwards to work on the restored bytes.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("revision"), integerProperty(0, 2)},
	}, {QStringLiteral("revision")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("revision"), integerProperty()},
		{QStringLiteral("restored"), booleanProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("revisions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		Song* song = Engine::getSong();
		QString target = args.value(QStringLiteral("path")).toString();
		if (target.isEmpty()) { target = song->projectFileName(); }
		if (target.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("no 'path' given and the session has no project file yet"));
		}
		const int revision = args.value(QStringLiteral("revision")).toInt();
		QString error;
		// The restore rotates the live file in as the new revision 0, so ONE
		// action step restoring revision 0 again is the exact inverse - and the
		// redo re-restores the revision this call chose.
		control::addUndoStep(
			[target]() {
				QString ignored;
				control::restoreProjectRevision(target, 0, &ignored);
			},
			[target, revision]() {
				QString ignored;
				control::restoreProjectRevision(target, revision, &ignored);
			});
		if (!control::restoreProjectRevision(target, revision, &error))
		{
			return ControlResult::failure(ControlErrorKind::NotFound, error);
		}

		QJsonObject result;
		result.insert(QStringLiteral("file"), target);
		result.insert(QStringLiteral("revision"), revision);
		result.insert(QStringLiteral("restored"), true);
		result.insert(QStringLiteral("sha256"), sha256OfFile(target));
		result.insert(QStringLiteral("revisions"), control::projectRevisionState(target));
		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("file"), target},
				{QStringLiteral("revision"), revision}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("project.restore_revision")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("path"), target},
						{QStringLiteral("revision"), 0}}}});
		transaction.insert(QStringLiteral("reversible"), true);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("action checkpoint: the live file was rotated in as revision 0 before "
				"the restore, so the recorded undo step restores it"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// project.get_state
// ---------------------------------------------------------------------------


void registerProjectGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.get_state");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("Project file, modified flag, tempo and track count.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("modified"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		{QStringLiteral("tempo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("track_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("revisions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
	});
	cmd.handler = [](const QJsonObject&) {
		Song* song = Engine::getSong();
		QJsonObject result;
		result.insert(QStringLiteral("file"), song->projectFileName());
		result.insert(QStringLiteral("modified"), song->isModified());
		result.insert(QStringLiteral("tempo"), song->getTempo());
		result.insert(QStringLiteral("track_count"), static_cast<int>(song->tracks().size()));
		result.insert(QStringLiteral("playing"), song->isPlaying());
		QJsonArray trackIds;
		for (int i = 0; i < static_cast<int>(song->tracks().size()); ++i)
		{
			trackIds.append(control::trackIdOf(song->tracks()[i]));
		}
		result.insert(QStringLiteral("tracks"), trackIds);
		// SPEC A16 deliverable 4: the recoverable revision set is REPORTED here
		// rather than assumed, so an agent can see what project.save kept.
		result.insert(QStringLiteral("revisions"),
			control::projectRevisionState(song->projectFileName()));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// project.open
// ---------------------------------------------------------------------------

void registerProjectOpen(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.open");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("open");
	cmd.description = QStringLiteral("Load a project file into this running instance.");
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}}, {QStringLiteral("path")});
	// SPEC A13: the load path must work with no display. A project that loads
	// with errors returns the per-item list here instead of stopping on the
	// "LMMS Error report" box, which in an agent instance nobody can click
	// (task #625).
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("track_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("tempo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("loaded_with_errors"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		{QStringLiteral("error_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("errors"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), objectSchema({
				{QStringLiteral("message"), stringProperty()},
				{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}})}}},
		// The id upgrade, reported rather than silent (SPEC-stable-ids.md 7, Q1).
		// `ids_assigned` counts the objects this load had to give an id to because
		// the file carried none (plus any duplicate-id repair); `format_upgraded`
		// is that count above zero. The upgrade is content-preserving and happens
		// in memory only - the file changes on the next project.save, which is
		// exactly why the caller is told.
		{QStringLiteral("ids_assigned"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("format_upgraded"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		// SPEC-ARCH-4 1.6.4 (ARCH-4 S1d): what the load PRESERVED rather than
		// loaded. A document written by a newer build carries elements this one
		// has no class for; since S1b/S1c they are kept verbatim and re-emitted
		// on save instead of being dropped, and THIS is how a caller learns
		// which ones - rather than discovering the loss on the next save, or
		// never. `unclaimed_count` is the array's own length, in the same
		// count/name_count shape as the load errors beside it. The list is in
		// DOCUMENT order (see the handler): an empty one means this build
		// understood the whole document.
		{QStringLiteral("unclaimed_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("unclaimed"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), stringProperty()}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString path = args.value(QStringLiteral("path")).toString();
		if (!QFileInfo::exists(path))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no such project file: %1").arg(path));
		}
		Song* song = Engine::getSong();
		// SPEC A16: the displaced session is NOT snapshotted, so the record says
		// what was replaced instead of pretending an inverse exists.
		const QString previousFile = song->projectFileName();
		const QString previousSha = previousFile.isEmpty() ? QString() : sha256OfFile(previousFile);
		song->loadProject(path);

		// A refused file (unparseable, or carrying local plugin paths) leaves
		// the session as it was; the reason is a typed refusal, not a modal.
		const QString refusal = song->loadRefusal();
		if (!refusal.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1: %2").arg(path, refusal));
		}

		QJsonObject result;
		result.insert(QStringLiteral("file"), song->projectFileName());
		result.insert(QStringLiteral("track_count"), static_cast<int>(song->tracks().size()));
		result.insert(QStringLiteral("tempo"), song->getTempo());
		// The per-item load errors, sorted so the answer is reproducible: each
		// entry is what failed (the sample or plugin path, in the message) and
		// why (the same sentence the "LMMS Error report" box used to show).
		QJsonArray errors;
		QStringList messages = song->errors().keys();
		messages.sort();
		for (const QString& message : messages)
		{
			errors.append(QJsonObject{{QStringLiteral("message"), message},
				{QStringLiteral("count"), song->errors().value(message)}});
		}
		result.insert(QStringLiteral("errors"), errors);
		result.insert(QStringLiteral("error_count"), errors.size());
		result.insert(QStringLiteral("loaded_with_errors"), !errors.isEmpty());
		// SPEC-stable-ids.md 7 Q1 (owner decision): a legacy file's first load is
		// a content-preserving one-time id UPGRADE, and the caller must be told
		// so rather than discover it when the file changes on the next save.
		const int idsAssigned = ProjectIds::loadAssignments();
		result.insert(QStringLiteral("ids_assigned"), idsAssigned);
		result.insert(QStringLiteral("format_upgraded"), idsAssigned > 0);
		// What this load preserved rather than loaded (SPEC-ARCH-4 1.6.4), in
		// DOCUMENT order - and that order is load-bearing, unlike the error list
		// above, which is sorted only because QHash::keys() has no order of its
		// own. Each path ends in the child index the re-emitted element occupies,
		// so sorting would destroy the very information the path carries.
		const QStringList unclaimed = song->unclaimedElements();
		QJsonArray unclaimedPaths;
		for (const QString& elementPath : unclaimed) { unclaimedPaths.append(elementPath); }
		result.insert(QStringLiteral("unclaimed"), unclaimedPaths);
		result.insert(QStringLiteral("unclaimed_count"), unclaimedPaths.size());
		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("previous_file"), previousFile},
				{QStringLiteral("previous_sha256"), previousSha}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("project.open")},
				{QStringLiteral("args"), QJsonObject{{QStringLiteral("path"), previousFile}}}});
		// Loading a project replaces the whole session; the engine keeps no
		// pre-load snapshot, so this transaction is documented, not reversible -
		// and control.undo says exactly that instead of undoing an older step.
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("none: the displaced session is not snapshotted; only the file path "
				"and its hash are recorded, so the caller can see what it replaced. UNSAVED "
				"changes to the previous session are lost"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


} // namespace

void registerProjectFilesCommands(ControlRegistry& registry)
{
	registerProjectOpen(registry);
	registerProjectSave(registry);
	registerProjectRestoreRevision(registry);
	registerProjectGetState(registry);
}

} // namespace lmms
