/*
 * InputMeter.cpp - the level of the engine's audio input
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
#include "InputMeter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "SampleFrame.h"

namespace lmms::inputmeter
{

namespace
{

// The audio thread raises these every period: they must never fall back to a lock.
static_assert(std::atomic<float>::is_always_lock_free, "the input meter's peaks must be lock-free atomics");

std::atomic<float> s_left{0.f};
std::atomic<float> s_right{0.f};
std::atomic<float> s_liveLeft{0.f};   //!< the same maxima for the live meter's own read (MX-134)
std::atomic<float> s_liveRight{0.f};
std::atomic<float> s_watchLeft{0.f};  //!< and for the peak watcher's (Q050-100)
std::atomic<float> s_watchRight{0.f};

#ifdef LMMS_TESTING
// The test seam: the raise's observation point. It is null in production, and it is only ever
// installed by a test (InputMeterTest.cpp).
detail::PeakObservationHook s_peakObservationHook = nullptr;
#endif
//! Q050-340: per reader, per capture channel.
std::array<std::array<std::atomic<float>, MaxChannels>, 2> s_channels{};
std::atomic<int> s_channelCount{0};

void raise(std::atomic<float>& held, float value)
{
	float current = held.load(std::memory_order_relaxed);
#ifdef LMMS_TESTING
	if (s_peakObservationHook != nullptr) { s_peakObservationHook(held, current); }
#endif
	// The raise is atomic with respect to a reader's clear: the sample is only skipped when the cell
	//  still holds what was observed. A reader that cleared the cell in between makes the
	//  compare/exchange fail, so the sample is raised into the cleared cell instead of lost with it.
	while (true)
	{
		const float desired = value > current ? value : current;
		if (held.compare_exchange_weak(current, desired, std::memory_order_relaxed)) { return; }
	}
}

} // namespace

#ifdef LMMS_TESTING
void detail::setPeakObservationHook(PeakObservationHook hook)
{
	s_peakObservationHook = hook;
}
#endif

void measure(const SampleFrame* buffer, f_cnt_t frames)
{
	if (buffer == nullptr || frames <= 0) { return; }
	float left = 0.f;
	float right = 0.f;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		left = std::max(left, std::abs(buffer[f][0]));
		right = std::max(right, std::abs(buffer[f][1]));
	}
	raise(s_left, left);
	raise(s_right, right);
	raise(s_liveLeft, left);
	raise(s_liveRight, right);
	raise(s_watchLeft, left);
	raise(s_watchRight, right);
}

namespace
{

void raiseChannel(int channel, float peak)
{
	for (auto& reader : s_channels) { raise(reader[static_cast<std::size_t>(channel)], peak); }
}

} // namespace

void measureChannels(const float* interleaved, int channels, f_cnt_t frames)
{
	if (interleaved == nullptr || frames <= 0 || channels <= 0) { return; }
	channels = std::min(channels, MaxChannels);
	const auto width = static_cast<std::size_t>(channels);
	for (int c = 0; c < channels; ++c)
	{
		float peak = 0.f;
		for (f_cnt_t f = 0; f < frames; ++f) { peak = std::max(peak, std::abs(interleaved[static_cast<std::size_t>(f) * width + static_cast<std::size_t>(c)])); }
		raiseChannel(c, peak);
	}
	s_channelCount.store(channels, std::memory_order_relaxed);
}

void measureStereoChannels(const SampleFrame* buffer, f_cnt_t frames)
{
	if (buffer == nullptr || frames <= 0) { return; }
	float left = 0.f;
	float right = 0.f;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		left = std::max(left, std::abs(buffer[f][0]));
		right = std::max(right, std::abs(buffer[f][1]));
	}
	raiseChannel(0, left);
	raiseChannel(1, right);
	s_channelCount.store(2, std::memory_order_relaxed);
}

int channelCount() { return s_channelCount.load(std::memory_order_relaxed); }

void takeChannels(Reader reader, float* peaks, int count)
{
	auto& held = s_channels[static_cast<std::size_t>(reader)];
	for (int c = 0; c < std::min(count, MaxChannels); ++c) { peaks[c] = held[static_cast<std::size_t>(c)].exchange(0.f, std::memory_order_relaxed); }
	for (int c = MaxChannels; c < count; ++c) { peaks[c] = 0.f; }
}

void take(float* left, float* right)
{
	// A null output is a side the reader does not want: it is not read, so its peak stays held for the next reader.
	if (left != nullptr) { *left = s_left.exchange(0.f, std::memory_order_relaxed); }
	if (right != nullptr) { *right = s_right.exchange(0.f, std::memory_order_relaxed); }
}

void live(float* left, float* right)
{
	if (left != nullptr) { *left = s_liveLeft.exchange(0.f, std::memory_order_relaxed); }
	if (right != nullptr) { *right = s_liveRight.exchange(0.f, std::memory_order_relaxed); }
}

void watch(float* left, float* right)
{
	*left = s_watchLeft.exchange(0.f, std::memory_order_relaxed);
	*right = s_watchRight.exchange(0.f, std::memory_order_relaxed);
}

} // namespace lmms::inputmeter
