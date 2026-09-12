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


//! @warning many parts of this class may be rewritten soon
class ProjectJournal
{
public:
	static const int MAX_UNDO_STATES;

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
					const DataFile& initData = DataFile( DataFile::Type::JournalData ) ) :
			joID( initID ),
			data( initData )
		{
		}
		jo_id_t joID;
		DataFile data;
	};

	struct CheckPoint
	{
		//! An empty step: an action-only step, or a redo record being filled.
		CheckPoint() = default;
		//! A step restoring one object (the historical checkpoint shape).
		CheckPoint( jo_id_t initID, const DataFile& initData ) :
			objects(),
			actions()
		{
			objects.append( SavedObject( initID, initData ) );
		}
		//! The objects this step restores, together, in one pop.
		QVector<SavedObject> objects;
		//! Operations this step runs, in push order, each paired with its
		//! reverse. Undo runs the firsts in reverse push order, redo the
		//! seconds; a step whose second is empty cannot be redone, and the
		//! redo stack is emptied rather than replaying an older entry.
		QVector<QPair<std::function<void()>, std::function<void()>>> actions;

		bool hasActions() const { return !actions.isEmpty(); }
		bool empty() const { return objects.isEmpty() && actions.isEmpty(); }
	};
	using CheckPointStack = QStack<CheckPoint>;

	//! An action-only step: runs \a undo, and on redo runs \a redo.
	static CheckPoint actionCheckPoint( std::function<void()> undo,
		std::function<void()> redo = std::function<void()>() );
	//! Drops the oldest steps once the stack exceeds MAX_UNDO_STATES.
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

} ;


} // namespace lmms

#endif // LMMS_PROJECT_JOURNAL_H
