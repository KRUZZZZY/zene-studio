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

void raise(std::atomic<float>& held, float value)
{
	float current = held.load(std::memory_order_relaxed);
	while (value > current && !held.compare_exchange_weak(current, value, std::memory_order_relaxed)) { }
}

} // namespace

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
}

void take(float* left, float* right)
{
	*left = s_left.exchange(0.f, std::memory_order_relaxed);
	*right = s_right.exchange(0.f, std::memory_order_relaxed);
}

} // namespace lmms::inputmeter
