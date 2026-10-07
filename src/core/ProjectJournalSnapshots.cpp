/*
 * ProjectJournalSnapshots.cpp - WF-033: the whole-project snapshot each undo
 *                               step can carry, so the history can be saved
 *                               with the project
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

/*
 * WHY THIS IS ITS OWN TRANSLATION UNIT
 *
 * The reason ProjectJournalBounds.cpp and ProjectJournalStructural.cpp are:
 * ProjectJournal.cpp is upstream-inherited, and this is a fork addition on top
 * of it. The design is docs/UNDO-HISTORY-PERSISTENCE.md (WF-033, option C of
 * plans/DESIGN-WF-033-UNDO-PERSISTENCE.md: the newest N steps as compressed
 * whole-project snapshots under a byte cap).
 *
 * WHAT THE JOURNAL DOES AND DOES NOT DO HERE
 *
 * A step's undo is still what it always was - object restores and recorded
 * closures - so nothing about in-session undo changes. A step merely CARRIES
 * the whole project as it was before it (its "pre-state"), captured by the
 * policy (UndoHistory) and handed to a save. The journal attaches it, keeps it
 * through undo/redo/merge/coalescing (earliest wins, as for object captures),
 * and drops it from steps the policy's bounds no longer keep.
 */

#include "ProjectJournal.h"

#include <utility>

namespace lmms
{

bool ProjectJournal::s_snapshotIds = false;

void ProjectJournal::beginEdit()
{
	if( m_editDepth++ > 0 ) { return; }
	m_pendingSnapshot.reset();
	// Taken BEFORE the handler runs: an action step is pushed after its change,
	// and this is the only point where the state before the edit still exists.
	if( m_snapshotPolicy != nullptr && isJournalling() && !m_capturing
		&& m_snapshotPolicy->capturing() )
	{
		m_capturing = true;
		m_pendingSnapshot = m_snapshotPolicy->capture();
		m_capturing = false;
	}
}




void ProjectJournal::endEdit()
{
	if( m_editDepth == 0 ) { return; }
	if( --m_editDepth == 0 ) { m_pendingSnapshot.reset(); }
}




ProjectJournal::Snapshot ProjectJournal::snapshotForObjectStep()
{
	// Inside an edit the pre-COMMAND state is the one the step undoes to: the
	// handler may already have changed something before this object.
	if( m_editDepth > 0 ) { return m_pendingSnapshot; }
	if( m_snapshotPolicy == nullptr || m_capturing || !m_snapshotPolicy->capturing() )
	{
		return Snapshot();
	}
	m_capturing = true;
	Snapshot taken = m_snapshotPolicy->capture();
	m_capturing = false;
	return taken;
}




void ProjectJournal::attachSnapshot( CheckPoint & step, const Snapshot & snapshot )
{
	step.snapshot = snapshot;
	step.snapshotBytes = snapshot ? snapshot->size() : -1;
}




QVector<ProjectJournal::StepSnapshot> ProjectJournal::stepSnapshots() const
{
	QVector<StepSnapshot> out;
	out.reserve( m_undoCheckPoints.size() );
	for( int i = m_undoCheckPoints.size() - 1; i >= 0; --i )
	{
		const CheckPoint & step = m_undoCheckPoints.at( i );
		out.append( StepSnapshot{ step.snapshot, step.snapshotBytes } );
	}
	return out;
}




ProjectJournal::PersistPlan ProjectJournal::planPersist( const QVector<StepSnapshot> & newestFirst,
	int keepSteps, qint64 keepBytes )
{
	PersistPlan plan;
	const int count = newestFirst.size();
	for( int i = 0; i < count; ++i )
	{
		const StepSnapshot & step = newestFirst.at( i );
		const int rest = count - i;
		if( step.bytes < 0 ) { plan.unavailable = rest; break; }
		if( step.bytes > keepBytes )
		{
			// The owner's rule: one snapshot over the cap on its own means the
			// history is not kept from this step onward - and is SAID so.
			plan.oversize = true;
			plan.oversizeBytes = step.bytes;
			plan.droppedOldest = rest;
			break;
		}
		// Oldest first out: the step limit, the cap, or a snapshot a bound
		// already dropped all end the run here.
		if( plan.saved >= keepSteps || !step.snapshot || plan.bytes + step.bytes > keepBytes )
		{
			plan.droppedOldest = rest;
			break;
		}
		++plan.saved;
		plan.bytes += step.bytes;
	}
	return plan;
}




void ProjectJournal::pruneSnapshots()
{
	if( m_snapshotPolicy == nullptr ) { return; }
	const bool capturing = m_snapshotPolicy->capturing();
	const int keep = capturing
		? planPersist( stepSnapshots(), m_snapshotPolicy->keepSteps(), m_snapshotPolicy->keepBytes() ).saved
		: 0;
	// Everything below the newest `keep` steps loses its snapshot (the size is
	// remembered so a save can say why); with persistence off, all of them -
	// and then they were never available to a save at all.
	for( int i = m_undoCheckPoints.size() - 1 - keep; i >= 0; --i )
	{
		CheckPoint & step = m_undoCheckPoints[i];
		step.snapshot.reset();
		if( !capturing ) { step.snapshotBytes = -1; }
	}
}




void ProjectJournal::pushLoadedStep( std::function<void()> undo, std::function<void()> redo,
	Snapshot before )
{
	if( !undo ) { return; }
	m_redoCheckPoints.clear();
	CheckPoint step = actionCheckPoint( std::move( undo ), std::move( redo ) );
	attachSnapshot( step, before );
	step.loaded = true;
	step.serial = ++m_stepSerial;
	m_undoCheckPoints.push( step );
	trimUndoStack();
}




ProjectJournal::DetachedHistory ProjectJournal::detachHistory()
{
	DetachedHistory history;
	history.undo = std::move( m_undoCheckPoints );
	history.redo = std::move( m_redoCheckPoints );
	history.retainedBytes = m_retainedBytes;
	history.evicted = m_evicted;
	history.coalesced = m_coalesced;
	m_undoCheckPoints.clear();
	m_redoCheckPoints.clear();
	return history;
}




void ProjectJournal::reattachHistory( DetachedHistory && history )
{
	m_undoCheckPoints = std::move( history.undo );
	m_redoCheckPoints = std::move( history.redo );
	m_retainedBytes = history.retainedBytes;
	m_evicted = history.evicted;
	m_coalesced = history.coalesced;
}

} // namespace lmms
