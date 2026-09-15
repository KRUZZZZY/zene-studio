/*
 * ProjectJournal.h - declaration of class ProjectJournal
 *
 * Copyright (c) 2006-2010 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_PROJECT_JOURNAL_H
#define LMMS_PROJECT_JOURNAL_H

#include <functional>

#include <QHash>
#include <QStack>
#include <QVector>

#include "LmmsTypes.h"
#include "DataFile.h"


namespace lmms
{


class JournallingObject;


/*! The engine's own undo stack - the one the GUI's Edit->Undo (Ctrl+Z) and the
 *  control surface's control.undo both unwind.
 *
 *  BOUNDED (Zene Studio; SPEC A16's "undo depth and drag coalescing" obligation,
 *  task #623). The stack is bounded TWO ways, and both bounds are declared,
 *  enforced and REPORTED rather than implicit:
 *
 *    - a COUNT cap: maxUndoStates(), `MAX_UNDO_STATES` unless a client changes
 *      it. Past it the OLDEST step is evicted (FIFO), so the newest step - the
 *      one an undo needs - is never the one dropped;
 *    - a BYTE budget: maxUndoBytes(), the total serialised size of the steps
 *      retained. Every step measures itself when it is captured
 *      (DataFile::toByteArray().size()), so the accounting is exact rather than
 *      estimated, and the same FIFO rule applies. One step is always retained
 *      even if it alone exceeds the budget: an undo stack that has silently
 *      dropped the edit you just made is worse than one that is over budget.
 *
 *  Every eviction is COUNTED (evictedSteps()) and reported by the
 *  control.undo_depth command, so "this is the whole history" can be told from
 *  "this is what the bound retains" - the same honesty rule the transaction
 *  record follows.
 *
 *  COALESCING (the drag rule). A run of the same command on the same target -
 *  a dragged clip, a dragged fader - is ONE step, not one per call, via
 *  coalesceTopStepIntoPrevious(). The rule itself (which commands, which
 *  target, the time window) is the control surface's and is declared in
 *  src/core/ControlReversibilityTable.cpp (the RC() rows) and
 *  src/core/ControlUndoCoalescing.cpp; this class provides the primitive and
 *  does not know what a command is. Undo semantics are unchanged by a merge:
 *  the EARLIEST capture of each object survives, which is the pre-gesture
 *  state, so one undo still returns the object to where the gesture started.
 *
 *  Neither the bounds nor the merge touch the audio thread: this class is
 *  GUI/agent-side only (Engine::projectJournal()).
 */
class ProjectJournal
{
public:
	static const int MAX_UNDO_STATES;

	//! The hard ceiling a client may raise the count cap to. A "bound" that can
	//! be set to a billion is not a bound; measured against the byte budget,
	//! 10000 steps of full-Song XML is already far past what an undo stack may
	//! hold, so the ceiling is a refusal boundary rather than a suggestion.
	static constexpr int MaxUndoStateLimit = 10000;
	//! Default byte budget: 16 MiB of serialised undo state. Large enough that a
	//! normal session never reaches it (a Song checkpoint of a working project
	//! is tens of KB), small enough that a 100-step stack of full-container
	//! checkpoints cannot grow without limit.
	static constexpr qint64 DefaultMaxUndoBytes = 16ll * 1024 * 1024;
	//! The hard ceiling on the byte budget (512 MiB).
	static constexpr qint64 MaxUndoByteLimit = 512ll * 1024 * 1024;

	ProjectJournal();
	virtual ~ProjectJournal() = default;

	void undo();
	void redo();

	bool canUndo() const;
	bool canRedo() const;

	void addJournalCheckPoint( JournallingObject *jo );

	/*! One undo step covering SEVERAL objects (Zene Studio, SPEC A16).
	 *
	 * undo() pops ONE checkpoint, so N checkpoints cost N Ctrl+Z presses. An
	 * agent command that writes more than one object - track.set_solo writes
	 * every track's mute through TrackView - must nevertheless be ONE undoable
	 * step for the human, so its before-states are recorded as one checkpoint
	 * and restored together. Additive: the single-object call above is the
	 * one-entry case of this and its behaviour is unchanged.
	 */
	void addJournalCheckPoint( const QVector<JournallingObject *> &objects );

	/*! One undo step that RUNS AN ACTION instead of restoring an object
	 * (Zene Studio, SPEC A16).
	 *
	 * A created or deleted Track/MixerChannel has no live object a checkpoint
	 * could restore - a deleted JournallingObject's id resolves to nullptr and
	 * a created one has nothing to put back - so the inverse is the operation,
	 * not the state. Pushing it here (rather than into a second undo stack kept
	 * by the control surface) is what makes Ctrl+Z and control.undo one
	 * history. `redo` is the reverse action, so a redo is faithful rather than
	 * a dropped step. Both actions run on the UI thread; nothing here is
	 * reached from the audio thread.
	 */
	void addJournalAction( std::function<void()> undo, std::function<void()> redo );

	/*! One undo step for a STRUCTURAL operation, carrying a measured document
	 * (Zene Studio, task #664, feature row 75).
	 *
	 * It is the action checkpoint above plus the one thing an action checkpoint
	 * gets wrong for a structural payload: the BYTES. A captured track (its
	 * clips and their notes) or a device's state document is a bounded but real
	 * 64 KiB, and an action step records `bytes = 0` - so a stack of a hundred
	 * structural deletes would report a retained size of zero and evict nothing,
	 * making the declared byte budget a suggestion. Here \a payloadBytes is the
	 * MEASURED size of the document the closure holds and is counted exactly as a
	 * captured object state is, so the same FIFO bound applies to both kinds of
	 * step and `control.undo_depth`'s retained_bytes means one thing.
	 *
	 * \a payloadBytes may be 0 (a reorder has no document). Counted, never
	 * stored: the document itself lives in the closure, and measuring it twice
	 * would let the two disagree.
	 */
	void addJournalStructure( std::function<void()> undo, std::function<void()> redo,
		qint64 payloadBytes );

	bool isJournalling() const
	{
		return m_journalling;
	}

	/*! Marks the current undo depth, and merges everything pushed since into
	 * ONE step (Zene Studio, SPEC A16 deliverable 3).
	 *
	 * This is the mechanism that makes "one agent command = one Ctrl+Z" true in
	 * general rather than per command. AutomatableModel::setValue() pushes a
	 * checkpoint of its own on every non-automated write
	 * (src/core/AutomatableModel.cpp:303), so a single command that writes N
	 * models - track.set_solo writes the solo flag and every other track's mute -
	 * leaves N checkpoints on the stack and costs N undos. The control registry
	 * therefore marks the depth before a mutating handler runs and merges
	 * afterwards: for each object, its EARLIEST capture wins (that is the
	 * pre-command state), and every recorded action is kept in push order.
	 */
	int undoDepth() const { return m_undoCheckPoints.size(); }
	void mergeCheckpointsFrom( int depth );

	// ---- the bound (Zene Studio, task #623: "undo depth and drag coalescing") ----

	//! The count cap in force (MAX_UNDO_STATES unless a client changed it).
	int maxUndoStates() const { return m_maxUndoStates; }
	/*! Sets the count cap. False - and NOTHING changes - when \a states is
	 *  outside [1, MaxUndoStateLimit]; a client that asks for an unbounded stack
	 *  is refused rather than obeyed. Trims immediately: lowering the cap evicts
	 *  the oldest steps there and then, which is why control.set_undo_depth is a
	 *  mutating command and reports what it dropped. */
	bool setMaxUndoStates( int states );

	//! The byte budget in force (DefaultMaxUndoBytes unless a client changed it).
	qint64 maxUndoBytes() const { return m_maxUndoBytes; }
	//! Sets the byte budget; false and unchanged outside [1, MaxUndoByteLimit].
	bool setMaxUndoBytes( qint64 bytes );

	//! Serialised bytes the retained undo steps occupy (exact, not estimated).
	qint64 retainedBytes() const { return m_retainedBytes; }
	//! Steps a bound has evicted since the stack was cleared. Counted, never
	//! hidden: it is how a client tells "the whole history" from "what the
	//! bound retains".
	int evictedSteps() const { return m_evicted; }
	//! The redo side's depth. The redo stack needs no separate budget: it can
	//! only ever hold steps that were on the bounded undo stack, so it is
	//! bounded by the same two caps transitively.
	int redoDepth() const { return m_redoCheckPoints.size(); }
	//! Steps merged away by coalescing since the stack was cleared.
	int coalescedSteps() const { return m_coalesced; }

	/*! Merges the NEWEST step into the step below it - the primitive behind
	 *  "a 200-step drag is one Ctrl+Z".
	 *
	 *  It merges unconditionally: the CALLER owns the policy (same command, same
	 *  target, inside the window), because only the control surface knows what a
	 *  command is. For each object the EARLIEST capture wins - that is the state
	 *  before the gesture - and recorded actions are kept in push order, so the
	 *  merged step undoes to exactly where the gesture started. The surviving
	 *  step keeps its own serial, so a transaction record that already points at
	 *  it stays valid.
	 *
	 *  False when there is nothing to merge into (fewer than two steps).
	 */
	bool coalesceTopStepIntoPrevious();

	/*! The serial of the newest step, 0 when the stack is empty. A transaction
	 *  record holds it so control.undo can tell "this step is still on the
	 *  stack" from "a bound evicted it". */
	quint64 topStepSerial() const;
	/*! The serial of the OLDEST retained step; the next serial to be issued when
	 *  the stack is empty, so `record.step < oldestStepSerial()` means evicted. */
	quint64 oldestStepSerial() const;
	/*! The serial of the step DIRECTLY BELOW the newest one, 0 when there is
	 *  none. The coalescing rule uses it to recognise "the step below the one
	 *  this call just pushed is the run I am still in" without holding an index:
	 *  a bound can evict steps under an index, but a serial is stable. */
	quint64 previousStepSerial() const;

	void setJournalling( const bool _on )
	{
		m_journalling = _on;
	}

	// alloc new ID and register object _obj to it
	jo_id_t allocID( JournallingObject * _obj );

	// if there's already something known about ID _id, but it is currently
	// unused (e.g. after jouralling object was deleted), register object
	// _obj to this id
	void reallocID( const jo_id_t _id, JournallingObject * _obj );

	// make ID _id unused, but keep all global journalling information
	// (order of journalling entries etc.) referring to _id - needed for
	// restoring a journalling object later
	void freeID( const jo_id_t _id )
	{
		reallocID( _id, nullptr );
	}

	//! hack, not used when saving a file
	static jo_id_t idToSave( jo_id_t id );
	//! hack, not used when loading a savefile
	static jo_id_t idFromSave( jo_id_t id );

	void clearJournal();
	void stopAllJournalling();
	JournallingObject * journallingObject( const jo_id_t _id )
	{
		if( m_joIDs.contains( _id ) )
		{
			return m_joIDs[_id];
		}
		return nullptr;
	}


private:
	using JoIdMap = QHash<jo_id_t, JournallingObject*>;

	//! One (object, saved state) pair of a checkpoint.
	struct SavedObject
	{
		SavedObject( jo_id_t initID = 0,
					const DataFile& initData = DataFile( DataFile::Type::JournalData ),
					int initBytes = 0 ) :
			joID( initID ),
			data( initData ),
			bytes( initBytes )
		{
		}
		jo_id_t joID;
		DataFile data;
		//! The serialised size of \c data, measured once when it was captured
		//! (serialisedBytes()). The byte budget is enforced from these, so they
		//! must be measured at capture time rather than estimated later.
		int bytes = 0;
	};

	struct CheckPoint
	{
		//! An empty step: an action-only step, or a redo record being filled.
		CheckPoint() = default;
		//! A step restoring one object (the historical checkpoint shape).
		CheckPoint( jo_id_t initID, const DataFile& initData, int initBytes = 0 ) :
			objects(),
			actions(),
			bytes( initBytes )
		{
			objects.append( SavedObject( initID, initData, initBytes ) );
		}
		//! The objects this step restores, together, in one pop.
		QVector<SavedObject> objects;
		//! Operations this step runs, in push order, each paired with its
		//! reverse. Undo runs the firsts in reverse push order, redo the
		//! seconds; a step whose second is empty cannot be redone, and the
		//! redo stack is emptied rather than replaying an older entry.
		QVector<QPair<std::function<void()>, std::function<void()>>> actions;
		//! The step's total serialised size: what the byte budget counts.
		int bytes = 0;
		//! A monotone identity, unique among the steps this journal ever held.
		//! A transaction record holds it, which is how an undo can tell "my step
		//! is still on the stack" from "a bound evicted it" without holding a
		//! pointer into the stack.
		quint64 serial = 0;

		bool hasActions() const { return !actions.isEmpty(); }
		bool empty() const { return objects.isEmpty() && actions.isEmpty(); }
	};
	using CheckPointStack = QStack<CheckPoint>;

	//! An action-only step: runs \a undo, and on redo runs \a redo.
	static CheckPoint actionCheckPoint( std::function<void()> undo,
		std::function<void()> redo = std::function<void()>() );
	//! The serialised size of \a jo's captured state, in bytes - the quantity
	//! the byte budget is enforced from. Measured with the same call for every
	//! step, so the budget means one thing.
	static int serialisedBytes( const DataFile & data );
	/*! Drops the OLDEST steps until BOTH caps hold (count and bytes), counting
	 *  each eviction in m_evicted. The newest step is never dropped: one step is
	 *  always retained even when it alone exceeds the byte budget. Called after
	 *  every push, from undo()'s and redo()'s hand-backs too - a cap lowered
	 *  while steps sit on the redo stack must still hold when they come back. */
	void trimUndoStack();
	//! Restores every live object of \a step, capturing the redo halves into
	//! \a redo. False when no object of the step is still alive (the caller then
	//! pops the next checkpoint, the historical behaviour). Non-const because
	//! DataFile::content() is: the step is the caller's own copy, popped off the
	//! stack, so modifying it in place is safe.
	bool restoreStep( CheckPoint & step, CheckPoint * redo );
	//! Captures \a jo's current state into \a into (the redo half of a step).
	void captureState( JournallingObject * jo, SavedObject * into ) const;
	//! Restores \a saved into its object, journalling off.
	void restoreState( SavedObject & saved );

	JoIdMap m_joIDs;

	CheckPointStack m_undoCheckPoints;
	CheckPointStack m_redoCheckPoints;

	bool m_journalling;

	//! The two caps in force (see the class comment). Defaulted to the declared
	//! values and changeable through setMaxUndoStates()/setMaxUndoBytes().
	int m_maxUndoStates = MAX_UNDO_STATES;
	qint64 m_maxUndoBytes = ProjectJournal::DefaultMaxUndoBytes;
	//! Serialised bytes the retained undo steps occupy; maintained on every
	//! push, pop, merge and eviction rather than recomputed on demand.
	qint64 m_retainedBytes = 0;
	//! Steps a bound has evicted, and steps coalescing has merged away.
	int m_evicted = 0;
	int m_coalesced = 0;
	//! The last serial handed to a step; 0 means "no step has been pushed yet".
	quint64 m_stepSerial = 0;

} ;


} // namespace lmms

#endif // LMMS_PROJECT_JOURNAL_H
