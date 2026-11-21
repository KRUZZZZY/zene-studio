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
	 *  At ratio 1.0 the OUTPUT is the source's own frames, so the delay cannot
	 *  enter the render. The converter is still DRIVEN over every frame this call
	 *  consumes, exactly once, in order, and only its output is discarded. That
	 *  matters for a run that passes through unity: a .5 -> 1.0 -> .5 sequence
	 *  hands the converter the same input history it would have had without the
	 *  unity segment, so the non-unity output after it is unchanged, and the
	 *  source cursor advances by exactly the frames consumed. Skipping the
	 *  converter here instead (as an earlier version of this fix did) left its
	 *  state behind the cursor and changed that following output.
	 *
	 *  Every OTHER ratio keeps the converter path below unchanged, with its exact
	 *  arithmetic and its own staging; the unity branch stages the same way. */
	if (resampleRatio == 1.0)
	{
		/*! Drive the converter EXACTLY as the non-unity loop below does - same staging, same
		 *  `numFrames -= generated` accounting, same input view - and overwrite the frames it
		 *  produced with the source frames it just consumed. The converter therefore sees the
		 *  same call sequence and ends in the same state as it would have without this branch,
		 *  which is what keeps a following non-unity call bit-identical to the historical
		 *  behaviour; only the samples written during unity differ (the source, not its
		 *  one-sample-delayed interpolation). */
		while (numFrames > 0)
		{
			if (state->m_bufferView.empty())
			{
				const auto rendered = render(state->m_buffer.data(), state->m_buffer.size(), state, loop);
				state->m_bufferView = {state->m_buffer.data(), rendered};
				if (rendered <= 0)
				{
					// The window or the loop ended: the rest of this block is silence.
					std::fill_n(dst, numFrames, SampleFrame{});
					break;
				}
			}

			SampleFrame* const produced = dst;
			const auto staged = state->m_bufferView.size();
			const auto [inputFramesUsed, outputFramesGenerated] = state->m_resampler.process(
				{&state->m_bufferView.data()[0][0], 2, state->m_bufferView.size()},
				{&dst[0][0], 2, numFrames});

			if (inputFramesUsed == 0 && outputFramesGenerated == 0)
			{
				std::fill_n(dst, numFrames, SampleFrame{});
				break;
			}

			// Replace what the converter interpolated with the source frames it consumed, so
			// the unity output is the source itself (no leading duplicate). `produced` is where
			// those frames were written, so they stay at the same output positions.
			const auto replace = std::min<std::size_t>(
				static_cast<std::size_t>(outputFramesGenerated),
				static_cast<std::size_t>(inputFramesUsed > 0 ? inputFramesUsed : 0));
			std::copy_n(state->m_bufferView.data(), replace, produced);

			state->m_bufferView = state->m_bufferView.subspan(inputFramesUsed);
			dst += outputFramesGenerated;
			numFrames -= outputFramesGenerated;
			if (inputFramesUsed < static_cast<f_cnt_t>(staged) && outputFramesGenerated == 0) { break; }
		}

		return numFrames < Engine::audioEngine()->framesPerPeriod();
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
