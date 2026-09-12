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

const int ProjectJournal::MAX_UNDO_STATES = 100; // TODO: make this configurable in settings

ProjectJournal::ProjectJournal() :
	m_joIDs(),
	m_undoCheckPoints(),
	m_redoCheckPoints(),
	m_journalling( false )
{
}




ProjectJournal::CheckPoint ProjectJournal::actionCheckPoint( std::function<void()> undo,
	std::function<void()> redo )
{
	CheckPoint step;
	step.actions.append( qMakePair( std::move( undo ), std::move( redo ) ) );
	return step;
}




//! Captures \a jo's current state, which becomes the redo half of a step.
void ProjectJournal::captureState( JournallingObject * jo, SavedObject * into ) const
{
	DataFile currentState( DataFile::Type::JournalData );
	jo->saveState( currentState, currentState.content() );
	*into = SavedObject( jo->id(), currentState );
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
		m_undoCheckPoints.push( undoState );
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

	m_redoCheckPoints.clear();
	m_undoCheckPoints.push( CheckPoint( jo->id(), dataFile ) );
	trimUndoStack();
}



void ProjectJournal::addJournalCheckPoint( const QVector<JournallingObject *> &objects )
{
	if( !isJournalling() ) { return; }

	// One checkpoint, N objects: undo() pops it once and restores all of them,
	// which is what makes one agent command one Ctrl+Z (SPEC A16).
	CheckPoint step;
	for( JournallingObject * jo : objects )
	{
		if( jo == nullptr ) { continue; }
		SavedObject saved;
		captureState( jo, &saved );
		step.objects.append( saved );
	}
	if( step.objects.isEmpty() ) { return; }

	m_redoCheckPoints.clear();
	m_undoCheckPoints.push( step );
	trimUndoStack();
}



void ProjectJournal::addJournalAction( std::function<void()> undoAction, std::function<void()> redoAction )
{
	if( !isJournalling() || !undoAction ) { return; }

	m_redoCheckPoints.clear();
	m_undoCheckPoints.push( actionCheckPoint( std::move( undoAction ), std::move( redoAction ) ) );
	trimUndoStack();
}



void ProjectJournal::trimUndoStack()
{
	if( m_undoCheckPoints.size() > MAX_UNDO_STATES )
	{
		m_undoCheckPoints.remove( 0, m_undoCheckPoints.size() - MAX_UNDO_STATES );
	}
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

	m_undoCheckPoints.remove( depth, excess );
	m_undoCheckPoints.push( merged );
}



void ProjectJournal::clearJournal()
{
	m_undoCheckPoints.clear();
	m_redoCheckPoints.clear();

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
