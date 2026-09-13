/*
 * ProjectJournal.cpp - implementation of ProjectJournal
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <cstdlib>
#include <utility>

#include <QDomElement>
#include <QSet>

#include "ProjectJournal.h"
#include "Engine.h"
#include "JournallingObject.h"
#include "lmms_math.h"
#include "Song.h"
#include "AutomationClip.h"

namespace lmms
{

//! Avoid clashes between loaded IDs (have the bit cleared)
//! and newly created IDs (have the bit set)
static const int EO_ID_MSB = 1 << 23;

//! The DEFAULT count cap. It is no longer a compile-time constant of the
//! mechanism: it is the value maxUndoStates() starts at, and a client changes
//! the live cap with control.set_undo_depth (SPEC A16's "undo depth" obligation,
//! task #623). Kept as MAX_UNDO_STATES because the transaction record's count cap
//! is defined as "the same depth as the undo stack", and one name for one
//! concept is worth more than a rename here.
const int ProjectJournal::MAX_UNDO_STATES = 100;

ProjectJournal::ProjectJournal() :
	m_joIDs(),
	m_undoCheckPoints(),
	m_redoCheckPoints(),
	m_journalling( false )
{
}




//! Captures \a jo's current state, which becomes the redo half of a step.
void ProjectJournal::captureState( JournallingObject * jo, SavedObject * into ) const
{
	DataFile currentState( DataFile::Type::JournalData );
	jo->saveState( currentState, currentState.content() );
	*into = SavedObject( jo->id(), currentState, serialisedBytes( currentState ) );
}

//! Restores one object from its saved XML, with journalling off so the restore
//! itself does not push a new checkpoint. The AutomationClip id fix-up is the
//! historical step that follows a restore that carried automation clips.
void ProjectJournal::restoreState( SavedObject & saved )
{
	JournallingObject * jo = m_joIDs.value( saved.joID, nullptr );
	if( jo == nullptr ) { return; }

	const bool previous = isJournalling();
	setJournalling( false );
	jo->restoreState( saved.data.content().firstChildElement() );
	setJournalling( previous );
	Engine::getSong()->setModified();

	// loading AutomationClip connections correctly
	if( !saved.data.content().elementsByTagName( "automationclip" ).isEmpty() )
	{
		AutomationClip::resolveAllIDs();
	}
}

//! Restores every live object of \a step in one go (SPEC A16: one agent command
//! is one undoable step, however many objects it wrote).
bool ProjectJournal::restoreStep( CheckPoint & step, CheckPoint * redo )
{
	bool restoredAny = false;
	for( SavedObject & saved : step.objects )
	{
		JournallingObject * jo = m_joIDs.value( saved.joID, nullptr );
		if( jo == nullptr ) { continue; }
		SavedObject current;
		captureState( jo, &current );
		redo->bytes += current.bytes;
		redo->objects.append( current );
		restoreState( saved );
		restoredAny = true;
	}
	return restoredAny;
}

void ProjectJournal::undo()
{
	while( !m_undoCheckPoints.isEmpty() )
	{
		CheckPoint c = m_undoCheckPoints.pop();
		// The step has left the bounded undo stack; the redo side is not
		// budgeted (it can only hold what the undo stack held) so the bytes are
		// released here and taken back on redo().
		m_retainedBytes -= c.bytes;
		CheckPoint redo;

		// Objects first: an action may FREE one of them (deleting a MixerChannel
		// frees its models, deleting a Track frees its clips), and a restore
		// after the free would be a restore of a dead journal id.
		bool did = restoreStep( c, &redo );
		bool redoable = true;
		for( int i = c.actions.size() - 1; i >= 0; --i )
		{
			// Undo is LIFO, so the recorded operations run in reverse push order
			// and the redo record keeps them in push order.
			const std::function<void()> undoAction = c.actions.at( i ).first;
			const std::function<void()> redoAction = c.actions.at( i ).second;
			if( !undoAction ) { continue; }
			if( redoAction ) { redo.actions.prepend( qMakePair( undoAction, redoAction ) ); }
			else { redoable = false; }
			undoAction();
			did = true;
		}
		if( !did ) { continue; }
		if( redoable ) { m_redoCheckPoints.push( redo ); }
		else { m_redoCheckPoints.clear(); }
		Engine::getSong()->setModified();
		break;
	}
}



void ProjectJournal::redo()
{
	while( !m_redoCheckPoints.isEmpty() )
	{
		CheckPoint c = m_redoCheckPoints.pop();
		CheckPoint undoState;

		bool did = restoreStep( c, &undoState );
		for( int i = c.actions.size() - 1; i >= 0; --i )
		{
			const std::function<void()> redoAction = c.actions.at( i ).second;
			if( !redoAction ) { continue; }
			undoState.actions.prepend( c.actions.at( i ) );
			redoAction();
			did = true;
		}
		if( !did ) { continue; }
		// A fresh serial: the step is a new entry on the stack, and a record
		// that still names the old one must not match it. The caps are re-applied
		// because a client may have LOWERED them while these steps sat on the
		// redo stack.
		undoState.serial = ++m_stepSerial;
		m_undoCheckPoints.push( undoState );
		m_retainedBytes += undoState.bytes;
		trimUndoStack();
		Engine::getSong()->setModified();
		break;
	}
}

bool ProjectJournal::canUndo() const
{
	return !m_undoCheckPoints.isEmpty();
}

bool ProjectJournal::canRedo() const
{
	return !m_redoCheckPoints.isEmpty();
}



void ProjectJournal::addJournalCheckPoint( JournallingObject *jo )
{
	if( !isJournalling() ) { return; }

	DataFile dataFile( DataFile::Type::JournalData );
	jo->saveState( dataFile, dataFile.content() );

	const int bytes = serialisedBytes( dataFile );
	m_redoCheckPoints.clear();
	CheckPoint step( jo->id(), dataFile, bytes );
	step.serial = ++m_stepSerial;
	m_undoCheckPoints.push( step );
	m_retainedBytes += bytes;
	trimUndoStack();
}



void ProjectJournal::addJournalCheckPoint( const QVector<JournallingObject *> &objects )
{
	if( !isJournalling() ) { return; }

	// One checkpoint, N objects: undo() pops it once and restores all of them,
	// which is what makes one agent command one Ctrl+Z (SPEC A16).
	CheckPoint step;
	int bytes = 0;
	for( JournallingObject * jo : objects )
	{
		if( jo == nullptr ) { continue; }
		SavedObject saved;
		captureState( jo, &saved );
		bytes += saved.bytes;
		step.objects.append( saved );
	}
	if( step.objects.isEmpty() ) { return; }
	step.bytes = bytes;

	m_redoCheckPoints.clear();
	step.serial = ++m_stepSerial;
	m_undoCheckPoints.push( step );
	m_retainedBytes += bytes;
	trimUndoStack();
}



void ProjectJournal::addJournalAction( std::function<void()> undoAction, std::function<void()> redoAction )
{
	if( !isJournalling() || !undoAction ) { return; }

	m_redoCheckPoints.clear();
	// An action-only step serialises nothing of its own (its bytes are 0): what
	// it can cost is bounded by the closure it carries, not by a serialised
	// state, and the closure is one recorded operation.
	CheckPoint step = actionCheckPoint( std::move( undoAction ), std::move( redoAction ) );
	step.serial = ++m_stepSerial;
	m_undoCheckPoints.push( step );
	trimUndoStack();
}



jo_id_t ProjectJournal::allocID(JournallingObject* obj)
{
	jo_id_t id;
	for (jo_id_t tid = fastRand(); m_joIDs.contains(id = tid % EO_ID_MSB | EO_ID_MSB); tid++) {}
	m_joIDs[id] = obj;
	return id;
}


void ProjectJournal::reallocID( const jo_id_t _id, JournallingObject * _obj )
{
	//printf("realloc %d %d\n", _id, _obj );
//	if( m_joIDs.contains( _id ) )
	{
		m_joIDs[_id] = _obj;
	}
}




jo_id_t ProjectJournal::idToSave( jo_id_t id )
{
	return id & ~EO_ID_MSB;
}

jo_id_t ProjectJournal::idFromSave( jo_id_t id )
{
	return id | EO_ID_MSB;
}




void ProjectJournal::mergeCheckpointsFrom( int depth )
{
	if( depth < 0 || depth > m_undoCheckPoints.size() ) { return; }
	const int excess = m_undoCheckPoints.size() - depth;
	if( excess <= 1 ) { return; }

	// For each OBJECT, the earliest capture wins: that is the state before the
	// command, and restoring a later capture would undo only part of it.
	CheckPoint merged;
	QSet<jo_id_t> seen;
	for( int i = depth; i < m_undoCheckPoints.size(); ++i )
	{
		const CheckPoint & step = m_undoCheckPoints.at( i );
		for( const SavedObject & saved : step.objects )
		{
			if( seen.contains( saved.joID ) ) { continue; }
			seen.insert( saved.joID );
			merged.objects.append( saved );
		}
		// Every recorded operation is kept, in push order.
		merged.actions += step.actions;
	}
	if( merged.empty() ) { m_undoCheckPoints.remove( depth, excess ); return; }

	// The FIRST step of the window keeps its identity: a record that already
	// names it stays valid through the merge. The bytes are re-summed from the
	// surviving captures (a later capture of the same object is dropped, so the
	// step is smaller than the sum of what it replaces).
	merged.serial = m_undoCheckPoints.at( depth ).serial;
	merged.bytes = 0;
	for( const SavedObject & saved : merged.objects ) { merged.bytes += saved.bytes; }
	for( int i = depth; i < m_undoCheckPoints.size(); ++i )
	{
		m_retainedBytes -= m_undoCheckPoints.at( i ).bytes;
	}

	m_undoCheckPoints.remove( depth, excess );
	m_undoCheckPoints.push( merged );
	m_retainedBytes += merged.bytes;
}



void ProjectJournal::clearJournal()
{
	m_undoCheckPoints.clear();
	m_redoCheckPoints.clear();
	// A fresh document starts with a fresh accounting. m_stepSerial is NOT
	// rewound: serials must stay unique for the life of the journal, so a record
	// left over from the previous document can never match a new step.
	m_retainedBytes = 0;
	m_evicted = 0;
	m_coalesced = 0;

	for( JoIdMap::Iterator it = m_joIDs.begin(); it != m_joIDs.end(); )
	{
		if( it.value() == nullptr )
		{
			it = m_joIDs.erase( it );
		}
		else
		{
			++it;
		}
	}
}

void ProjectJournal::stopAllJournalling()
{
	for( JoIdMap::Iterator it = m_joIDs.begin(); it != m_joIDs.end(); ++it)
	{
		if( it.value() != nullptr )
		{
			it.value()->setJournalling(false);
		}
	}
	setJournalling(false);
}



} // namespace lmms
