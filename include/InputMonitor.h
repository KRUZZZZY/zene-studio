/*
 * InputMonitor.h - R2.1: input monitoring - Off, Auto and In
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

#ifndef LMMS_INPUT_MONITOR_H
#define LMMS_INPUT_MONITOR_H

#include <atomic>
#include <cstdint>

#include "MonitorMode.h"
#include "PlayHandle.h"
#include "lmms_export.h"

namespace lmms
{

class SampleTrack;

/*! The monitor path of ONE sample track: a play handle on the track's own bus, so the input
 *  goes through the track's devices, fader, pan and mixer channel exactly like its clips.
 *  It exists only while the track's mode is not Off (SampleTrack adds and removes it), and
 *  it is PERSISTENT - the engine's song-stop clear keeps it, like an instrument's handle.
 *
 *  Each period it reads the input channel of the track's record ROUTE (the route is the
 *  track's position in the song, track.set_arm's own rule; the route's `inputChannel()`
 *  names the channel, and a track past the route count reads channel 0). The wide capture
 *  buffer is read when the backend fills it, the stereo input buffer otherwise; the channel
 *  is played mono to both sides. Allocation-free and lock-free: the route table is fixed at
 *  construction (MultiTrackRecorder) and the song's track list only changes under the
 *  engine's model-change lock, which excludes this render. */
class LMMS_EXPORT InputMonitorHandle : public PlayHandle
{
public:
	explicit InputMonitorHandle(SampleTrack* track);

	void play(std::span<SampleFrame> buffer) override;
	bool isFinished() const override { return false; }
	bool isFromTrack(const Track* track) const override;

	//! Frames the gate has passed since construction. Any thread; relaxed.
	std::uint64_t framesPassed() const noexcept { return m_framesPassed.load(std::memory_order_relaxed); }

private:
	SampleTrack* m_track;
	std::atomic<std::uint64_t> m_framesPassed{0};
};

} // namespace lmms

#endif // LMMS_INPUT_MONITOR_H
