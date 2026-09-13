/*
 * ControlUndoCoalescing.h - the COALESCING rule of the control surface's undo
 *                           steps (Zene Studio, SPEC A16's "undo depth and drag
 *                           coalescing", task #623).
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

#ifndef LMMS_CONTROL_UNDO_COALESCING_H
#define LMMS_CONTROL_UNDO_COALESCING_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlReversibility.h"
#include "lmms_export.h"

namespace lmms
{

class ProjectJournal;

namespace control
{

/*! THE DRAG RULE, as one sentence, for a client that wants to read it rather
 *  than infer it from `control.commands_list`:
 *
 *  "Two consecutive calls of the same command on the SAME target, with no other
 *   undo step pushed in between and less than the coalescing window apart, are
 *   ONE undo step; anything else opens a new one."
 *
 *  The rule exists because a drag is one gesture and not 200 edits: without it,
 *  moving a clip with 200 `clip.move` calls costs 200 Ctrl+Z presses, and the
 *  human's and the agent's undo histories diverge in granularity even though
 *  they share one stack.
 *
 *  WHICH commands coalesce is not decided here - it is DECLARED in the contract
 *  table (the RC() rows of src/core/ControlReversibilityTable.cpp), one row per
 *  drag-shaped command, naming the argument(s) whose values identify the thing
 *  the gesture is being made on. This header holds the mechanism that reads
 *  that declaration.
 */
class LMMS_EXPORT UndoCoalescer
{
public:
	/*! True when a call of the command whose coalescing key is \a key continues
	 *  the previous run: same key, inside the window, and the step DIRECTLY BELOW
	 *  the one this call pushed is the step the run started with (\a
	 *  previousSerial). The comparison is by SERIAL rather than by stack index
	 *  because a bound can evict steps while a command runs - an index moves
	 *  under eviction, a serial does not. */
	bool continues(const QString& key, quint64 previousSerial) const;

	/*! Records the step a command produced - the run's step, for the next call -
	 *  by its journal SERIAL, and restarts the window. Called for every
	 *  coalescing-capable command, whether or not it merged. */
	void stepOpened(const QString& key, quint64 serial);

	//! Forgets the run: the next command opens a new step, whatever it is.
	void breakRun();

	//! The window in force, milliseconds; 0 means "coalesce nothing".
	int windowMs() const { return m_windowMs; }
	/*! Sets the window. False - and unchanged - outside
	 *  [0, MaxUndoCoalesceWindowMs]. 0 is legal and is the honest way to ask for
	 *  the pre-0.3.0 behaviour: one step per command. */
	bool setWindowMs(int ms);

	//! Milliseconds since the last step this coalescer recorded, -1 when none.
	qint64 sinceLastMs() const;

private:
	QString m_key;
	//! The journal serial of the step the last run produced, 0 when none.
	quint64 m_topSerial = 0;
	//! Milliseconds since the process started at the last step this coalescer
	//! saw, -1 when it has seen none.
	qint64 m_atMs = -1;
	//! The window in force. Initialised from the DECLARED default - a coalescing
	//! window that is indeterminate until someone reads it is not a rule (the
	//! socket flow caught exactly that, reporting window_ms: -1).
	int m_windowMs = UndoCoalesceWindowMs;
	QElapsedTimer m_clock;
};

//! Every command the contract table declares as coalescing, sorted.
LMMS_EXPORT QStringList coalescingCommands();

/*! The coalescing key of one call: "<command>:<value>" per declared target
 *  argument, or empty when the command does not coalesce (or the table has no
 *  row for it - which the anti-drift test fails on anyway). */
LMMS_EXPORT QString coalescingKey(const QString& commandId, const QJsonObject& args);

//! The rule above, as text, for control.undo_depth's `rule` field.
LMMS_EXPORT QString coalescingRuleText();

/*! The whole decision for one mutating command, and the bookkeeping on both
 *  sides: merges \a journal's newest step into the previous one when the call
 *  continues the run, and records the result on \a coalescer either way.
 *
 *  The caller says whether the command PRODUCED a step (the registry compares the
 *  top serial before and after, which is the reliable test under a bound that
 *  evicts - see docs/UNDO-BOUNDS.md). Returns true when the step was merged.
 */
LMMS_EXPORT bool coalesceOrOpenStep(UndoCoalescer& coalescer, ProjectJournal& journal,
	const QString& commandId, const QJsonObject& args, bool producedStep);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_UNDO_COALESCING_H
