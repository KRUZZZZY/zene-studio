/*
 * RecorderTaps.cpp - recording the master or a channel's output instead of the input
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
#include "RecorderTaps.h"

#include <array>
#include <atomic>

#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "MultiTrackRecorder.h"
#include "TrackRecorder.h"

namespace lmms::recordertaps
{

namespace
{

struct Entry
{
	std::atomic<int> source{0};
	std::atomic<int> channelId{-1};
	std::atomic<int> point{0};
	std::atomic<int> side{0};
};

std::array<Entry, MultiTrackRecorder::MaxRoutes>& table()
{
	static std::array<Entry, MultiTrackRecorder::MaxRoutes> entries;
	return entries;
}

//! How many routes take something other than the input: 0 keeps every tap a single load.
std::atomic<int>& tappedCount()
{
	static std::atomic<int> count{0};
	return count;
}

//! Push @a frames frames of one side into route @a route: @a samples strided by @a stride (1 planar, 2 interleaved).
void push(int route, const float* samples, int stride, f_cnt_t frames) noexcept
{
	MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
	if (route < recorder.trackCount()) { recorder.track(route).processInputInterleaved(samples, stride, frames); }
}

} // namespace

void set(int route, const Tap& tap)
{
	if (route < 0 || route >= MultiTrackRecorder::MaxRoutes) { return; }
	Entry& entry = table()[static_cast<std::size_t>(route)];
	const bool wasTapped = entry.source.load() != 0;
	entry.channelId.store(tap.channelId);
	entry.point.store(tap.point);
	entry.side.store(tap.side);
	entry.source.store(static_cast<int>(tap.source));  // published last: the rest is in place before a tap reads it
	const bool isTapped = tap.source != Source::Input;
	if (isTapped != wasTapped) { tappedCount().fetch_add(isTapped ? 1 : -1); }
}

Tap get(int route)
{
	if (route < 0 || route >= MultiTrackRecorder::MaxRoutes) { return {}; }
	const Entry& entry = table()[static_cast<std::size_t>(route)];
	return {static_cast<Source>(entry.source.load()), entry.channelId.load(), entry.point.load(), entry.side.load()};
}

bool tapped(int route) noexcept
{
	return route >= 0 && route < MultiTrackRecorder::MaxRoutes && table()[static_cast<std::size_t>(route)].source.load(std::memory_order_relaxed) != 0;
}

void channelAt(const MixerChannel& channel, SidechainTapPoint point, f_cnt_t frames) noexcept
{
	if (tappedCount().load(std::memory_order_relaxed) == 0) { return; }
	const bool postFader = point == SidechainTapPoint::PostFader || point == SidechainTapPoint::PostFaderNoGain;
	for (std::size_t route = 0; route < table().size(); ++route)
	{
		const Entry& entry = table()[route];
		if (entry.source.load(std::memory_order_acquire) != static_cast<int>(Source::Channel) || entry.channelId.load(std::memory_order_relaxed) != channel.id()
			|| entry.point.load(std::memory_order_relaxed) != static_cast<int>(point)) { continue; }
		const int side = entry.side.load(std::memory_order_relaxed);
		// After the fader the channel's signal is planar (one array a side); before it, interleaved frames.
		if (postFader) { push(static_cast<int>(route), channel.m_postFaderBuffer.buffer(static_cast<std::size_t>(side)).data(), 1, frames); }
		else { push(static_cast<int>(route), channel.m_buffer[0].data() + side, 2, frames); }
	}
}

void master(const SampleFrame* buffer, f_cnt_t frames) noexcept
{
	if (tappedCount().load(std::memory_order_relaxed) == 0 || buffer == nullptr) { return; }
	for (std::size_t route = 0; route < table().size(); ++route)
	{
		const Entry& entry = table()[route];
		if (entry.source.load(std::memory_order_acquire) != static_cast<int>(Source::Master)) { continue; }
		push(static_cast<int>(route), buffer[0].data() + entry.side.load(std::memory_order_relaxed), 2, frames);
	}
}

} // namespace lmms::recordertaps
