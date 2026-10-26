/*
 * SamplerTakeRecorder.cpp - capture the input straight into a sampler (0.5.0 L10, Q050-156)
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

#include "SamplerTakeRecorder.h"

#include <algorithm>

#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "SampleRecordAccumulator.h"

namespace lmms
{

SamplerTakeRecorder::SamplerTakeRecorder() = default;

SamplerTakeRecorder::~SamplerTakeRecorder() = default;

bool SamplerTakeRecorder::arm(const QString& trackId, const QString& device,
	sample_rate_t sampleRate, f_cnt_t latencyFrames, std::uint64_t targetFrames)
{
	if (state() != State::Idle || sampleRate <= 0 || targetFrames == 0) { return false; }

	m_accum = std::make_unique<SampleRecordAccumulator>(sampleRate, RingFrames);
	m_result.reset();
	m_trackId = trackId;
	m_device = device;
	m_sampleRate = sampleRate;
	m_latency = std::max<f_cnt_t>(0, latencyFrames);
	m_target = targetFrames;
	m_skip = static_cast<std::uint64_t>(m_latency);
	m_taken = 0;
	m_recorded.store(0, std::memory_order_relaxed);
	setState(State::Recording);
	return true;
}

void SamplerTakeRecorder::stop()
{
	if (state() != State::Recording) { return; }
	finishTake();
}

void SamplerTakeRecorder::cancel()
{
	if (m_accum != nullptr)
	{
		m_accum->requestFinish();
		m_accum->finish();
	}
	m_accum.reset();
	m_result.reset();
	m_trackId.clear();
	m_device.clear();
	m_sampleRate = 0;
	m_latency = 0;
	m_target = 0;
	m_skip = 0;
	m_taken = 0;
	m_recorded.store(0, std::memory_order_relaxed);
	setState(State::Idle);
}

std::shared_ptr<const SampleBuffer> SamplerTakeRecorder::takeFinished()
{
	if (state() != State::Done || m_accum == nullptr) { return nullptr; }
	if (m_result == nullptr) { m_result = m_accum->finish(); }
	return m_result;
}

void SamplerTakeRecorder::releaseFinished()
{
	m_accum.reset();
	m_result.reset();
	m_trackId.clear();
	m_device.clear();
	m_sampleRate = 0;
	m_latency = 0;
	m_target = 0;
	m_skip = 0;
	m_taken = 0;
	m_recorded.store(0, std::memory_order_relaxed);
	setState(State::Idle);
}

std::uint64_t SamplerTakeRecorder::framesDropped() const noexcept
{
	return m_accum != nullptr ? m_accum->framesDropped() : 0;
}

void SamplerTakeRecorder::finishTake() noexcept
{
	if (m_accum != nullptr) { m_accum->requestFinish(); }
	setState(State::Done);
}

void SamplerTakeRecorder::push(const SampleFrame* input, f_cnt_t frames) noexcept
{
	if (m_state.load(std::memory_order_acquire) != static_cast<int>(State::Recording)
		|| m_accum == nullptr || input == nullptr || frames <= 0)
	{
		return;
	}

	// The head is the engine's recording round trip; it is dropped before the take starts.
	const auto skipped = static_cast<f_cnt_t>(std::min<std::uint64_t>(m_skip, static_cast<std::uint64_t>(frames)));
	input += skipped;
	frames -= skipped;
	m_skip -= skipped;
	if (frames <= 0) { return; }

	const auto left = m_target - m_taken;
	const auto take = static_cast<f_cnt_t>(std::min<std::uint64_t>(left, static_cast<std::uint64_t>(frames)));
	m_accum->append(input, take);
	m_taken += static_cast<std::uint64_t>(take);
	m_recorded.store(m_taken, std::memory_order_relaxed);
	if (m_taken >= m_target) { finishTake(); }
}

} // namespace lmms
