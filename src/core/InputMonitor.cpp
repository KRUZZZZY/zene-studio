/*
 * InputMonitor.cpp - R2.1: input monitoring - Off, Auto and In
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

#include "InputMonitor.h"

#include <algorithm>

#include "AudioEngine.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TrackRecorder.h"

namespace lmms
{

InputMonitorHandle::InputMonitorHandle(SampleTrack* track) :
	PlayHandle(PlayHandle::Type::InputMonitorHandle),
	m_track(track)
{
	setAudioBusHandle(track->audioBusHandle());
}


bool InputMonitorHandle::isFromTrack(const Track* track) const
{
	return m_track == track;
}


namespace
{

//! The track's record route: its position in the song (track.set_arm's rule), or -1.
//! The position is maintained by the container (TrackContainer::trackIndex), so the
//! per-period lookup is a hash read, not a scan of every track.
int routeOf(const Track* track)
{
	return Engine::getSong()->trackIndex(track);
}

//! The period's capture input as the engine holds it.
MonitorInputSource sourceOf(AudioEngine* engine)
{
	return {engine->inputWideBuffer(), engine->inputWideFrames(), engine->inputWideChannels(),
		engine->inputBuffer(), engine->inputBufferFrames()};
}

} // namespace


f_cnt_t copyMonitorInput(const MonitorInputSource& source, int channel,
	std::span<SampleFrame> buffer) noexcept
{
	if (source.wideFrames > 0)
	{
		// The wide capture is the input: a channel it does not carry is silence, not the
		// stereo bus's left side (that would monitor a different input).
		if (channel < 0 || channel >= source.wideChannels) { return 0; }
		const f_cnt_t frames = std::min(static_cast<f_cnt_t>(buffer.size()), source.wideFrames);
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			const float value = source.wide[static_cast<std::size_t>(f) * static_cast<std::size_t>(source.wideChannels)
				+ static_cast<std::size_t>(channel)];
			buffer[f] = SampleFrame(value, value);
		}
		return frames;
	}
	const f_cnt_t frames = std::min(static_cast<f_cnt_t>(buffer.size()), source.stereoFrames);
	const int side = channel == 1 ? 1 : 0;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buffer[f] = SampleFrame(source.stereo[f][side], source.stereo[f][side]);
	}
	return frames;
}


void InputMonitorHandle::play(std::span<SampleFrame> buffer)
{
	std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
	const MonitorMode mode = m_track->monitorMode();
	if (mode == MonitorMode::Off) { return; }
	AudioEngine* engine = Engine::audioEngine();
	const MultiTrackRecorder& recorder = engine->recorder();
	const int route = routeOf(m_track);
	const bool routed = route >= 0 && route < recorder.trackCount();
	const bool armed = routed && recorder.track(route).isArmed();
	if (!monitorPasses(mode, armed, m_track->isPlaying())) { return; }
	const f_cnt_t passed = copyMonitorInput(sourceOf(engine), routed ? recorder.track(route).inputChannel() : 0, buffer);
	m_framesPassed.fetch_add(static_cast<std::uint64_t>(passed), std::memory_order_relaxed);
}

} // namespace lmms
