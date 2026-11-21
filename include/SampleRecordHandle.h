/*
 * SampleRecordHandle.h - play-handle for recording a sample
 *
 * Copyright (c) 2008 Csaba Hruska <csaba.hruska/at/gmail.com>
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

#ifndef LMMS_SAMPLE_RECORD_HANDLE_H
#define LMMS_SAMPLE_RECORD_HANDLE_H

#include <cstdint>
#include <span>

#include "PlayHandle.h"
#include "PunchWindow.h"
#include "TimePos.h"

namespace lmms
{


class PatternTrack;
class SampleBuffer;
class SampleClip;
class SampleRecordAccumulator;
class Track;
struct PendingRecordTake;


//! One clip take. Built on the MAIN thread (SampleClip prepares it when the clip is armed or
//! recording starts) so the audio thread only claims it (SampleClip::claimRecordHandle) and
//! never allocates, starts a thread or touches the model to record. When the engine removes
//! the handle, its destructor hands the take to the main thread (reapRetiredTakes), which
//! finishes the accumulator and installs the take on the clip.
class SampleRecordHandle : public PlayHandle
{
public:
	//! Main thread only: allocates the accumulator and starts its drain thread.
	SampleRecordHandle( SampleClip* clip );
	//! Any thread, realtime-safe: never joins or touches the clip. The take is queued for the
	//! main thread.
	~SampleRecordHandle() override;

	void play( std::span<SampleFrame> buffer ) override;
	bool isFinished() const override;

	bool isFromTrack( const Track * _track ) const override;

	f_cnt_t framesRecorded() const;

	//! Audio thread: the clip claimed this take, so it is a take that was recorded (and is
	//! installed when it ends; an unclaimed one is only discarded).
	void markClaimed() noexcept { m_claimed = true; }
	//! Main thread, before the take is claimed: the arming (SampleClip::armGeneration) it was
	//! prepared under. A take of an earlier arming never disarms a re-armed clip.
	void setArmGeneration(std::uint64_t generation) noexcept;

	//! Main thread: finishes every take queued by a destroyed handle and installs it on its
	//! clip (a lane take while the transport goes on recording, else the clip itself).
	static void reapRetiredTakes();
	//! Main thread: reap, then top up the takes of every armed clip. Runs every 10 ms from the
	//! main-thread timer; tests call it directly.
	static void pumpTakes();
	//! Main thread: remember an armed clip so pumpTakes() keeps its takes prepared.
	static void watchArmedClip(SampleClip* clip);
	//! Frames the take ring dropped on overflow, summed over every take reaped so far. Each was
	//! padded with silence in its take (framesPaddedTotal), unless the gap queue was full.
	static std::uint64_t framesDroppedTotal() noexcept;
	static std::uint64_t framesPaddedTotal() noexcept;
	//! Recording takes that found no prepared handle when the transport started them.
	static std::uint64_t unpreparedTakes() noexcept;
	//! Takes diverted to the intrusive fallback because the primary queue was full; all are still
	//! reaped on the main thread.
	static std::uint64_t queueOverflows() noexcept;
	//! Takes the main thread has reaped (finished and handed back) so far.
	static std::uint64_t takesReaped() noexcept;
	//! Audio thread: count a take that could not be claimed (it is not recorded).
	static void countUnpreparedTake() noexcept;


private:
	virtual void writeBuffer( const SampleFrame* _ab,
						const f_cnt_t _frames );
	//! Period frames [from, to) into the take: live input inside the punch window, silence
	//! outside it (E3). No armed region: the window is the whole period.
	void writePunched( const SampleFrame* period, f_cnt_t from, f_cnt_t to, const PunchFrames& punch );

	// D9c (audit grade-B-recording.md): the take is staged into a fixed
	// pre-allocated ring and assembled on the accumulator's own thread, so the
	// audio thread neither allocates per period nor builds the take at stop.
	// See SampleRecordAccumulator.h. Owned by this handle until its destructor
	// queues it for the main thread.
	PendingRecordTake* m_take;
	SampleRecordAccumulator* m_accum;
	//! Set on the audio thread when the clip claims the take (see markClaimed()).
	bool m_claimed = false;
	f_cnt_t m_framesRecorded;
	TimePos m_minLength;

	Track * m_track;
	PatternTrack* m_patternTrack;
	SampleClip * m_clip;
	//! R2.2: frames still to drop from the take's head (the recording round trip).
	f_cnt_t m_compensationFrames;
	//! BUGS_FOUND 11.17: the start offset is folded into m_compensationFrames on the first play().
	bool m_offsetApplied;

} ;


} // namespace lmms

#endif // LMMS_SAMPLE_RECORD_HANDLE_H
