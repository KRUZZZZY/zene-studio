/*
 * CarlaRenderBuffers.h - planar scratch for one Carla process call
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_CARLA_RENDER_BUFFERS_H
#define LMMS_CARLA_RENDER_BUFFERS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "SampleFrame.h"

namespace lmms
{

//! Planar stereo scratch for one Carla process call, allocated when the instrument is built.
/*! cluster new-p10-545: CarlaInstrument::playImpl() used two variable-length stack arrays sized
 *  from the caller's output span. The span is an audio-device period, so that reserved a period of
 *  stack on whichever thread rendered the instrument - the audio thread for the Rack and Patchbay.
 *  The planes here hold the engine's largest period (MAXIMUM_BUFFER_SIZE) and are owned by the
 *  instrument, so the render path never touches the stack or grows a container. A request longer
 *  than the planes is clipped; the caller leaves the frames beyond it silent.
 */
class CarlaRenderBuffers
{
public:
	//! The audio engine's largest period; CarlaInstrument::playImpl() static-asserts this against
	//! AudioEngine's MAXIMUM_BUFFER_SIZE so the two cannot drift apart.
	static constexpr std::size_t MaxPeriodFrames = 4096;

	CarlaRenderBuffers() : CarlaRenderBuffers(MaxPeriodFrames) {}

	explicit CarlaRenderBuffers(std::size_t capacity)
		: m_left(capacity)
		, m_right(capacity)
	{
	}

	//! Clears the first @a requestedFrames of both planes and returns how many frames may be
	//! rendered, never more than capacity(). Audio thread: allocates, frees, locks and waits nothing.
	auto prepare(std::size_t requestedFrames) noexcept -> std::uint32_t
	{
		const auto frames = std::min(requestedFrames, m_left.size());
		std::fill_n(m_left.data(), frames, 0.0f);
		std::fill_n(m_right.data(), frames, 0.0f);
		return static_cast<std::uint32_t>(frames);
	}

	//! The whole render step of CarlaInstrument::playImpl(): clears the planes, lets @a process fill them
	//! (it receives the two plane pointers and the frame count) and interleaves the result into @a out.
	//! Frames of @a out beyond capacity() are not touched; the caller has already left them silent.
	//! Audio thread: reserves a fixed few pointers of stack whatever the period is, and allocates nothing.
	template<typename Process>
	void render(std::span<SampleFrame> out, Process&& process)
	{
		const auto frames = prepare(out.size());
		float* planes[] = { left(), right() };
		process(planes, frames);
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			out[i][0] = planes[0][i];
			out[i][1] = planes[1][i];
		}
	}

	auto left() noexcept -> float* { return m_left.data(); }
	auto right() noexcept -> float* { return m_right.data(); }
	auto capacity() const noexcept -> std::size_t { return m_left.size(); }

private:
	std::vector<float> m_left;
	std::vector<float> m_right;
};

} // namespace lmms

#endif // LMMS_CARLA_RENDER_BUFFERS_H
