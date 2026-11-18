/*
 * SessionCheckpoint.h - Q050-044: one undo step that puts the whole session back
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

#ifndef LMMS_SESSION_CHECKPOINT_H
#define LMMS_SESSION_CHECKPOINT_H

#include <memory>

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{
namespace control
{

/*! The two operations that replace or rewrite more of the session than the
 *  registry can describe - project.open (the whole session is replaced) and
 *  script.run (a Lua script writes through its own bindings) - take the session
 *  as it is BEFORE they act, as the same compressed document an undo-history
 *  step carries (Song::captureUndoSnapshot: the save document, audio files by
 *  reference, never by copy), and leave ONE undo step that restores it.
 *
 *  The checkpoint and its redo state together are bounded by the undo stack's
 *  byte budget (ProjectJournal::maxUndoBytes, control.set_undo_depth). A large
 *  pre-state is refused before the action; if the completed action's snapshot
 *  makes the pair too large, the pre-state is restored before refusal. Partial
 *  loads carry their skipped-section list as metadata, so undo and redo preserve
 *  their read-only state. */
struct SessionCheckpoint
{
	enum class State
	{
		Skipped,  //!< nothing to protect, or nothing that can be captured: see \a note
		Taken,    //!< the snapshot is held in \a before
		TooLarge, //!< both retained snapshots do not fit; a completed action is restored before refusal
	};

	State state = State::Skipped;
	std::shared_ptr<const QByteArray> before;
	qint64 bytes = 0;          //!< compressed pre-state snapshot, when one was measured
	qint64 retainedBytes = 0;  //!< combined pre-state and redo-state payload, when a step was pushed
	qint64 limit = 0;          //!< the byte budget it was measured against
	QString fileBefore;        //!< the project file name the session had
	QStringList skippedBefore; //!< sections omitted from a partial session before the action
	//! The container format each captured session had: a restore keeps the format of the session it
	//! replaces, so the format of a restored snapshot has to travel with the checkpoint.
	bool containerV2Before = false; //!< the pre-action session was a v2 container
	bool containerV2After = false;  //!< the post-action session was a v2 container (the redo state)
	QString note;              //!< the plain-words reason when not Taken
	bool pushed = false;       //!< the undo step is on the journal

	bool taken() const { return state == State::Taken; }
	bool refused() const { return state == State::TooLarge; }
	//! The reply field both commands report: taken, pushed, bytes, retained_bytes, limit and the note.
	LMMS_EXPORT QJsonObject toJson() const;
};

/*! Captures the session for \a action ("project.open", "script.run"). With
 *  \a onlyWhenModified, a session with no unsaved changes is Skipped: its file
 *  already holds it. */
LMMS_EXPORT SessionCheckpoint takeSessionCheckpoint(const QString& action, bool onlyWhenModified);

/*! project.open's shared body (the control command and the interface's open
 *  paths): checkpoint the displaced session, load \a path, and on a good load
 *  push the one step that returns to the displaced session. False, with
 *  \a refusal set and NOTHING changed, when both retained snapshots do not fit. */
LMMS_EXPORT bool loadProjectWithCheckpoint(const QString& path, const QStringList& skipSections,
	SessionCheckpoint* checkpoint, QString* refusal);

/*! After a script ran: when the project differs from \a checkpoint, replace the
 *  finer journal steps the script pushed (everything newer than
 *  \a serialBefore) with the ONE step that restores the checkpoint. Sets
 *  \a changed to whether the project differs; if the snapshot pair does not fit,
 *  it restores the checkpoint and sets \a refusal. */
LMMS_EXPORT void finishScriptCheckpoint(SessionCheckpoint* checkpoint, quint64 serialBefore,
	bool* changed, QString* refusal);

} // namespace control
} // namespace lmms

#endif // LMMS_SESSION_CHECKPOINT_H
