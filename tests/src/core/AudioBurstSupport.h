/*
 * AudioBurstSupport.h - test audio with transients: short noise bursts at given ticks
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

#ifndef LMMS_TESTS_AUDIO_BURST_SUPPORT_H
#define LMMS_TESTS_AUDIO_BURST_SUPPORT_H

#include <cmath>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"

namespace lmms::test
{

//! @a ticks of audio at the engine's rate: silence with a short decaying noise burst at each of
//! @a bursts (ticks) - transients the onset detector finds - or none.
inline std::vector<SampleFrame> burstAudio(int ticks, std::initializer_list<int> bursts)
{
	if (ticks <= 0) { throw std::invalid_argument("burstAudio requires a positive tick count"); }
	const double framesPerTick = Engine::framesPerTick();
	std::vector<SampleFrame> data(static_cast<std::size_t>(ticks * framesPerTick), SampleFrame(0.f, 0.f));
	unsigned seed = 7;
	for (const int tick : bursts)
	{
		const auto start = static_cast<std::size_t>(tick * framesPerTick);
		for (std::size_t i = 0; i < 1024 && start + i < data.size(); ++i)
		{
			seed = seed * 1103515245u + 12345u;
			const float noise = (static_cast<float>((seed >> 8) & 0xFFFF) / 32768.f - 1.f) * std::exp(-static_cast<float>(i) / 200.f);
			data[start + i] = SampleFrame(noise, noise);
		}
	}
	return data;
}

//! A sample clip at tick 0 of a new sample track in a cleared song, playing @a data.
inline SampleClip* sampleClipOf(const std::vector<SampleFrame>& data)
{
	Song* song = Engine::getSong();
	song->clearProject();
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

} // namespace lmms::test

#endif // LMMS_TESTS_AUDIO_BURST_SUPPORT_H
