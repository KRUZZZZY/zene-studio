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
#include <cstdint>

#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "MultiTrackRecorder.h"
#include "TrackRecorder.h"

namespace lmms::recordertaps
{

namespace
{

//! One route's tap in one word, so a tap read while the UI thread changes it is the old one or the new one,
//! never half of each: the channel id + 1 in the high 32 bits (a zero word is the default Tap), then the
//! point, the side and the source a byte each.
using Entry = std::atomic<std::uint64_t>;

std::uint64_t pack(const Tap& tap)
{
	const std::uint32_t channel = static_cast<std::uint32_t>(tap.channelId) + 1u;
	return std::uint64_t{channel} << 32 | std::uint64_t{static_cast<std::uint8_t>(tap.point)} << 16
		| std::uint64_t{static_cast<std::uint8_t>(tap.side)} << 8 | std::uint64_t{static_cast<std::uint8_t>(tap.source)};
}

Tap unpack(std::uint64_t word)
{
	const auto channel = static_cast<std::int32_t>(static_cast<std::uint32_t>(word >> 32) - 1u);
	return {static_cast<Source>(word & 0xff), channel, static_cast<int>((word >> 16) & 0xff), static_cast<int>((word >> 8) & 0xff)};
}

std::array<Entry, MultiTrackRecorder::MaxRoutes>& table()
{
	static std::array<Entry, MultiTrackRecorder::MaxRoutes> entries{};  // every route on the input
	return entries;
}

//! Route @a route's tap as the audio thread reads it.
Tap tapOf(std::size_t route) noexcept
{
	return unpack(table()[route].load(std::memory_order_acquire));
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
	const bool wasTapped = unpack(entry.load()).source != Source::Input;
	const bool isTapped = tap.source != Source::Input;
	// The count rises before a tap is published and falls after one is withdrawn: no block finds the
	// route skipped by the input path while the taps still read a count of 0.
	if (isTapped && !wasTapped) { tappedCount().fetch_add(1); }
	entry.store(pack(tap));
	if (!isTapped && wasTapped) { tappedCount().fetch_sub(1); }
}

Tap get(int route)
{
	if (route < 0 || route >= MultiTrackRecorder::MaxRoutes) { return {}; }
	return unpack(table()[static_cast<std::size_t>(route)].load());
}

bool tapped(int route) noexcept
{
	return route >= 0 && route < MultiTrackRecorder::MaxRoutes && tapOf(static_cast<std::size_t>(route)).source != Source::Input;
}

void channelAt(const MixerChannel& channel, SidechainTapPoint point, f_cnt_t frames) noexcept
{
	if (tappedCount().load(std::memory_order_relaxed) == 0) { return; }
	const bool postFader = point == SidechainTapPoint::PostFader || point == SidechainTapPoint::PostFaderNoGain;
	for (std::size_t route = 0; route < table().size(); ++route)
	{
		const Tap tap = tapOf(route);
		if (tap.source != Source::Channel || tap.channelId != channel.id() || tap.point != static_cast<int>(point)) { continue; }
		const int side = tap.side;
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
		const Tap tap = tapOf(route);
		if (tap.source != Source::Master) { continue; }
		push(static_cast<int>(route), buffer[0].data() + tap.side, 2, frames);
	}
}

} // namespace lmms::recordertaps
