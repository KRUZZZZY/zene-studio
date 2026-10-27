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

#include "SessionModel.h"
#include "SessionScheduler.h"

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

} // namespace


bool sameSessionContent( const ClipSlot& a, const ClipSlot& b ) noexcept
{
	return contentOf( a ) == contentOf( b );
}


int syncSessionLauncher( SessionScheduler& scheduler, const SessionModel& before, const SessionModel& after ) noexcept
{
	const int tracks = std::max( before.trackCount(), after.trackCount() );
	const int scenes = std::max( before.sceneCount(), after.sceneCount() );
	int published = 0;
	for( int track = 0; track < tracks; ++track )
	{
		for( int scene = 0; scene < scenes; ++scene )
		{
			const ClipSlot& was = cellOrEmpty( before, track, scene );
			const ClipSlot& now = cellOrEmpty( after, track, scene );
			if( sameSessionContent( was, now ) ) { continue; }
			if( publishSessionCell( scheduler, track, scene, now ) ) { ++published; }
			// A cell that is gone stops as session.stop_slot stops it (a cell never launched ignores it).
			if( now.isEmpty() && !was.isEmpty() )
			{
				scheduler.requestStop( track, scene, LaunchMode::Trigger, LaunchQuantisation::None );
			}
		}
	}
	return published;
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
