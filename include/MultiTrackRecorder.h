/*
 * MultiTrackRecorder.h - owns the hardcoded two-track capture streams of the
 *                        two-track recording prototype
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_MULTI_TRACK_RECORDER_H
#define LMMS_MULTI_TRACK_RECORDER_H

#include <array>
#include <cstdint>
#include <string>

#include "LmmsTypes.h"
#include "TrackRecorder.h"

namespace lmms
{

class SampleFrame;


//! Two hardcoded per-track capture streams (prototype, task #556).
/*!
 * The minimal stand-in for per-SampleTrack arm/input-selection state: each
 * track owns a TrackRecorder, an arm flag and an input-channel index. Track 0
 * defaults to input channel 0 and track 1 to input channel 1; the mapping is
 * changed with TrackRecorder::setInputChannel() while disarmed.
 *
 * processInput() is the single audio-thread entry point: it feeds one
 * interleaved engine period to every armed track.
 */
class MultiTrackRecorder
{
public:
	static constexpr int NumTracks = 2;

	MultiTrackRecorder();
	~MultiTrackRecorder();

	MultiTrackRecorder(const MultiTrackRecorder&) = delete;
	MultiTrackRecorder& operator=(const MultiTrackRecorder&) = delete;

	TrackRecorder& track(int index) { return m_tracks[index]; }
	const TrackRecorder& track(int index) const { return m_tracks[index]; }

	//! Audio thread: feed one interleaved engine period to all armed tracks.
	//! Realtime-safe.
	void processInput(const SampleFrame* input, f_cnt_t frames) noexcept;

	//! Off the audio thread: arm track \a index from input channel
	//! \a inputChannel, writing \a filePath. Returns false on bad index or
	//! file error.
	bool armTrack(int index, const std::string& filePath, int sampleRate, int inputChannel);
	//! Off the audio thread: disarm both tracks and flush their files.
	void disarmAll();

	std::uint64_t totalOverflowCount() const noexcept;

private:
	std::array<TrackRecorder, NumTracks> m_tracks;
} ;


} // namespace lmms

#endif // LMMS_MULTI_TRACK_RECORDER_H
