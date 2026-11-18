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
 * Nothing is exported: these are private declarations shared by the two files. */

#ifndef LMMS_CONTROL_COMMANDS_SESSION_RECORD_INTERNAL_H
#define LMMS_CONTROL_COMMANDS_SESSION_RECORD_INTERNAL_H

#include <memory>

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "Clip.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "JournallingObject.h"
#include "ProjectJournal.h"
#include "SessionArrangementRecorder.h"
#include "SessionModel.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

namespace lmms
{
class SampleBuffer;

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
	int patternId = -1;
	const SampleBuffer* audioClip = nullptr;
	SessionArrangementRecorder::Event::ContentKind contentKind =
		SessionArrangementRecorder::Event::ContentKind::Unspecified;
};

//! One start event still waiting for its stop.
struct OpenStart
{
	int track = -1;
	int scene = -1;
	tick_t tick = 0;
	bool matched = false;
	int patternId = -1;
	const SampleBuffer* audioClip = nullptr;
	SessionArrangementRecorder::Event::ContentKind contentKind =
		SessionArrangementRecorder::Event::ContentKind::Unspecified;
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
			open[*openCount] = OpenStart{ event.track, event.scene, event.tick, false,
				event.patternId, event.audioClip, event.contentKind };
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
		pairs[pairCount].patternId = open[match].patternId;
		pairs[pairCount].audioClip = open[match].audioClip;
		pairs[pairCount].contentKind = open[match].contentKind;
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

//! The track a pair will create its clip on - the SAME two gates landPairState
//! applies before it writes (an in-range column, a span with length), plus that
//! column's own entry being there. Nullptr when the pair touches no track, and
//! therefore nothing a journal checkpoint could restore.
inline Track* landTouchTrack( const Song& song, const LandPair& pair )
{
	if( pair.track < 0 || pair.track >= static_cast<int>( song.tracks().size() ) ) { return nullptr; }
	if( pair.stopTick - pair.startTick <= 0 ) { return nullptr; }
	return song.tracks()[static_cast<std::size_t>( pair.track )];
}

/*! ONE composite journal checkpoint for ALL TOUCHED tracks, taken BEFORE the
 *  pass creates any clip. The per-pair loop does NOT carry it.
 *
 *  A checkpoint PER PAIR would be one undo step PER CLIP: the undo stack's own
 *  bound evicts a step per push, and a step that was evicted is gone - so a
 *  pass landing two clips on one track could lose the checkpoint that holds the
 *  track's state before the FIRST clip, and one undo would then restore only
 *  the later one. One composite step over the distinct touched tracks is the
 *  whole pass, whatever it lands. */
inline void checkpointLandTracks( const Song& song, const LandPair* pairs, int pairCount )
{
	QVector<JournallingObject*> touched;
	for( int i = 0; i < pairCount; ++i )
	{
		Track* track = landTouchTrack( song, pairs[i] );
		if( track != nullptr && track->isJournalling() && !touched.contains( track ) )
		{
			touched.append( track );
		}
	}
	ProjectJournal* journal = Engine::projectJournal();
	if( journal != nullptr && !touched.isEmpty() ) { journal->addJournalCheckPoint( touched ); }
}

//! Creates and reports one landed clip. Audio is loaded during preflight, before
//! the recorder ring is consumed, and passed here for the SampleClip to own.
QJsonObject landPairState( Song& song, const SessionModel& model, const LandPair& pair,
	const std::shared_ptr<const SampleBuffer>& audioSample );

} // namespace sessionrecord
} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_SESSION_RECORD_INTERNAL_H
