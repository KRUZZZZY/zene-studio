/*
 * Sample.cpp
 *
 * Copyright (c) 2025 saker <sakertooth@gmail.com>
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

#include "Sample.h"


namespace lmms {

Sample::Sample(const SampleFrame* data, size_t numFrames, int sampleRate)
	: m_buffer(std::make_shared<SampleBuffer>(data, numFrames, sampleRate))
	, m_startFrame(0)
	, m_endFrame(m_buffer->size())
	, m_loopStartFrame(0)
	, m_loopEndFrame(m_buffer->size())
{
}

Sample::Sample(std::shared_ptr<const SampleBuffer> buffer)
	: m_buffer(buffer)
	, m_startFrame(0)
	, m_endFrame(m_buffer->size())
	, m_loopStartFrame(0)
	, m_loopEndFrame(m_buffer->size())
{
}

Sample::Sample(const Sample& other)
	: m_buffer(other.m_buffer)
	, m_startFrame(other.startFrame())
	, m_endFrame(other.endFrame())
	, m_loopStartFrame(other.loopStartFrame())
	, m_loopEndFrame(other.loopEndFrame())
	, m_amplification(other.amplification())
	, m_frequency(other.frequency())
	, m_reversed(other.reversed())
{
}

Sample::Sample(Sample&& other) noexcept
	: m_buffer(std::move(other.m_buffer))
	, m_startFrame(other.startFrame())
	, m_endFrame(other.endFrame())
	, m_loopStartFrame(other.loopStartFrame())
	, m_loopEndFrame(other.loopEndFrame())
	, m_amplification(other.amplification())
	, m_frequency(other.frequency())
	, m_reversed(other.reversed())
{
}

auto Sample::operator=(const Sample& other) -> Sample&
{
	m_buffer = other.m_buffer;
	m_startFrame = other.startFrame();
	m_endFrame = other.endFrame();
	m_loopStartFrame = other.loopStartFrame();
	m_loopEndFrame = other.loopEndFrame();
	m_amplification = other.amplification();
	m_frequency = other.frequency();
	m_reversed = other.reversed();

	return *this;
}

auto Sample::operator=(Sample&& other) noexcept -> Sample&
{
	m_buffer = std::move(other.m_buffer);
	m_startFrame = other.startFrame();
	m_endFrame = other.endFrame();
	m_loopStartFrame = other.loopStartFrame();
	m_loopEndFrame = other.loopEndFrame();
	m_amplification = other.amplification();
	m_frequency = other.frequency();
	m_reversed = other.reversed();

	return *this;
}

bool Sample::play(SampleFrame* dst, PlaybackState* state, size_t numFrames, Loop loop, double ratio) const
{
	if (!m_buffer || m_buffer->empty()) { return false; }

	state->m_frameIndex = std::max<int>(m_startFrame, state->m_frameIndex);

	const auto sampleRateRatio = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / m_buffer->sampleRate();
	const auto freqRatio = frequency() / DefaultBaseFreq;
	const double resampleRatio = sampleRateRatio * freqRatio * ratio;
	state->m_resampler.setRatio(resampleRatio);

	/*! cluster null-1: a 1:1 read is the source, not the converter's output.
	 *
	 *  libsamplerate's converters are not identity filters, and the linear one -
	 *  the default - delays by ONE SAMPLE at ratio 1.0: it emits the first input
	 *  frame twice (measured on the linked library, all converter modes; see the
	 *  fix's log). That made an offline render of a unity clip at the source's
	 *  own rate exactly one sample late, and made `zene render` of a plain
	 *  sample track stop being bit-identical to the file it plays.
	 *
	 *  At ratio 1.0 with default pitch there is nothing to convert, so the read
	 *  is the source's frames and the delay cannot enter the render. This branch
	 *  is a read-and-copy of the same frames the converter would have consumed -
	 *  same cursor advance, same window/loop bounds, same amplification - so every
	 *  OTHER ratio keeps the converter path and its exact arithmetic. The frame
	 *  buffer staging (`m_bufferView`) is bypassed here and left empty, so a
	 *  ratio change in a later call cannot replay stale staged frames. */
	if (resampleRatio == 1.0)
	{
		if (!state->m_bufferView.empty())
		{
			// Staged frames are unconsumed source frames; at unity they are the output as well.
			const auto staged = std::min(numFrames, state->m_bufferView.size());
			std::copy_n(state->m_bufferView.data(), staged, dst);
			state->m_bufferView = state->m_bufferView.subspan(staged);
			dst += staged;
			numFrames -= staged;
		}
		while (numFrames > 0)
		{
			const auto chunk = std::min(numFrames, state->m_buffer.size());
			const auto rendered = render(state->m_buffer.data(), static_cast<f_cnt_t>(chunk), state, loop);
			std::copy_n(state->m_buffer.data(), rendered, dst);
			if (rendered < chunk) { std::fill_n(dst + rendered, chunk - rendered, SampleFrame{}); }
			dst += chunk;
			numFrames -= chunk;
			// The source ended (the window or the loop): the rest of the block is silence.
			if (rendered < chunk) { break; }
		}
		if (numFrames > 0) { std::fill_n(dst, numFrames, SampleFrame{}); }
		return true;
	}

	// TODO: These kind of playback pipelines/graphs are repeated within other parts of the codebase that work with
	// audio samples. We should find a way to unify this but the right abstraction is not so clear yet.
	while (numFrames > 0)
	{
		if (state->m_bufferView.empty())
		{
			const auto rendered = render(state->m_buffer.data(), state->m_buffer.size(), state, loop);
			state->m_bufferView = {state->m_buffer.data(), rendered};
		}
 
		const auto [inputFramesUsed, outputFramesGenerated] = state->m_resampler.process(
			{&state->m_bufferView.data()[0][0], 2, state->m_bufferView.size()}, {&dst[0][0], 2, numFrames});

		if (inputFramesUsed == 0 && outputFramesGenerated == 0)
		{
			std::fill_n(dst, numFrames, SampleFrame{});
			break;
		}

		state->m_bufferView = state->m_bufferView.subspan(inputFramesUsed);
		dst += outputFramesGenerated;
		numFrames -= outputFramesGenerated;
	}

	return numFrames < Engine::audioEngine()->framesPerPeriod();
}

f_cnt_t Sample::render(SampleFrame* dst, f_cnt_t size, PlaybackState* state, Loop loop) const
{
	for (f_cnt_t frame = 0; frame < size; ++frame)
	{
		switch (loop)
		{
		case Loop::Off:
			if (state->m_frameIndex < 0 || state->m_frameIndex >= m_endFrame) { return frame; }
			break;
		case Loop::On:
			if (state->m_frameIndex < m_loopStartFrame && state->m_backwards)
			{
				state->m_frameIndex = m_loopEndFrame - 1;
			}
			else if (state->m_frameIndex >= m_loopEndFrame) { state->m_frameIndex = m_loopStartFrame; }
			break;
		case Loop::PingPong:
			if (state->m_frameIndex < m_loopStartFrame && state->m_backwards)
			{
				state->m_frameIndex = m_loopStartFrame;
				state->m_backwards = false;
			}
			else if (state->m_frameIndex >= m_loopEndFrame)
			{
				state->m_frameIndex = m_loopEndFrame - 1;
				state->m_backwards = true;
			}
			break;
		default:
			break;
		}

		const auto value
			= m_buffer->data()[m_reversed ? m_buffer->size() - state->m_frameIndex - 1 : state->m_frameIndex]
			* m_amplification;
		dst[frame] = value;
		state->m_backwards ? --state->m_frameIndex : ++state->m_frameIndex;
	}

	return size;
}

auto Sample::sampleDuration() const -> std::chrono::milliseconds
{
	const auto numFrames = endFrame() - startFrame();
	const auto duration = numFrames / static_cast<float>(m_buffer->sampleRate()) * 1000;
	return std::chrono::milliseconds{static_cast<int>(duration)};
}

void Sample::setAllPointFrames(int startFrame, int endFrame, int loopStartFrame, int loopEndFrame)
{
	setStartFrame(startFrame);
	setEndFrame(endFrame);
	setLoopStartFrame(loopStartFrame);
	setLoopEndFrame(loopEndFrame);
}

} // namespace lmms
