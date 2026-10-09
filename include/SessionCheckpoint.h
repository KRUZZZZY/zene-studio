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
 *  The checkpoint is bounded by the undo stack's byte budget
 *  (ProjectJournal::maxUndoBytes, control.set_undo_depth): a session whose
 *  compressed snapshot is over it is REFUSED before anything changes, because a
 *  step bigger than the budget cannot be kept and "undoable" would be a claim
 *  the engine cannot honour. */
struct SessionCheckpoint
{
	enum class State
	{
		Skipped,  //!< nothing to protect, or nothing that can be captured: see \a note
		Taken,    //!< the snapshot is held in \a before
		TooLarge, //!< over the budget: the destructive action must not run
	};

	State state = State::Skipped;
	std::shared_ptr<const QByteArray> before;
	qint64 bytes = 0;      //!< the compressed snapshot, when one was measured
	qint64 limit = 0;      //!< the byte budget it was measured against
	QString fileBefore;    //!< the project file name the session had
	QString note;          //!< the plain-words reason when not Taken
	bool pushed = false;   //!< the undo step is on the journal

	bool taken() const { return state == State::Taken; }
	bool refused() const { return state == State::TooLarge; }
	//! The reply field both commands report: taken, pushed, bytes, limit and the note.
	LMMS_EXPORT QJsonObject toJson() const;
};

/*! Captures the session for \a action ("project.open", "script.run"). With
 *  \a onlyWhenModified, a session with no unsaved changes is Skipped: its file
 *  already holds it. A partial load (sections skipped) is Skipped too - a
 *  snapshot of part of a project would "restore" a smaller project. */
LMMS_EXPORT SessionCheckpoint takeSessionCheckpoint(const QString& action, bool onlyWhenModified);

/*! project.open's shared body (the control command and the interface's open
 *  paths): checkpoint the displaced session, load \a path, and on a good load
 *  push the one step that returns to the displaced session. False, with
 *  \a refusal set and NOTHING changed, when the checkpoint is over the budget. */
LMMS_EXPORT bool loadProjectWithCheckpoint(const QString& path, const QStringList& skipSections,
	SessionCheckpoint* checkpoint, QString* refusal);

/*! After a script ran: when the project differs from \a checkpoint, replace the
 *  finer journal steps the script pushed (everything newer than
 *  \a serialBefore) with the ONE step that restores the checkpoint. Sets
 *  \a changed to whether the project differs. */
LMMS_EXPORT void finishScriptCheckpoint(SessionCheckpoint* checkpoint, quint64 serialBefore,
	bool* changed);

} // namespace control
} // namespace lmms

#endif // LMMS_SESSION_CHECKPOINT_H
