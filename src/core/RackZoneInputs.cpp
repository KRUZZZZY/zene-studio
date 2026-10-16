/*
 * RackZoneInputs.cpp - the notes a rack's key/velocity zones route: one input block per chain, and the voices
 *                      a zone's sample plays through
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
 */

#include "RackZoneInputs.h"

#include <algorithm>

#include "SampleBuffer.h"

namespace lmms
{

void RackZoneVoice::restart()
{
	position = 0.0;
	lastFramesPlayed = 0;
	fade = 1.0f;
	ended = false;
}

bool RackZoneVoice::render(const SampleBuffer& sample, SampleFrame* dst, f_cnt_t frames, double step, float gain,
	bool released)
{
	const SampleFrame* data = sample.data();
	const std::size_t size = sample.size();
	for (f_cnt_t f = 0; f < frames && !ended; ++f)
	{
		const auto index = static_cast<std::size_t>(position);
		if (index >= size) { ended = true; break; }
		// The last source frame is held: its interpolation partner is itself, so a one-frame sample is
		// not silence, and a fractional step keeps the frame for the rest of its duration. The voice ends
		// only once the position has passed the end of the sample.
		const std::size_t next = index + 1 < size ? index + 1 : index;
		const auto fraction = static_cast<float>(position - static_cast<double>(index));
		if (released) { fade -= 1.0f / static_cast<float>(FadeFrames); }
		if (fade <= 0.0f) { ended = true; break; }
		dst[f] += (data[index] * (1.0f - fraction) + data[next] * fraction) * (gain * fade);
		position += step;
		if (position >= static_cast<double>(size)) { ended = true; }
	}
	return !ended;
}

RackZoneInputs::RackZoneInputs() = default;

RackZoneInputs::~RackZoneInputs() = default;

void RackZoneInputs::prepare(const std::vector<int>& routedChains, int chainCount, f_cnt_t frames,
	sample_rate_t outputRate)
{
	clear();
	if (frames <= 0 || chainCount <= 0) { return; }
	m_blocks.resize(static_cast<std::size_t>(chainCount));
	for (const int chain : routedChains)
	{
		if (chain < 0 || chain >= chainCount) { continue; }
		auto block = std::make_unique<Block>();
		block->frames.assign(static_cast<std::size_t>(frames), SampleFrame{});
		m_blocks[static_cast<std::size_t>(chain)] = std::move(block);
	}
	m_outputRate = outputRate > 0 ? outputRate : m_outputRate;
	m_frames = frames;
}

void RackZoneInputs::clear()
{
	m_blocks.clear();
	m_frames = 0;
	for (RackZoneVoice& voice : m_voices) { voice.owner.store(nullptr, std::memory_order_relaxed); }
}

auto RackZoneInputs::routes(int chain) const -> bool
{
	return chain >= 0 && chain < static_cast<int>(m_blocks.size()) && m_blocks[static_cast<std::size_t>(chain)] != nullptr;
}

void RackZoneInputs::deliver(int chain, const SampleFrame* block)
{
	if (!routes(chain) || block == nullptr) { return; }
	Block& target = *m_blocks[static_cast<std::size_t>(chain)];
	// Held for one block add: several instrument tracks may feed this rack from different workers.
	while (m_lock.test_and_set(std::memory_order_acquire)) {}
	for (f_cnt_t f = 0; f < m_frames; ++f) { target.frames[static_cast<std::size_t>(f)] += block[f]; }
	target.live.store(true, std::memory_order_relaxed);
	m_lock.clear(std::memory_order_release);
}

auto RackZoneInputs::voiceFor(const RackZoneNote& note) -> RackZoneVoice*
{
	for (RackZoneVoice& voice : m_voices)
	{
		if (voice.owner.load(std::memory_order_acquire) != note.id) { continue; }
		// The handle pointer alone cannot tell a continuing note from a pooled handle reused for a new note:
		// the reused handle keeps the same pointer, and tempo resize rescales and truncates its frame count,
		// so a continuing note can report a smaller frame count next period. The generation the handle carries
		// on construction and on every reuse is the stable identity: a changed generation is a new note and
		// restarts the voice; an unchanged one resumes where the voice left off.
		if (note.generation != voice.lastGeneration)
		{
			voice.restart();
			voice.lastGeneration = note.generation;
		}
		return &voice;
	}
	for (RackZoneVoice& voice : m_voices)
	{
		const void* expected = nullptr;
		if (voice.owner.compare_exchange_strong(expected, note.id, std::memory_order_acq_rel))
		{
			voice.restart();
			voice.lastGeneration = note.generation;
			return &voice;
		}
	}
	return nullptr;
}

auto RackZoneInputs::playVoice(const RackZoneNote& note, const SampleBuffer& sample, SampleFrame* dst) -> bool
{
	if (!active() || note.id == nullptr || sample.empty()) { return false; }
	RackZoneVoice* voice = voiceFor(note);
	if (voice == nullptr) { return false; }
	voice->touched.store(m_period.load(std::memory_order_relaxed), std::memory_order_relaxed);
	voice->lastFramesPlayed = note.framesPlayed;
	const f_cnt_t start = std::clamp<f_cnt_t>(note.startOffset, 0, m_frames);
	const double step = note.pitch * static_cast<double>(sample.sampleRate()) / static_cast<double>(m_outputRate);
	voice->render(sample, dst + start, m_frames - start, step, note.gain, note.released);
	return true;
}

auto RackZoneInputs::liveBlock(int chain) const -> const SampleFrame*
{
	if (!routes(chain)) { return nullptr; }
	const Block& block = *m_blocks[static_cast<std::size_t>(chain)];
	return block.live.load(std::memory_order_relaxed) ? block.frames.data() : nullptr;
}

void RackZoneInputs::endPeriod()
{
	for (const std::unique_ptr<Block>& block : m_blocks)
	{
		if (block == nullptr || !block->live.load(std::memory_order_relaxed)) { continue; }
		std::fill(block->frames.begin(), block->frames.end(), SampleFrame{});
		block->live.store(false, std::memory_order_relaxed);
	}
	// A voice whose note did not play this period has lost its note: free it for the next one.
	const unsigned period = m_period.load(std::memory_order_relaxed);
	for (RackZoneVoice& voice : m_voices)
	{
		if (voice.touched.load(std::memory_order_relaxed) != period) { voice.owner.store(nullptr, std::memory_order_release); }
	}
	m_period.store(period + 1, std::memory_order_relaxed);
}

auto RackZoneInputs::busyVoices() const -> int
{
	return static_cast<int>(std::count_if(m_voices.begin(), m_voices.end(),
		[](const RackZoneVoice& voice) { return voice.owner.load(std::memory_order_relaxed) != nullptr; }));
}

} // namespace lmms
