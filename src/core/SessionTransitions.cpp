/*
 * SessionTransitions.cpp - keeping the launcher and the authored session grid in step (Q050-489)
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
 *
 */
/*! Q050-489. The session grid (SessionModel, authored, saved) and the launcher (SessionScheduler, transient,
 *  audio thread) meet at two points: what a cell HOLDS is published to the engine, and what a cell DOES is a queued
 *  press/stop. Before this file only a launch published content, so an edit, an undo or a redo of a cell that was
 *  playing left the engine rendering - and the launcher reporting - what the grid no longer held
 *  (docs/050-features/Q050-489.md, the transition map). syncSessionLauncher is the one boundary every model change
 *  now passes; the pattern-reference remaps keep a cell naming its pattern when the pattern store renumbers. */

#include "SessionPlayback.h"

#include <algorithm>
#include <tuple>

#include "Engine.h"
#include "PatternTrack.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

namespace
{

//! The cell at (@a track, @a scene), or an empty one when the grid does not reach it.
const ClipSlot& cellOrEmpty( const SessionModel& model, int track, int scene )
{
	static const ClipSlot empty;
	return track < model.trackCount() && scene < model.sceneCount() ? model.slot( track, scene ) : empty;
}

//! What a cell renders: the fields publishSessionCell sends and the audio bake reads (SessionAudioBake.h).
auto contentOf( const ClipSlot& slot )
{
	return std::make_tuple( slot.type(), slot.patternId(), slot.audioSource(), slot.legato(), slot.loopStart(),
		slot.loopLength(), slot.gainDb(), slot.transpose(), slot.detune(), slot.warp(), slot.warpBpm(), slot.ramMode() );
}

//! Calls @a visit(track, scene, was, now) for every cell whose rendered content differs between the two grids.
template<typename Visit>
void forEachChangedCell( const SessionModel& before, const SessionModel& after, Visit visit )
{
	const int tracks = std::max( before.trackCount(), after.trackCount() );
	const int scenes = std::max( before.sceneCount(), after.sceneCount() );
	for( int track = 0; track < tracks; ++track )
	{
		for( int scene = 0; scene < scenes; ++scene )
		{
			const ClipSlot& was = cellOrEmpty( before, track, scene );
			const ClipSlot& now = cellOrEmpty( after, track, scene );
			if( !sameSessionContent( was, now ) ) { visit( track, scene, was, now ); }
		}
	}
}

bool lostItsClip( const ClipSlot& was, const ClipSlot& now ) { return now.isEmpty() && !was.isEmpty(); }

} // namespace


bool sameSessionContent( const ClipSlot& a, const ClipSlot& b ) noexcept
{
	return contentOf( a ) == contentOf( b );
}


int syncSessionLauncher( SessionScheduler& scheduler, const SessionModel& before, const SessionModel& after ) noexcept
{
	// The commands this change needs - a publish per changed cell, a stop per cell that lost its clip - counted
	// BEFORE any is queued. The model thread is the queue's only producer, so if they fit now they all land.
	std::size_t needed = 0;
	forEachChangedCell( before, after, [&needed]( int, int, const ClipSlot& was, const ClipSlot& now ) {
		needed += lostItsClip( was, now ) ? 2 : 1;
	} );
	if( needed > scheduler.commandQueueSpace() )
	{
		// Too many for the fixed queue: rather than leave some removed cell playing, the whole launcher stops at
		// once, as session.stop_all does (one atomic request, never refused). The next launch publishes the grid.
		scheduler.reset();
		return 0;
	}
	int published = 0;
	forEachChangedCell( before, after, [&]( int track, int scene, const ClipSlot& was, const ClipSlot& now ) {
		if( publishSessionCell( scheduler, track, scene, now ) ) { ++published; }
		// A cell that is gone stops as session.stop_slot stops it (a cell never launched ignores it).
		if( lostItsClip( was, now ) ) { scheduler.requestStop( track, scene, LaunchMode::Trigger, LaunchQuantisation::None ); }
	} );
	return published;
}


std::vector<SessionCellCopy> sessionCellsOfPatternTrack( Track* track )
{
	auto* pattern = dynamic_cast<PatternTrack*>( track );
	Song* song = Engine::getSong();
	if( pattern == nullptr || song == nullptr ) { return {}; }
	return song->sessionModel().cellsOfPattern( pattern->patternIndex() );
}


void restoreSessionPatternCells( const std::vector<SessionCellCopy>& cells, Track* restored )
{
	auto* pattern = dynamic_cast<PatternTrack*>( restored );
	Song* song = Engine::getSong();
	if( cells.empty() || pattern == nullptr || song == nullptr ) { return; }
	const SessionModel before = song->sessionModel();
	if( song->sessionModel().restoreCells( cells, pattern->patternIndex() ) > 0 )
	{
		syncSessionLauncher( song->sessionScheduler(), before, song->sessionModel() );
	}
}


std::vector<SessionCellCopy> SessionModel::cellsOfPattern( int pattern ) const
{
	std::vector<SessionCellCopy> cells;
	for( int track = 0; track < m_trackCount; ++track )
	{
		for( int scene = 0; scene < m_sceneCount; ++scene )
		{
			const ClipSlot& cell = slot( track, scene );
			if( cell.type() == ClipSlot::Type::Midi && cell.patternId() == pattern ) { cells.push_back( { track, scene, cell } ); }
		}
	}
	return cells;
}


int SessionModel::restoreCells( const std::vector<SessionCellCopy>& cells, int pattern )
{
	int restored = 0;
	for( const SessionCellCopy& copy : cells )
	{
		if( copy.track >= m_trackCount || copy.scene >= m_sceneCount ) { continue; }
		ClipSlot& cell = slot( copy.track, copy.scene );
		cell = copy.slot;
		cell.setPatternReference( pattern );
		++restored;
	}
	return restored;
}


int SessionModel::patternRemoved( int removed )
{
	int changed = 0;
	for( ClipSlot& slot : m_slots )
	{
		if( slot.type() != ClipSlot::Type::Midi || slot.patternId() < removed ) { continue; }
		if( slot.patternId() == removed ) { slot.clear(); }
		else { slot.setPatternReference( slot.patternId() - 1 ); }
		++changed;
	}
	return changed;
}


int SessionModel::patternsSwapped( int first, int second )
{
	int changed = 0;
	for( ClipSlot& slot : m_slots )
	{
		if( slot.type() != ClipSlot::Type::Midi || first == second ) { continue; }
		if( slot.patternId() == first ) { slot.setPatternReference( second ); }
		else if( slot.patternId() == second ) { slot.setPatternReference( first ); }
		else { continue; }
		++changed;
	}
	return changed;
}

} // namespace lmms
