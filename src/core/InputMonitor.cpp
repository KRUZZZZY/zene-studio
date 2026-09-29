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


void InputMonitorHandle::play(std::span<SampleFrame> buffer)
{
	std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
	const MonitorMode mode = m_track->monitorMode();
	if (mode == MonitorMode::Off) { return; }

	int route = -1;
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
	{
		if (tracks[i] == m_track) { route = i; break; }
	}
	AudioEngine* engine = Engine::audioEngine();
	const MultiTrackRecorder& recorder = engine->recorder();
	const bool routed = route >= 0 && route < recorder.trackCount();
	const bool armed = routed && recorder.track(route).isArmed();
	if (!monitorPasses(mode, armed, m_track->isPlaying())) { return; }
	const int channel = routed ? recorder.track(route).inputChannel() : 0;

	f_cnt_t passed = 0;
	const f_cnt_t wideFrames = engine->inputWideFrames();
	const int wideChannels = engine->inputWideChannels();
	if (wideFrames > 0 && channel < wideChannels)
	{
		const float* wide = engine->inputWideBuffer();
		passed = std::min(static_cast<f_cnt_t>(buffer.size()), wideFrames);
		for (f_cnt_t f = 0; f < passed; ++f)
		{
			const float value = wide[static_cast<std::size_t>(f) * static_cast<std::size_t>(wideChannels)
				+ static_cast<std::size_t>(channel)];
			buffer[f] = SampleFrame(value, value);
		}
	}
	else
	{
		const SampleFrame* input = engine->inputBuffer();
		passed = std::min(static_cast<f_cnt_t>(buffer.size()), engine->inputBufferFrames());
		const int side = channel == 1 ? 1 : 0;
		for (f_cnt_t f = 0; f < passed; ++f)
		{
			buffer[f] = SampleFrame(input[f][side], input[f][side]);
		}
	}
	m_framesPassed.fetch_add(static_cast<std::uint64_t>(passed), std::memory_order_relaxed);
}

} // namespace lmms
