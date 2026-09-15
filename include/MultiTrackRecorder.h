/*
 * MultiTrackRecorder.h - owns the per-route capture streams of the multi-track
 *                        recorder: one TrackRecorder per route, each fed from
 *                        one interleaved input channel (0.3.0)
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

#ifndef LMMS_MULTI_TRACK_RECORDER_H
#define LMMS_MULTI_TRACK_RECORDER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "LmmsTypes.h"
#include "TrackRecorder.h"

namespace lmms
{

class SampleFrame;


//! The multi-track recorder: N routes, each owning a TrackRecorder, an arm flag
//! and an input-channel index (prototype task #556; made N-wide and drivable by
//! feature row 14 / row 64 of docs/FEATURE-LIST-0.3.0.md).
/*!
 * WHAT CHANGED AND WHY. This class used to be the "two hardcoded per-track
 * capture streams" of the two-track prototype: NumTracks was a constexpr, the
 * storage was a std::array of exactly that many TrackRecorders, and each route
 * was pinned to the input channel of its own index. Feature row 64 asks for an
 * ARBITRARY input count, and row 14 records that no control-surface group drove
 * this object at all. Both are answered here and in the `record.*` group:
 *
 *  - the route count is a CONSTRUCTION parameter (default NumTracks, so every
 *    existing user of this class - the offline harness, the ALSA probe, the
 *    unit test, AudioEngine's member - keeps the shape it had);
 *  - every route's selectable input-channel range is likewise a construction
 *    parameter (default DEFAULT_CHANNELS, the stereo bus), so a route can be
 *    pointed at any channel an N-channel interface delivers, not just at 0/1;
 *  - nothing is allocated after construction. The routes are built once, off
 *    the audio thread, and processInput() only walks them - which is what keeps
 *    it realtime-safe.
 *
 * Threading contract (unchanged from the prototype):
 *  - the constructor and arm/disarm run off the audio thread;
 *  - processInput()/processInputInterleaved() are the audio thread's entry
 *    points and allocate nothing, lock nothing and make no syscall.
 */
class MultiTrackRecorder
{
public:
	//! The two-track prototype's count: the default route capacity, and the
	//! floor a configuration never goes below (AudioInputPath::recordRouteCapacity).
	static constexpr int NumTracks = 2;
	//! How many routes an engine prepares. A route is a file plus an input
	//! channel, so this is NOT the input count - several routes may record the
	//! same channel, and one route records one channel. The bound exists because
	//! each route pre-allocates its ring (TrackRecorder::RingCapacityFrames) at
	//! construction: 16 routes is about 4 MiB of rings, paid once, and is more
	//! routes than a small multi-track session has tracks.
	static constexpr int MaxRoutes = 16;

	//! \a routeCapacity routes (bounded below by 1), each able to select an
	//! input channel in [0, \a inputChannelCapacity).
	explicit MultiTrackRecorder(int routeCapacity = NumTracks,
		int inputChannelCapacity = static_cast<int>(DEFAULT_CHANNELS));
	~MultiTrackRecorder();

	MultiTrackRecorder(const MultiTrackRecorder&) = delete;
	MultiTrackRecorder& operator=(const MultiTrackRecorder&) = delete;

	//! The number of routes this recorder owns. Fixed at construction.
	int trackCount() const noexcept { return static_cast<int>(m_tracks.size()); }
	//! The number of interleaved input channels a route may select from.
	int inputChannelCapacity() const noexcept;
	//! Applies a new selectable range to every route (off the audio thread).
	void setInputChannelCapacity(int channels) noexcept;

	//! Route \a index. Undefined for an index outside [0, trackCount()).
	TrackRecorder& track(int index) noexcept { return *m_tracks[static_cast<std::size_t>(index)]; }
	const TrackRecorder& track(int index) const noexcept
	{
		return *m_tracks[static_cast<std::size_t>(index)];
	}

	//! Audio thread: feed one interleaved engine period to all armed routes.
	//! Realtime-safe. The stereo-bus overload is the interleaved one with
	//! DEFAULT_CHANNELS channels.
	void processInput(const SampleFrame* input, f_cnt_t frames) noexcept;
	//! Audio thread: feed one interleaved N-CHANNEL period (0.3.0, feature row
	//! 64). Realtime-safe.
	void processInputInterleaved(const sample_t* interleaved, int channels, f_cnt_t frames) noexcept;

	//! Off the audio thread: arm route \a index from input channel
	//! \a inputChannel, writing \a filePath. Returns false on a bad index, a
	//! channel outside the selectable range, or a file error.
	bool armTrack(int index, const std::string& filePath, int sampleRate, int inputChannel);
	//! Off the audio thread: disarm every route and flush their files.
	void disarmAll();

	std::uint64_t totalOverflowCount() const noexcept;

private:
	std::vector<std::unique_ptr<TrackRecorder>> m_tracks;
} ;


} // namespace lmms

#endif // LMMS_MULTI_TRACK_RECORDER_H
