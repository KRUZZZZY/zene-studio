/*
 * DeltaTap.cpp - hear only what a device changes: its output minus its input
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

#include "DeltaTap.h"

#include <algorithm>

#include "AudioBuffer.h"
#include "AudioBus.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "Engine.h"

namespace lmms
{

void DeltaTap::setEnabled(bool on, f_cnt_t frames)
{
	const auto needed = static_cast<std::size_t>(frames) * 2;
	if (!on || m_dry.size() >= needed)
	{
		m_on.store(on, std::memory_order_release);
		return;
	}
	// A larger scratch: allocated while the audio thread is held off, so it never sees it move.
	AudioEngine* engine = Engine::audioEngine();
	engine->requestChangeInModel();
	m_dry.assign(needed, 0.f);
	m_on.store(true, std::memory_order_release);
	engine->doneChangeInModel();
}

bool DeltaTap::process(Effect& effect, AudioBus& bus) noexcept
{
	const auto count = static_cast<std::size_t>(bus.frames()) * 2;
	if (!enabled() || count > m_dry.size()) { return effect.processAudioBuffer(bus); }
	float* samples = bus[0];
	std::copy(samples, samples + count, m_dry.begin());
	const bool more = effect.processAudioBuffer(bus);
	for (std::size_t i = 0; i < count; ++i) { samples[i] -= m_dry[i]; }
	// The delta of a silent output is the inverted input: never flagged quiet.
	bus.quietChannels().set(0, false);
	bus.quietChannels().set(1, false);
	return more;
}

bool DeltaTap::process(Effect& effect, AudioBuffer& buffer) noexcept
{
	const auto frames = static_cast<std::size_t>(buffer.frames());
	if (!enabled() || frames * 2 > m_dry.size()) { return effect.processAudioBuffer(buffer); }
	float* left = buffer.buffer(0).data();
	float* right = buffer.buffer(1).data();
	std::copy(left, left + frames, m_dry.begin());
	std::copy(right, right + frames, m_dry.begin() + static_cast<std::ptrdiff_t>(frames));
	const bool more = effect.processAudioBuffer(buffer);
	for (std::size_t i = 0; i < frames; ++i)
	{
		left[i] -= m_dry[i];
		right[i] -= m_dry[frames + i];
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);
	return more;
}

} // namespace lmms
