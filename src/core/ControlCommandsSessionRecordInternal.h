/*
 * ControlCommandsSessionRecordInternal.h - what the two halves of the
 *                                          Arrangement Record group share
 *                                          (task #641, SPEC-zene-studio §4.1).
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

/* WHY THIS HEADER EXISTS, and why it is private to src/core. The Arrangement
 * Record group is the TAP (arm, status, back-to-arrangement in
 * ControlCommandsSessionRecord.cpp) and the PASS that writes the recorded
 * performance into the arrangement (ControlCommandsSessionRecordLand.cpp). Both
 * halves have to read the same ring the same way - pair a launch with the stop
 * that ended it - and the pass lives in its own translation unit because this
 * fork's file-length ratchet measures a file as a unit (the group measured 518
 * lines against the 500-line limit when it was one file).
 *
 * The alternative - each half pairing for itself - is the defect the header of
 * ControlVocabulary.cpp records: two lanes re-deriving the same helper, ending in
 * a duplicate-symbol link failure. This is the same seam, and the same argument,
 * as ControlCommandsSessionShared.h beside it.
 *
 * Nothing is exported: these are inline definitions in a private namespace. */

#ifndef LMMS_CONTROL_COMMANDS_SESSION_RECORD_INTERNAL_H
#define LMMS_CONTROL_COMMANDS_SESSION_RECORD_INTERNAL_H

#include <QJsonObject>
#include <QString>

#include "Clip.h"
#include "ControlVocabulary.h"
#include "SessionArrangementRecorder.h"
#include "SessionModel.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

namespace lmms
{
namespace sessionrecord
{

//! Events one pass holds at once: the ring's own capacity, so a pass is bounded
//! by the same number the producer is (SessionArrangementRecorder.h).
constexpr std::size_t MaxLandEvents = SessionArrangementRecorder::Capacity;

//! One completed start/stop pair: what a landed arrangement clip is made of.
struct LandPair
{
	int track = -1;
	int scene = -1;
	tick_t startTick = 0;
	tick_t stopTick = 0;
};

//! One start event still waiting for its stop.
struct OpenStart
{
	int track = -1;
	int scene = -1;
	tick_t tick = 0;
	bool matched = false;
};

/*! Pairs the snapshot's events. Returns the number of complete pairs written to
 *  \a pairs, and leaves every start that never found a stop in \a open.
 *
 *  A STOP WITH NO START is ignored and counted by the caller: the engine records
 *  a stop only for a slot it has seen start, plus once per playing slot at a
 *  reset (SessionScheduler::consumeResetRequest), so an unmatched stop means the
 *  ring was written by something newer than this reader - a measurement, not a
 *  fault to hide. */
inline int landPairEvents( const SessionArrangementRecorder::Event* events, std::size_t count,
	LandPair* pairs, int maxPairs, OpenStart* open, int* openCount, int maxOpen,
	int* unmatchedStops )
{
	int pairCount = 0;
	*openCount = 0;
	*unmatchedStops = 0;
	for( std::size_t i = 0; i < count; ++i )
	{
		const SessionArrangementRecorder::Event& event = events[i];
		if( event.started )
		{
			if( *openCount >= maxOpen ) { continue; }
			open[*openCount] = OpenStart{ event.track, event.scene, event.tick, false };
			++( *openCount );
			continue;
		}
		int match = -1;
		for( int candidate = 0; candidate < *openCount; ++candidate )
		{
			if( !open[candidate].matched && open[candidate].track == event.track
				&& open[candidate].scene == event.scene )
			{
				match = candidate;
				break;
			}
		}
		if( match < 0 )
		{
			++( *unmatchedStops );
			continue;
		}
		open[match].matched = true;
		if( pairCount >= maxPairs ) { continue; }
		pairs[pairCount].track = event.track;
		pairs[pairCount].scene = event.scene;
		pairs[pairCount].startTick = open[match].tick;
		pairs[pairCount].stopTick = event.tick;
		++pairCount;
	}
	// Compact the open list: only an unmatched start is one.
	int remaining = 0;
	for( int i = 0; i < *openCount; ++i )
	{
		if( !open[i].matched ) { open[remaining] = open[i]; ++remaining; }
	}
	*openCount = remaining;
	return pairCount;
}

//! Consumes exactly \a count events, oldest first: the caller has already
//! decided that every one of them can be landed (or is an unmatched stop, which
//! has no clip to make).
inline void consumeLandEvents( SessionArrangementRecorder& recorder, std::size_t count )
{
	SessionArrangementRecorder::Event event;
	std::size_t seen = 0;
	while( seen < count && recorder.pop( event ) ) { ++seen; }
}

//! The JSON a land reply carries for one pair - whether a clip was created for
//! it or why it was not.
inline QJsonObject landPairState( Song& song, const SessionModel& model, const LandPair& pair )
{
	QJsonObject landed;
	landed.insert( QStringLiteral( "track_index" ), pair.track );
	landed.insert( QStringLiteral( "scene" ), pair.scene );
	landed.insert( QStringLiteral( "start_tick" ), static_cast<int>( pair.startTick ) );
	landed.insert( QStringLiteral( "stop_tick" ), static_cast<int>( pair.stopTick ) );
	if( pair.track < 0 || pair.track >= static_cast<int>( song.tracks().size() ) )
	{
		// The engine takes a track over by its POSITION in the song's track
		// list, and the session grid may be wider than that list (the
		// launchableColumns rule session.launch_slot states).
		landed.insert( QStringLiteral( "landed" ), false );
		landed.insert( QStringLiteral( "reason" ), QStringLiteral( "column_has_no_song_track" ) );
		return landed;
	}
	const tick_t length = pair.stopTick - pair.startTick;
	if( length <= 0 )
	{
		landed.insert( QStringLiteral( "landed" ), false );
		landed.insert( QStringLiteral( "reason" ), QStringLiteral( "empty_span" ) );
		return landed;
	}
	Track* track = song.tracks()[static_cast<std::size_t>( pair.track )];
	if( track != nullptr )
	{
		// TrackContentWidget::mousePressEvent checkpoints the track and then
		// calls Track::createClip(); do the same, so the journal owns a real
		// inverse (the clip.add and midi.retro_capture_to_clip shape).
		track->addJournalCheckPoint();
	}
	const ClipSlot& slot = model.slot( pair.track, pair.scene );
	Clip* clip = track != nullptr ? track->createClip( TimePos( pair.startTick ) ) : nullptr;
	if( clip == nullptr )
	{
		landed.insert( QStringLiteral( "landed" ), false );
		landed.insert( QStringLiteral( "reason" ), QStringLiteral( "track_created_no_clip" ) );
		return landed;
	}
	clip->changeLength( TimePos( length ) );
	// A landed span is a MANUALLY resized clip: the arrangement record says how
	// long the performance was, and an auto-resizing clip would shrink to its
	// (here empty) content instead.
	clip->setAutoResize( false );
	if( !slot.name().isEmpty() ) { clip->setName( slot.name() ); }
	clip->dataChanged();

	landed.insert( QStringLiteral( "landed" ), true );
	landed.insert( QStringLiteral( "track" ), control::trackIdOf( track ) );
	landed.insert( QStringLiteral( "clip" ), control::clipIdOf( clip ) );
	landed.insert( QStringLiteral( "position" ),
		static_cast<int>( clip->startPosition().getTicks() ) );
	landed.insert( QStringLiteral( "length" ), static_cast<int>( clip->length().getTicks() ) );
	landed.insert( QStringLiteral( "name" ), clip->name() );
	// The PatternStore reference the session slot names, reported rather than
	// copied: this build does not fill the clip's notes from it (see the file
	// header of ControlCommandsSessionRecordLand.cpp). -1 means the slot carries
	// no pattern reference at all.
	landed.insert( QStringLiteral( "pattern" ), slot.patternId() );
	landed.insert( QStringLiteral( "content" ),
		slot.patternId() >= 0 ? QStringLiteral( "pattern_reference_not_copied" )
			: QStringLiteral( "empty" ) );
	return landed;
}

} // namespace sessionrecord
} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_SESSION_RECORD_INTERNAL_H
