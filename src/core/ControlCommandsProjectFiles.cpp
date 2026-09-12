/*
 * ControlCommandsProject.cpp - the project.* and render.render commands.
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
#include <QJsonObject>

#include "ControlRegistry.h"

#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
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


} // namespace

void registerProjectFilesCommands(ControlRegistry& registry)
{
	registerProjectSave(registry);
	registerProjectRestoreRevision(registry);
	registerProjectGetState(registry);
}

} // namespace lmms
