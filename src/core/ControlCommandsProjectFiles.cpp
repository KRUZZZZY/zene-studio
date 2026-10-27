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
#include "SessionCheckpoint.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MissingDevices.h"
#include "ProjectIds.h"
#include "ProjectRevisions.h"
#include "Song.h"
#include "UndoHistory.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! 0.5.0 Q050-018: the placeholders the session holds, as `missing_device_count` / `missing_devices`.
void insertMissingDevices(QJsonObject& result, Song* song)
{
	QJsonArray devices;
	for (const MissingDevice& device : missingdevices::collect(song)) { devices.append(device.toJson()); }
	result.insert(QStringLiteral("missing_device_count"), devices.size());
	result.insert(QStringLiteral("missing_devices"), devices);
}

//! The file hash of a saved/restored revision, reported to the caller.
QString sha256OfFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Algorithm::Sha256);
	if (!hash.addData(&file)) { return QString(); }
	return QString::fromLatin1(hash.result().toHex());
}

//! The recorded UNDO half of project.restore_revision when the restore CREATED
//! the project file: a file that never existed cannot be restored from revision
//! 0, so undo removes it. The same shape as revisions.restore's undo
//! (ControlCommandsRevisions.cpp, undoRestoreStep).
void removeRestoreCreatedFile(const QString& target)
{
	const bool removed = QFile::remove(target);
	const QFileInfo remaining(target);
	if (!removed && (remaining.exists() || remaining.isSymLink()))
	{
		control::recordActionRestoreFailure(
			QStringLiteral("the project file %1, created by project.restore_revision, could NOT be "
				"removed during undo - the path still exists").arg(target));
	}
}

//! The revision of \a projectPath whose bytes hash to \a sha256, or -1 when the
//! retained set holds no such revision.
int revisionHoldingBytes(const QString& projectPath, const QString& sha256)
{
	if (sha256.isEmpty()) { return -1; }
	for (const QJsonValue& value
		: control::projectRevisionState(projectPath).value(QStringLiteral("revisions")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("sha256")).toString() == sha256)
		{
			return entry.value(QStringLiteral("revision")).toInt();
		}
	}
	return -1;
}

//! The `__transaction` project.save records, honest PER CALL (BUG-CTL-6).
//! `revisionKept` is true only when the rotation actually retained a revision.
//! An inverse is recorded only when that rotation also completed policy cleanup:
//! a stale cleanup refusal makes the same rotation refuse during restore.
//! Recording `reversible: false` is the honest shape and is permitted by the
//! contract (ControlRegistry::stampContract lets a handler record less than its
//! class allows, never more).
QJsonObject saveTransaction(const QString& target, bool refused, bool revisionKept)
{
	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"),
		QJsonObject{{QStringLiteral("file"), target},
			{QStringLiteral("previous_revision_replaced"), revisionKept ? 0 : -1}});
	const bool inverseAvailable = revisionKept && !refused;
	if (inverseAvailable)
	{
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
	}
	transaction.insert(QStringLiteral("reversible"), inverseAvailable);
	QString mechanism;
	if (!revisionKept)
	{
		mechanism = refused
			? QStringLiteral("snapshot only: the file is over the 'keep-3' policy's per-revision "
				"cap, so no revision was kept and no inverse is offered")
			: QStringLiteral("snapshot only: this is the file's first save - there was no "
				"previous revision to keep, so no revision 0 exists and no inverse is offered");
	}
	else if (refused)
	{
		mechanism = QStringLiteral("revision 0 was retained, but stale revision cleanup failed; "
			"project.restore_revision would be refused by the same rotation failure, so no inverse is offered");
	}
	else
	{
		mechanism = QStringLiteral("file revision: the replaced file is kept as revision 0 of "
			"the named 'keep-3' policy and project.restore_revision restores it");
	}
	transaction.insert(QStringLiteral("mechanism"), mechanism);
	return transaction;
}


// ---------------------------------------------------------------------------
// project.save (SPEC A16 deliverable 4: the previous revision is kept)
// ---------------------------------------------------------------------------

//! Q050-007: the format a project.save to \a target writes. `format` absent
//! follows the session (Song::plannedSaveFormat - legacy unless the global
//! opt-in is on or the project already is a v2 container); "legacy" and "v2"
//! name it. "v2" for a path that is not `.mmpz` is refused, typed, BEFORE any
//! revision is rotated: a container is only ever a `.mmpz`.
bool requestedSaveFormat(const QJsonObject& args, const Song& song, const QString& target,
	Song::SaveFormat* format, ControlResult* error)
{
	const QString name = args.value(QStringLiteral("format")).toString();
	if (name.isEmpty()) { *format = song.plannedSaveFormat(target); return true; }
	if (name == QLatin1String("legacy")) { *format = Song::SaveFormat::Legacy; return true; }
	if (!Song::isContainerPath(target))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("format \"v2\" writes a container to a path ending in exactly \".mmpz\" (lowercase), "
				"and '%1' does not").arg(target));
		return false;
	}
	*format = Song::SaveFormat::V2Container;
	return true;
}

//! What a save wrote, said in the reply: the format, the file's size, and - for a container - the cost.
void describeSavedFormat(QJsonObject* result, const Song& song, const QString& target)
{
	const bool v2 = song.lastSavedFormat() == Song::SaveFormat::V2Container;
	result->insert(QStringLiteral("format"), v2 ? QStringLiteral("v2") : QStringLiteral("legacy"));
	result->insert(QStringLiteral("bytes"), static_cast<qint64>(QFileInfo(target).size()));
	if (v2) { result->insert(QStringLiteral("format_note"), Song::containerV2Note()); }
}


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
		"for the save it recorded. Q050-007: `format` \"v2\" saves the project as a v2 CONTAINER "
		"(a .mmpz ZIP of one entry per section): a LARGER file than the legacy single-blob .mmpz, "
		"which builds older than this one cannot open (`format_note` says so). Without `format` the "
		"legacy .mmpz is written unless the setting app/savecontainerv2 is \"1\" or the project was "
		"opened from a container; \"legacy\" forces the old blob. project.open tells both by content.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		// Q050-007: the opt-in v2 container. Absent, a save writes what the session
		// already has: the legacy single-blob .mmpz, unless the global setting
		// app/savecontainerv2 is on or the project was opened from a container.
		{QStringLiteral("format"), enumProperty({QStringLiteral("legacy"), QStringLiteral("v2")})},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("saved"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		{QStringLiteral("revision_kept"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		{QStringLiteral("revisions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
		{QStringLiteral("revision_skipped"), stringProperty()},
		// WF-033: what the save did with the undo history (docs/UNDO-HISTORY-PERSISTENCE.md).
		{QStringLiteral("undo_history"), objectProperty()},
		// Q050-007: the format the file was written in, its size, and for a v2 container the cost.
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("format_note"), stringProperty()},
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

		// The engine can state a reason of its own - a partial load is the
		// one it has (ARCH-4 S2b) - and reporting it is the difference
		// between "try again" and "this session cannot be saved as it
		// stands, and reopening it whole is the fix".
		const auto refusalOf = [&song, &target]() -> ControlResult {
			const QString refusal = song->saveRefusal();
			return ControlResult::failure(ControlErrorKind::Refused,
				refusal.isEmpty()
					? QStringLiteral("the engine refused to write %1").arg(target)
					: QStringLiteral("%1: %2").arg(target, refusal));
		};

		// A refusal the engine ALREADY knows about is answered before the
		// rotation: rotateProjectRevision shifts rev0 -> rev1 -> rev2 and
		// drops the oldest, so rotating first charged a refused save for the
		// revision history it never replaced (keep-3, so a full set lost its
		// oldest entry to a write that did not happen). isPartialLoad() is the
		// engine's own stated precondition for refusing (include/Song.h:142),
		// so the gate names the same fact the engine would.
		if (song->isPartialLoad())
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1: this session was loaded PARTIALLY: %2 section(s) are not in "
					"memory (%3), so saving would write a document missing them. Reopen the "
					"project without \"sections\" to save it.")
					.arg(target)
					.arg(song->notLoadedSections().size())
					.arg(song->notLoadedSections().join(QStringLiteral(", "))));
		}

		Song::SaveFormat format = Song::SaveFormat::Legacy;
		ControlResult formatError;
		if (!requestedSaveFormat(args, *song, target, &format, &formatError)) { return formatError; }

		// SPEC A16 deliverable 4: keep the previous revision BEFORE it is
		// overwritten. A file over the per-revision cap is not copied at all (a
		// truncated project is a corrupt one) and the result says so.
		bool refused = false;
		QString refusedReason;
		const int revisionsKept = control::rotateProjectRevision(target, &refused, &refusedReason);
		// A revision was actually retained only when the rotation copied one
		// (BUG-CTL-6): a first save has no file to replace and a cap refusal
		// copies nothing, and both return 0 here. Everything the reply claims as
		// reversible is gated on this, not on `!refused`.
		const bool revisionKept = revisionsKept > 0;

		// WF-033: a real save - the undo history goes with it when the project opted in.
		const UndoHistory::SaveScope historyScope;
		if (!song->saveProjectFile(target, false, format))
		{
			return refusalOf();
		}

		// The file is written; the SESSION state a successful save records is the
		// step this handler used to skip, so its reply said saved:true while
		// get_state still said modified:true with no file name and the bare save
		// and control.quit {save:true} then refused (BUG-SAVE-01).
		song->noteProjectSaved(target);
		song->setContainerV2Project(song->lastSavedFormat() == Song::SaveFormat::V2Container);

		QJsonObject result;
		result.insert(QStringLiteral("file"), target);
		result.insert(QStringLiteral("saved"), true);
		describeSavedFormat(&result, *song, target);
		result.insert(QStringLiteral("revision_kept"), revisionKept);
		result.insert(QStringLiteral("revisions"), control::projectRevisionState(target));
		result.insert(QStringLiteral("undo_history"), UndoHistory::instance().lastSave().toJson());
		if (refused) { result.insert(QStringLiteral("revision_skipped"), refusedReason); }
		else if (!revisionKept)
		{
			// Not a refusal and not a cap: a first save simply had nothing to
			// keep, and the reply says which of the two it was (BUG-CTL-6).
			result.insert(QStringLiteral("revision_skipped"), QStringLiteral("there was no "
				"previous revision to keep: this is the file's first save"));
		}

		result.insert(QStringLiteral("__transaction"), saveTransaction(target, refused, revisionKept));
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
		// redo re-restores the revision this call chose. Where there was NO live
		// file, nothing was rotated in: the undo removes the file the restore
		// created instead (new-p15-24), because revision 0 cannot undo a creation.
		//
		// The step is recorded AFTER the restore has succeeded (Luna cluster
		// 5737): restoreProjectRevision() is the call that can refuse - the
		// revision is outside the policy, or is simply not there - and a step
		// registered before it left a failed command's phantom behind, whose
		// undo "restored" a file the command never touched. The caller's next
		// control.undo then spent itself on that phantom and moved the real step
		// before it one further from the keyboard.
		//
		// Read BEFORE the restore replaces the file: it decides what the undo is.
		const bool liveExisted = QFileInfo::exists(target);
		if (!control::restoreProjectRevision(target, revision, &error))
		{
			return ControlResult::failure(ControlErrorKind::NotFound, error);
		}
		// What this restore brought in, identified by its CONTENT (Luna cluster
		// 5738). The number the caller named is not a stable identity: this
		// restore rotated the live file in, the recorded undo rotates it in again,
		// and every rotation shifts rev0 -> rev1 -> rev2 - so by the time the redo
		// ran, revision N held a different file and the redo wrote THAT over the
		// live one, undoing nothing the caller could see.
		const QString restoredSha = sha256OfFile(target);
		// The recorded halves REPORT their file operation, they do not swallow
		// it: a journal action's closure is `void`, so a restore that could not
		// put the file back has no other way to reach the caller - and the
		// sibling command (`revisions.restore`) reports the same failure the
		// same way, through the same channel.
		control::addUndoStep(
			[target, liveExisted]() {
				if (!liveExisted) { removeRestoreCreatedFile(target); return; }
				QString reason;
				if (control::restoreProjectRevision(target, 0, &reason)) { return; }
				control::recordActionRestoreFailure(
					QStringLiteral("the project file %1 was NOT put back from the revision this "
						"restore rotated in (revision 0) - %2. The live file still holds what it "
						"held when the undo ran; project.get_state reports the retained set and "
						"project.restore_revision restores what it still holds").arg(target, reason));
			},
			[target, restoredSha]() {
				QString reason;
				// -1: the undo's own rotation left the set in a shape where these
				// bytes are no longer retained. Restoring some other revision
				// instead would be a wrong-bytes overwrite, so the redo leaves the
				// file as the undo left it - and says that it did.
				const int current = revisionHoldingBytes(target, restoredSha);
				if (current < 0)
				{
					control::recordActionRestoreFailure(
						QStringLiteral("the project file %1 was NOT re-restored: the bytes this "
							"restore brought in (sha256 %2) are no longer retained in the "
							"keep-3 set, so redo leaves the live file exactly as the undo left "
							"it. project.get_state reports the retained set").arg(target, restoredSha));
					return;
				}
				if (control::restoreProjectRevision(target, current, &reason)) { return; }
				control::recordActionRestoreFailure(
					QStringLiteral("the project file %1 was NOT re-restored from revision %2 - %3. "
						"The live file still holds what it held when the redo ran; "
						"project.get_state reports the retained set and restores what it still "
						"holds").arg(target).arg(current).arg(reason));
			});

		QJsonObject result;
		result.insert(QStringLiteral("file"), target);
		result.insert(QStringLiteral("revision"), revision);
		result.insert(QStringLiteral("restored"), true);
		result.insert(QStringLiteral("sha256"), restoredSha);
		result.insert(QStringLiteral("revisions"), control::projectRevisionState(target));
		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("file"), target},
				{QStringLiteral("revision"), revision}});
		// A restore that CREATED the file is inverted by control.undo, the recorded step that
		// removes it (the same shape as revisions.restore's created-file case); otherwise the
		// inverse is the revision-0 restore that step performs.
		transaction.insert(QStringLiteral("inverse"), liveExisted
			? QJsonObject{{QStringLiteral("op"), QStringLiteral("project.restore_revision")},
				{QStringLiteral("args"), QJsonObject{{QStringLiteral("path"), target},
					{QStringLiteral("revision"), 0}}}}
			: QJsonObject{{QStringLiteral("op"), QStringLiteral("control.undo")},
				{QStringLiteral("args"), QJsonObject()}});
		transaction.insert(QStringLiteral("mechanism"), liveExisted
			? QStringLiteral("action checkpoint: the live file was rotated in as revision 0 before "
				"the restore, so the recorded undo step restores it")
			: QStringLiteral("action checkpoint: the project file did not exist before this call, so "
				"the recorded undo step REMOVES the file the restore created - a file that never "
				"existed cannot be restored from revision 0"));
		transaction.insert(QStringLiteral("reversible"), true);
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
		{QStringLiteral("playing"), booleanProperty()},
		{QStringLiteral("tracks"), arrayProperty()},
		// 0.5.0 Q050-018: the placeholders the session holds now (same shape as project.open's list)
		{QStringLiteral("missing_device_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("missing_devices"), arrayProperty()},
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
		insertMissingDevices(result, song);
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

//! The transaction record of one project.open: what it displaced, and - when the displaced session was
//! checkpointed (Q050-044) - the one undo step that restores it.
QJsonObject projectOpenTransaction(const QString& previousFile, const QString& previousSha,
	const control::SessionCheckpoint& checkpoint)
{
	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"),
		QJsonObject{{QStringLiteral("previous_file"), previousFile},
			{QStringLiteral("previous_sha256"), previousSha}});
	if (checkpoint.pushed)
	{
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("restore the displaced session")},
				{QStringLiteral("applies"), QStringLiteral("journal")}});
		transaction.insert(QStringLiteral("reversible"), true);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("snapshot: the session this open displaced, with its unsaved changes, "
				"and the opened result are captured whole (%1 bytes compressed in total, audio files by reference) "
				"and one undo "
				"step puts it back, file name included; redo restores the project as it was loaded, "
				"including any skipped sections")
				.arg(checkpoint.retainedBytes));
		return transaction;
	}
	transaction.insert(QStringLiteral("inverse"),
		QJsonObject{{QStringLiteral("op"), QStringLiteral("project.open")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("path"), previousFile}}}});
	transaction.insert(QStringLiteral("reversible"), false);
	transaction.insert(QStringLiteral("mechanism"),
		QStringLiteral("none: %1; only the file path and its hash are recorded, so the caller can see "
			"what it replaced")
			.arg(checkpoint.note.isEmpty() ? QStringLiteral("the open did not complete") : checkpoint.note));
	return transaction;
}


void registerProjectOpen(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.open");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("open");
	cmd.description = QStringLiteral("Load a project file into this running instance.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		// SPEC-ARCH-4 1.4 (ARCH-4 S2b): name the sections this load should NOT
		// read. Their subtrees are removed from the document's bytes before
		// anything parses them, so nothing is built for them and the rest of the
		// file loads exactly as it would have. Absent or empty means a whole
		// load, which is what every caller did before this existed; unknown
		// names are simply not sections and are skipped over without effect.
		{QStringLiteral("sections"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), stringProperty()}}},
	}, {QStringLiteral("path")});
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
		// SPEC-ARCH-4 1.4 (ARCH-4 S2b): the reader half. `sections` above names
		// what NOT to load; these three say what that cost. `not_loaded` is in
		// DOCUMENT order and lists the sections actually REMOVED - a name the
		// document does not carry skips nothing and appears nowhere - and it is
		// NOT the same answer as `unclaimed`: unclaimed is content this build
		// could not understand and preserved, not_loaded is content the CALLER
		// asked it not to read. A load can report either, both or neither.
		// `partial` is `not_loaded_count > 0`, and it is the flag that matters:
		// such a session holds less than its file and is read-only, which
		// project.save reports as a typed refusal rather than a silent subset.
		{QStringLiteral("not_loaded_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("not_loaded"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), stringProperty()}}},
		{QStringLiteral("partial"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		// 0.5.0 Q050-018: the instruments and effects the file names that this session could not load. Each keeps
		// its original XML (saved back unchanged); {kind, name, format, vendor, track, reason, identity}. Never
		// blocks the open and is not a load error.
		{QStringLiteral("missing_device_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("missing_devices"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		// Q050-044: what became of the displaced session - captured for one undo, or why not.
		{QStringLiteral("checkpoint"), objectProperty()},
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
		// Q050-044: a session with unsaved changes is checkpointed first (bounded by the undo
		// stack's byte budget) and the load leaves ONE undo step that puts it back; a checkpoint
		// over the budget refuses the open before either project is touched. The record still
		// names what was displaced (file and hash) for a session that was saved.
		const QString previousFile = song->projectFileName();
		const QString previousSha = previousFile.isEmpty() ? QString() : sha256OfFile(previousFile);
		// SPEC-ARCH-4 1.4 (ARCH-4 S2b): the sections to skip, as the caller named
		// them. Taken in the order given and passed through unchanged - the
		// reducer reports back what it ACTUALLY removed, so a name that is not a
		// section of this document costs nothing and is not echoed as if it had
		// been skipped.
		QStringList skipSections;
		for (const QJsonValue& value : args.value(QStringLiteral("sections")).toArray())
		{
			skipSections.append(value.toString());
		}
		control::SessionCheckpoint checkpoint;
		QString checkpointRefusal;
		if (!control::loadProjectWithCheckpoint(path, skipSections, &checkpoint, &checkpointRefusal))
		{
			return ControlResult::failure(ControlErrorKind::Refused, checkpointRefusal);
		}

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
		// What this load was ASKED to leave out and did (SPEC-ARCH-4 1.4, ARCH-4
		// S2b), in document order and for the same reason `unclaimed` is: the
		// order is the document's, not Qt's. Reported from the engine's own
		// record of what the reducer removed, never from the request - a caller
		// that named a section this document does not carry is told nothing went,
		// because nothing did.
		const QStringList notLoaded = song->notLoadedSections();
		QJsonArray notLoadedNames;
		for (const QString& sectionName : notLoaded) { notLoadedNames.append(sectionName); }
		result.insert(QStringLiteral("not_loaded"), notLoadedNames);
		result.insert(QStringLiteral("not_loaded_count"), notLoadedNames.size());
		result.insert(QStringLiteral("partial"), song->isPartialLoad());
		insertMissingDevices(result, song);
		result.insert(QStringLiteral("checkpoint"), checkpoint.toJson());
		const QJsonObject transaction = projectOpenTransaction(previousFile, previousSha, checkpoint);
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
