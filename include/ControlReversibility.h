/*
 * ControlReversibility.h - the SPEC A16 reversibility contract of the agent
 *                          control surface: the classification of every
 *                          registered command, and the shared undo-step helper.
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

#ifndef LMMS_CONTROL_REVERSIBILITY_H
#define LMMS_CONTROL_REVERSIBILITY_H

#include <functional>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "lmms_export.h"

namespace lmms
{

class JournallingObject;

namespace control
{

//! SPEC A16's classification of a command, one value per registered command.
//!
//!   TrueInverse   a live JournallingObject checkpoint restores it: the
//!                 engine's own ProjectJournal replays the object's saved XML.
//!                 This covers the plain (one object), the composite (many
//!                 objects restored as ONE step) and the action (a recorded
//!                 undo action that runs when the stack unwinds) checkpoint.
//!   Snapshot      no live object can be restored. The inverse is a *bounded
//!                 recorded state* - a container snapshot, a file revision, a
//!                 scalar - and it is either replayed by a recorded inverse
//!                 command or named as the documented manual fallback.
//!   Irreversible  no inverse exists in this engine, by nature of the command
//!                 (out-of-band mutation, destruction of state that is not
//!                 captured). An undo attempt must FAIL, typed, never pretend.
//!   NotMutating   the command changes no project state; there is nothing to
//!                 reverse. (A command is not "reversible" for doing nothing.)
enum class ReversibilityClass
{
	NotMutating,
	TrueInverse,
	Snapshot,
	Irreversible,
};

//! Wire name of \p cls ("not_mutating" | "true_inverse" | "snapshot" |
//! "irreversible"); the string an agent reads in a transaction record.
LMMS_EXPORT QString reversibilityClassName(ReversibilityClass cls);
//! Parses a wire name; NotMutating for anything unknown.
LMMS_EXPORT ReversibilityClass reversibilityClassFromName(const QString& name);

//! One row of the contract table, in its raw (static) form; the strings are
//! UTF-8 literals so the table can live in a data-only translation unit.
struct ReversibilityRow
{
	const char* command;
	ReversibilityClass cls;
	const char* reason;
	const char* mechanism;
	const char* fallback; //!< empty string when the inverse is automatic
	bool reversible;      //!< the class's default verdict for a call that succeeds
};

//! The rows of THE classification table (ControlReversibilityTable.cpp).
LMMS_EXPORT const ReversibilityRow* reversibilityRowTable(int* rowCount);

//! One row of the contract table: what the command is, why, and what the
//! engine actually provides.
struct ReversibilityEntry
{
	QString command;                        //!< the registered command id
	ReversibilityClass cls = ReversibilityClass::NotMutating;
	QString reason;                         //!< why it is in this class
	QString mechanism;                      //!< the mechanism the engine provides
	//! For Irreversible rows (and Snapshot rows whose inverse is not
	//! automatic): what an agent or a human should do instead. Empty when the
	//! inverse is applied automatically.
	QString fallback;
	//! The class's default verdict: true when the mechanism applies the inverse
	//! by itself, false when the recorded state is a manual fallback only.
	bool reversible = false;
};

//! THE classification table: one row for every registered command.
//!
//! It is a single file on purpose. A table spread over twelve command groups
//! cannot be reviewed as a table, and the point of this artifact is that an
//! agent (or a reviewer) can read the whole contract in one place. The
//! anti-drift test (tests/src/core/ReversibilityContractTest.cpp) asserts both
//! directions: every row names a registered command, and every registered
//! command has a row.
class LMMS_EXPORT ReversibilityTable
{
public:
	static ReversibilityTable& instance();

	//! The row for \p command; nullptr when the table has none (which the
	//! anti-drift test treats as a failure, not as "nothing to do").
	const ReversibilityEntry* lookup(const QString& command) const;
	QStringList commandIds() const;
	QVector<ReversibilityEntry> entries() const;

private:
	ReversibilityTable();

	QHash<QString, ReversibilityEntry> m_entries;
};

// ---------------------------------------------------------------------------
// The transaction record (SPEC A16 deliverable 2): what the registry keeps per
// mutating command, and the bounds it keeps them within.
// ---------------------------------------------------------------------------

//! Hard cap on retained transaction records. The engine's own undo depth
//! (ProjectJournal::MAX_UNDO_STATES) is also 100, so the audit record and the
//! undo stack evict at the same point and an agent never sees a record for a
//! step it can no longer undo.
constexpr int MaxTransactionRecords = 100;
//! Hard cap on the total serialised size of the retained records. A record
//! carries the before-state plus the inverse descriptor; the per-record
//! before-state caps live with the commands that fill them (a device state XML
//! is capped at 64 KiB, a track snapshot at the same). The registry enforces
//! this total on top, so a long session cannot grow the record without bound.
constexpr int MaxTransactionBytes = 262144;

//! One undo step for a change that has no live object to restore - a created
//! or deleted track/channel/automation track, a scalar owned by a subsystem the
//! engine does not journal, a view selection.
//!
//! The step is pushed onto the engine's OWN undo stack (ProjectJournal), so an
//! agent's command and a user's Ctrl+Z are one history: the GUI's Edit->Undo
//! runs ProjectJournal::undo(), which runs \p undo. \p redo is the reverse, so
//! the GUI's redo is faithful too and not a silently-dropped step.
LMMS_EXPORT void addUndoStep(std::function<void()> undo,
	std::function<void()> redo = std::function<void()>());

//! One undo step covering SEVERAL live objects at once (SPEC A16 deliverable
//! 3: one agent command = one undoable step). Used where one agent action
//! writes more than one object - track.set_solo writes every track's mute.
//! Restoring them one checkpoint at a time would cost one Ctrl+Z each, i.e. N
//! undo steps for one command, which is the defect this exists to prevent.
LMMS_EXPORT void addUndoStep(const QVector<JournallingObject*>& journallingObjects);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_REVERSIBILITY_H
