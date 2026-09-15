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
	//! The COALESCING declaration (Zene Studio, bounded undo): the argument
	//! name(s) - comma-separated - whose VALUES identify the thing a gesture is
	//! being made on, or nullptr for a command that never coalesces. Two
	//! consecutive calls of the same command with the same target values are
	//! ONE undo step; see docs/UNDO-BOUNDS.md and include/ProjectJournal.h.
	//! It is declared in the TABLE rather than in each command's handler so the
	//! whole coalescing contract can be read (and anti-drift-test) as data.
	const char* coalesceTarget;
};

/*! The rows of THE classification table. It is THREE literal blocks, split by
 *  WHAT THE INVERSE IS, and the first block is itself in two files along the
 *  same seam (within true_inverse the inverse is either the engine's own live
 *  object checkpoint or a recorded ACTION checkpoint):
 *
 *    ControlReversibilityTable.cpp         true_inverse - LIVE object checkpoint
 *    ControlReversibilityTableAction.cpp   true_inverse - ACTION checkpoint
 *    ControlReversibilityTableTrackFolder.cpp
 *                                          the folder-track group's rows (two
 *                                          live-checkpoint, five action), joined
 *                                          into the block above
 *    ControlReversibilityTableSnapshot.cpp  snapshot (a bounded recorded state,
 *                                           replayed by an inverse command or
 *                                           named as the manual fallback)
 *    ControlReversibilityTablePassive.cpp   irreversible + not_mutating
 *
 *  This function returns the true_inverse block JOINED across its files, so
 *  a caller still reads ONE block with ONE row count. ReversibilityTable's
 *  constructor reads all of them, so the contract is still read, and tested, as
 *  one table; the files are separate because this fork's file-length ratchet
 *  measures a file as a unit, and the true_inverse block alone had grown past
 *  the 500-line limit twice.
 */
LMMS_EXPORT const ReversibilityRow* reversibilityRowTable(int* rowCount);
//! The ACTION half of the first block: the true_inverse rows whose inverse is a
//! recorded operation rather than a live object checkpoint. Joined into
//! reversibilityRowTable(); not read by the constructor on its own.
LMMS_EXPORT const ReversibilityRow* reversibilityActionRowTable(int* rowCount);
//! The folder-track GROUP's rows (owner items 3+20+21): two live-checkpoint rows
//! and five recorded-action rows. Joined into reversibilityRowTable() as well,
//! so the block's class still comes from each row and not from its file.
LMMS_EXPORT const ReversibilityRow* reversibilityTrackFolderRowTable(int* rowCount);
//! The `vca.*` GROUP's twelve mutating rows (OWNER-31 item 11, phase-locked
//! multitrack edit groups): six live-checkpoint rows - the group's fader, mute
//! and solo models, the composite solo step, and the clip checkpoints a locked
//! edit takes - and six recorded-action rows for the state that is not a model
//! (a name, a membership list, a lock flag, a group's existence). Joined into
//! reversibilityRowTable() for the same reason the folder rows are: the block's
//! class comes from each row, not from its file. The group's two reads are
//! `not_mutating` and live with the other passive rows, because the table's
//! blocks are split by what the inverse IS and not by command group.
LMMS_EXPORT const ReversibilityRow* reversibilityVcaRowTable(int* rowCount);
//! The 0.3.0 verb wave's three LIVE-checkpoint rows (clip.trim, clip.slip,
//! note.probability_set). Joined into reversibilityRowTable() for the same
//! reason the folder rows are - the block's class comes from each row, not from
//! its file - and because the true_inverse half's own file sits at the file
//! ratchet. render.stems is NOT here: it is not_mutating and lives with the
//! other not_mutating rows in ControlReversibilityTablePassive.cpp.
LMMS_EXPORT const ReversibilityRow* reversibilityVerbRowTable(int* rowCount);
//! The 0.3.0 note/scale/device wave's fifteen rows (board task #648; feature-list
//! rows 11, 66 and 81): seven LIVE-checkpoint rows (the note randomisation, transform
//! and slide verbs, and scale.snap_notes), four recorded-ACTION rows (the Song's MIDI
//! seed, the scale group's root/scale context and the MPE input flag - none of them a
//! JournallingObject) and four not_mutating readers. Joined into
//! reversibilityRowTable() for the same reason the routing and scan-and-crash rows are:
//! the block's class comes from each row, not from its file.
LMMS_EXPORT const ReversibilityRow* reversibilityNoteScaleRowTable(int* rowCount);
//! The chain-preset GROUP's four recorded-action rows (OWNER-31 item 2: the
//! preset store is a file tree outside the project, so each command records the
//! undo step for its own file operation). Joined into the action half by
//! reversibilityActionRowTable(), and through it into reversibilityRowTable(),
//! for the same reason the folder rows are: the block's class comes from each
//! row, not from its file.
LMMS_EXPORT const ReversibilityRow* reversibilityChainRowTable(int* rowCount);
//! The second block: the snapshot rows.
LMMS_EXPORT const ReversibilityRow* reversibilitySnapshotRowTable(int* rowCount);
/*! The routing surface's rows: the pdc / routing / bus / port groups (feature
 * rows 27-29) and the mixer group's routing verbs. A GROUP's rows, whatever their
 * class - the class comes from each row, not from the file, exactly as
 * reversibilityTrackFolderRowTable's do. Joined into reversibilityRowTable().
 */
LMMS_EXPORT const ReversibilityRow* reversibilityRoutingRowTable(int* rowCount);
/*! The plugin scan-cache group's and the crash-reporter group's rows (feature
 * rows 46 and 54). A GROUP's rows, whatever their class - the class comes from
 * each row, not from the file, exactly as reversibilityRoutingRowTable's do.
 * Joined into reversibilityRowTable(): the passive block is at 477 of the 500
 * lines the file-length ratchet allows and the live block at 499, so the rows
 * land here rather than in either.
 */
LMMS_EXPORT const ReversibilityRow* reversibilityScanAndCrashRowTable(int* rowCount);
/*! The `mastering.*` group's three rows (feature rows 25 and 72): one
 *  recorded-action true_inverse row (mastering.run writes files in a directory
 *  outside the project) and the group's two not_mutating inspectors. A GROUP
 *  file on the same seam as the folder, vca and routing files - the class comes
 *  from each row, not from its file. Joined into reversibilityRowTable().
 */
LMMS_EXPORT const ReversibilityRow* reversibilityMasteringRowTable(int* rowCount);
//! The third block: the irreversible and the not_mutating rows.
LMMS_EXPORT const ReversibilityRow* reversibilityPassiveRowTable(int* rowCount);
/*! The `stem.*` group's seven not_mutating rows (feature row 26, board task
 *  #653): the offline stem-separation engine's read verb, its four job verbs and
 *  its two model-store verbs. A GROUP file on the same seam as the folder, vca,
 *  routing, scan/crash and mastering files - and, like the passive block itself,
 *  a file split off a block because the file-length ratchet reads a file as a
 *  unit. Joined into the passive block's rows by ReversibilityTable's
 *  constructor. The array is EMPTY without LMMS_HAVE_STEM_SPLIT: the ids do not
 *  exist in that configuration, and a row naming an unregistered command is a
 *  failure in the other direction (ReversibilityContractTest).
 */
LMMS_EXPORT const ReversibilityRow* reversibilityStemRowTable(int* rowCount);

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
	//! The argument name(s), comma-separated, whose VALUES identify the target
	//! of a repeatable gesture ("clip", "channel", "target,plugin,name,index").
	//! Empty for a command that never coalesces, which is every command that is
	//! not a drag-shaped edit of one named thing.
	QString coalesceTarget;

	//! True when a run of this command on ONE target is a single undo step.
	bool coalesces() const { return !coalesceTarget.isEmpty(); }
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

// ---------------------------------------------------------------------------
// The undo bound and the coalescing window (SPEC A16; task #623's "undo depth
// and drag coalescing"). The ENGINE's two caps live with the mechanism that
// enforces them (ProjectJournal::MAX_UNDO_STATES for the count,
// ProjectJournal::DefaultMaxUndoBytes for the bytes); what belongs here is the
// rule the CONTROL SURFACE adds on top, because coalescing is a command-surface
// grouping decision and not an engine one.
// ---------------------------------------------------------------------------

/*! The window, in milliseconds, inside which a run of the SAME command on the
 *  SAME target is ONE undo step. A drag is one gesture, not 200 edits: an agent
 *  that streams `clip.move` at 50 Hz must not leave 200 Ctrl+Z presses behind
 *  it, while two deliberate drags of the same clip seconds apart must stay two
 *  steps. 400 ms is longer than a drag's frame interval and shorter than a
 *  human's "I have stopped and started again" pause; it is DECLARED rather than
 *  tuned per client so undo granularity is reproducible between agents.
 *  control.set_undo_coalescing changes the live value, and 0 disables
 *  coalescing entirely (which reproduces the pre-0.3.0 behaviour exactly).
 *  See docs/UNDO-BOUNDS.md. */
constexpr int UndoCoalesceWindowMs = 400;
//! The largest window control.set_undo_coalescing accepts. Beyond a minute the
//! grouping stops describing a gesture, and the cap keeps a client from making
//! the undo stack's granularity unreproducible for everyone else.
constexpr int MaxUndoCoalesceWindowMs = 60000;

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
