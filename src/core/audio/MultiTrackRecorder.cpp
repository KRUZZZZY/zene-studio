/*
 * MultiTrackRecorder.cpp - the N-route multi-track capture owner (0.3.0)
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

#include "MultiTrackRecorder.h"
#include "RecorderTaps.h"

#include <algorithm>

#include "SampleFrame.h"

namespace lmms
{


MultiTrackRecorder::MultiTrackRecorder(int routeCapacity, int inputChannelCapacity)
{
	// Built once, here, off the audio thread: from now on the route list never
	// changes size, which is what lets processInput() walk it without a lock.
	const auto routes = std::clamp(routeCapacity, 1, MaxRoutes);  // the documented bound (RecorderTaps' table)
	m_tracks.reserve(static_cast<std::size_t>(routes));
	for (int i = 0; i < routes; ++i)
	{
		m_tracks.push_back(std::make_unique<TrackRecorder>());
	}
	setInputChannelCapacity(inputChannelCapacity);

	// The prototype's default mapping survives: route k starts on channel k
	// when the selectable range reaches that far, and on the last selectable
	// channel when it does not (a 2-route recorder over a 1-channel input).
	for (int i = 0; i < routes; ++i)
	{
		m_tracks[static_cast<std::size_t>(i)]->setInputChannel(i);
	}
}


MultiTrackRecorder::~MultiTrackRecorder()
{
	disarmAll();
}


int MultiTrackRecorder::inputChannelCapacity() const noexcept
{
	if (m_tracks.empty()) { return 1; }
	return m_tracks.front()->inputChannelCapacity();
}


void MultiTrackRecorder::setInputChannelCapacity(int channels)
{
	for (auto& track : m_tracks)
	{
		track->setInputChannelCapacity(channels);
	}
}


void MultiTrackRecorder::reconcileInputChannelCapacity()
{
	const int deliveredChannels = m_lastDeliveredInputChannels.exchange(0, std::memory_order_acq_rel);
	if (deliveredChannels > 0 && deliveredChannels != inputChannelCapacity())
	{
		setInputChannelCapacity(deliveredChannels);
	}
	for (auto& track : m_tracks)
	{
		if (track->hasUnavailableInput())
		{
			track->disarm();
		}
	}
}


void MultiTrackRecorder::processInput(const SampleFrame* input, f_cnt_t frames) noexcept
{
	if (input == nullptr)
	{
		return;
	}
	// The stereo bus is an interleaved buffer of DEFAULT_CHANNELS channels;
	// SampleFrame's two floats are adjacent, which is the same layout
	// TrackRecorder::processInput has always relied on.
	processInputInterleaved(input->data(), static_cast<int>(DEFAULT_CHANNELS), frames);
}


void MultiTrackRecorder::processInputInterleaved(const sample_t* interleaved, int channels,
	f_cnt_t frames) noexcept
{
	if (interleaved == nullptr || channels < 1)
	{
		return;
	}
	if (frames > 0)
	{
		m_lastDeliveredInputChannels.store(channels, std::memory_order_release);
	}
	for (std::size_t route = 0; route < m_tracks.size(); ++route)
	{
		// 0.5.0 WF-072/086: a route taking the master or a channel is fed there, not from the input.
		if (!recordertaps::tapped(static_cast<int>(route))) { m_tracks[route]->processInputInterleaved(interleaved, channels, frames); }
	}
}


bool MultiTrackRecorder::armTrack(int index, const std::string& filePath,
	int sampleRate, int inputChannel)
{
	if (index < 0 || index >= trackCount())
	{
		return false;
	}
	TrackRecorder& target = *m_tracks[static_cast<std::size_t>(index)];
	if (!target.inputChannelSelectable(inputChannel))
	{
		// Refused BEFORE the file is opened: a route that cannot be fed the
		// channel it was asked for must not leave a take file behind.
		return false;
	}
	return target.arm(filePath, sampleRate, inputChannel);
}


void MultiTrackRecorder::disarmAll()
{
	for (auto& track : m_tracks)
	{
		track->disarm();
	}
}


std::uint64_t MultiTrackRecorder::totalOverflowCount() const noexcept
{
	std::uint64_t total = 0;
	for (const auto& track : m_tracks)
	{
		total += track->overflowCount();
	}
	return total;
}


} // namespace lmms
