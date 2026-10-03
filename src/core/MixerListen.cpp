/*
 * MixerListen.cpp - the solo-to-listen bus: AFL / PFL of mixer channels on the master output
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

#include "MixerListen.h"

#include <array>
#include <atomic>

#include "Mixer.h"
#include "SampleFrame.h"
#include "ValueBuffer.h"

namespace lmms::mixerlisten
{

namespace
{

constexpr auto Relaxed = std::memory_order_relaxed;

std::array<std::atomic<bool>, MaxChannels> s_listening{};
std::atomic<int> s_count{0};
std::atomic<int> s_mode{static_cast<int>(Mode::AfterFader)};

//! Add @a channel's listened signal to @a out. Its buffer is its post-insert, pre-fader signal, which AFL
//! takes through the fader (sample-exact where the fader is) - unless post-fader inserts baked the fader
//! into it (MX-082): AFL then takes it at unity, and PFL the pre-fader copy the inserts kept.
void addListened(SampleFrame* out, MixerChannel& channel, bool afterFader, f_cnt_t frames)
{
	const SampleFrame* src = !afterFader && channel.m_postInserts.baked() ? channel.m_postInserts.preFader() : channel.m_buffer;
	const ValueBuffer* volBuf = afterFader ? postinserts::faderBuffer(channel, nullptr) : nullptr;
	const float gain = afterFader ? postinserts::faderValue(channel, nullptr) : 1.f;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const float g = volBuf != nullptr ? volBuf->values()[f] : gain;
		out[f][0] += src[f][0] * g;
		out[f][1] += src[f][1] * g;
	}
}

} // namespace

void setListening(int channel, bool on)
{
	if (channel < 0 || channel >= MaxChannels) { return; }
	const bool was = s_listening[static_cast<std::size_t>(channel)].exchange(on, Relaxed);
	if (was != on) { s_count.fetch_add(on ? 1 : -1, Relaxed); }
}

bool listening(int channel)
{
	return channel >= 0 && channel < MaxChannels && s_listening[static_cast<std::size_t>(channel)].load(Relaxed);
}

bool anyListening() { return s_count.load(Relaxed) > 0; }

QVector<int> listeningChannels()
{
	QVector<int> out;
	for (int i = 0; i < MaxChannels; ++i)
	{
		if (listening(i)) { out.append(i); }
	}
	return out;
}

void clear()
{
	for (auto& flag : s_listening) { flag.store(false, Relaxed); }
	s_count.store(0, Relaxed);
}

void setMode(Mode mode) { s_mode.store(static_cast<int>(mode), Relaxed); }

Mode mode() { return static_cast<Mode>(s_mode.load(Relaxed)); }

void replaceOutput(SampleFrame* out, Mixer& mixer, f_cnt_t frames)
{
	if (!anyListening()) { return; }
	const bool afterFader = mode() == Mode::AfterFader;
	for (f_cnt_t f = 0; f < frames; ++f) { out[f] = SampleFrame(0.f, 0.f); }
	const int channels = std::min<int>(mixer.numChannels(), MaxChannels);
	for (int i = 0; i < channels; ++i)
	{
		if (!s_listening[static_cast<std::size_t>(i)].load(Relaxed)) { continue; }
		MixerChannel* channel = mixer.mixerChannel(i);
		// A muted channel is not processed, so its buffer still holds its last block: it is heard as silence.
		if (channel->m_muted.load(Relaxed)) { continue; }
		addListened(out, *channel, afterFader, frames);
	}
}

} // namespace lmms::mixerlisten
