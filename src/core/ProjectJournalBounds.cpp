/*
 * ProjectJournalBounds.cpp - the BOUND and the COALESCING primitive of the
 *                            engine's undo stack (Zene Studio, SPEC A16's
 *                            "undo depth and drag coalescing", task #623).
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
 */

/*
 * WHY THIS IS ITS OWN TRANSLATION UNIT
 *
 * ProjectJournal.cpp is the stack itself - push, pop, restore, merge - and it is
 * upstream-inherited code this fork declares in tests/upstream-modifications.txt.
 * The bound and the coalescing primitive are this fork's additions on top of it,
 * and this fork's file-length ratchet measures a file as a unit: keeping the
 * additions in one place keeps the inherited mechanism readable AND keeps the
 * two under the limit. The class, its invariants and its public API are
 * unchanged by the split - these are ProjectJournal's own member functions.
 *
 * THE TWO DECISIONS THIS FILE IMPLEMENTS are written down in full in
 * docs/UNDO-BOUNDS.md and summarised on the class in include/ProjectJournal.h:
 * the two-sided bound (a count cap and a byte budget, FIFO eviction, the newest
 * step never dropped, every eviction counted and reported) and the coalescing
 * primitive (the earliest capture of each object survives, so a merged run still
 * undoes to where the gesture started).
 */

#include <QSet>

#include "ProjectJournal.h"

namespace lmms
{

//! The serialised size of one captured state, in bytes.
//!
//! ONE call for every step (QDomDocument::toByteArray writes exactly the XML the
//! project file carries), so "the budget is 16 MiB" means the same thing for
//! every command. Measured at capture time rather than estimated later: the byte
//! budget is enforced from these numbers, and an estimate would make the bound
//! a suggestion. GUI/agent-side only - nothing here is reached from the audio
//! thread.
int ProjectJournal::serialisedBytes( const DataFile & data )
{
	return data.toByteArray().size();
}




bool ProjectJournal::setMaxUndoStates( int states )
{
	if( states < 1 || states > MaxUndoStateLimit ) { return false; }
	m_maxUndoStates = states;
	// Lowering the cap evicts the oldest steps there and then: the caller is
	// told what that cost through evictedSteps(), which is why
	// control.set_undo_depth is a mutating command.
	trimUndoStack();
	return true;
}




bool ProjectJournal::setMaxUndoBytes( qint64 bytes )
{
	if( bytes < 1 || bytes > MaxUndoByteLimit ) { return false; }
	m_maxUndoBytes = bytes;
	trimUndoStack();
	return true;
}




quint64 ProjectJournal::topStepSerial() const
{
	return m_undoCheckPoints.isEmpty() ? 0 : m_undoCheckPoints.last().serial;
}




quint64 ProjectJournal::previousStepSerial() const
{
	return m_undoCheckPoints.size() >= 2 ? m_undoCheckPoints.at( m_undoCheckPoints.size() - 2 ).serial
										: 0;
}




quint64 ProjectJournal::oldestStepSerial() const
{
	// An empty stack reports the NEXT serial to be issued: every already-issued
	// serial is then strictly older than it, which is exactly what "evicted"
	// means to the record that holds one.
	return m_undoCheckPoints.isEmpty() ? m_stepSerial + 1 : m_undoCheckPoints.first().serial;
}




bool ProjectJournal::coalesceTopStepIntoPrevious()
{
	if( m_undoCheckPoints.size() < 2 ) { return false; }

	CheckPoint top = m_undoCheckPoints.pop();
	CheckPoint & previous = m_undoCheckPoints.top();

	// For each OBJECT the EARLIEST capture wins: the step below already holds
	// the state from before the gesture, and restoring a later capture would
	// undo only part of it. The surviving step keeps its own serial, so a
	// transaction record that already names it stays valid.
	QSet<jo_id_t> already;
	for( const SavedObject & saved : previous.objects ) { already.insert( saved.joID ); }
	for( const SavedObject & saved : top.objects )
	{
		if( already.contains( saved.joID ) ) { continue; }
		previous.objects.append( saved );
		previous.bytes += saved.bytes;
	}
	// Every recorded action is kept, in push order.
	previous.actions += top.actions;

	m_retainedBytes -= top.bytes;
	++m_coalesced;
	return true;
}




ProjectJournal::CheckPoint ProjectJournal::actionCheckPoint( std::function<void()> undo,
	std::function<void()> redo )
{
	CheckPoint step;
	step.actions.append( qMakePair( std::move( undo ), std::move( redo ) ) );
	return step;
}



void ProjectJournal::trimUndoStack()
{
	// TWO caps, ONE eviction order: FIFO, so the NEWEST step - the one an undo
	// needs - is never the one dropped. The byte loop's `size() > 1` keeps the
	// newest step even when it alone exceeds the budget: an undo stack that has
	// silently dropped the edit you just made is worse than one that is over
	// budget, and the overage is reported rather than hidden.
	while( !m_undoCheckPoints.isEmpty()
		&& ( m_undoCheckPoints.size() > m_maxUndoStates
			|| ( m_retainedBytes > m_maxUndoBytes && m_undoCheckPoints.size() > 1 ) ) )
	{
		m_retainedBytes -= m_undoCheckPoints.first().bytes;
		m_undoCheckPoints.remove( 0 );
		++m_evicted;
	}
}

} // namespace lmms
