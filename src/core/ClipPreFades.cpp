/*
 * ClipPreFades.cpp - an audio clip that sounds before its start and after its end (0.5.0 ED-179)
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
#include "ClipPreFades.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "AudioEngine.h"
#include "ClipAudioLoop.h"
#include "ClipEdits.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "TakeLane.h"
#include "Track.h"

namespace lmms
{

namespace
{

//! The clip's Sample, read only (SampleClip has no const accessor).
const Sample& sampleOf(const SampleClip& clip) { return const_cast<SampleClip&>(clip).sample(); }

bool comped(const SampleClip& clip)
{
	const Track* owner = clip.getTrack();
	return owner != nullptr && (!owner->takeLanes().segments().empty() || owner->takeLanes().auditionLane() >= 0);
}

std::int64_t framesAt(double ticks, double framesPerTick) { return static_cast<std::int64_t>(std::llround(ticks * framesPerTick)); }

//! The source frame under the clip's end, where its hidden tail begins.
std::int64_t endFrame(const SampleClip& clip, double framesPerTick)
{
	const int origin = clip.startPosition().getTicks() + clip.startTimeOffset().getTicks();
	return static_cast<std::int64_t>(clip.sampleWindow().sourceIn) + framesAt(clip.endPosition().getTicks() - origin, framesPerTick);
}

//! leadTicks' mirror: the pre-fade-out, no longer than the source frames after the clip's end, so the ramp
//! reaches silence before the audio runs out (a clip longer than its audio has no hidden tail).
int tailTicks(const SampleClip& clip)
{
	const double framesPerTick = clip.clipFramesPerTick();
	if (framesPerTick <= 0.0) { return 0; }
	const std::int64_t atEnd = endFrame(clip, framesPerTick);
	if (atEnd > static_cast<std::int64_t>(clip.sampleWindow().sourceOut)) { return 0; }
	const auto hidden = static_cast<double>(static_cast<std::int64_t>(sampleOf(clip).sampleSize()) - atEnd);
	return std::clamp(clip.clipEdits().preFadeOutTicks, 0, std::max(0, static_cast<int>(std::floor(hidden / framesPerTick))));
}

} // namespace

namespace prefades
{

bool engaged(const SampleClip& clip)
{
	const ClipEdits& edits = clip.clipEdits();
	return edits.hasPreFade() && edits.fadeInOffsetTicks == 0 && edits.fadeOutOffsetTicks == 0 && clip.rendersLinearly() && edits.transpose == 0 && !edits.hasSpeed() && !clip.isRecord()
		&& !sampleOf(clip).reversed() && !comped(clip) && !audioloop::engaged(clip);  // ED-029: a looped clip plays its loop
}

int leadTicks(const SampleClip& clip)
{
	const double framesPerTick = clip.clipFramesPerTick();
	if (framesPerTick <= 0.0) { return 0; }
	const auto available = static_cast<int>(std::floor(static_cast<double>(clip.sampleWindow().sourceIn) / framesPerTick));
	return std::clamp(clip.clipEdits().preFadeInTicks, 0, available);
}

TimePos spanStart(const SampleClip& clip)
{
	return engaged(clip) ? TimePos(clip.startPosition().getTicks() - leadTicks(clip)) : clip.startPosition();
}

TimePos spanEnd(const SampleClip& clip)
{
	return engaged(clip) ? TimePos(clip.endPosition().getTicks() + tailTicks(clip)) : clip.endPosition();
}

SampleWindow window(const SampleClip& clip, TimePos at)
{
	if (!engaged(clip)) { return {clip.sourceFrameAt(at), clip.sourceFrameAt(clip.endPosition())}; }
	const double framesPerTick = clip.clipFramesPerTick();
	const SampleWindow own = clip.sampleWindow();
	const int origin = clip.startPosition().getTicks() + clip.startTimeOffset().getTicks();
	const auto size = static_cast<std::int64_t>(sampleOf(clip).sampleSize());
	const auto in = static_cast<std::int64_t>(own.sourceIn);
	const auto out = static_cast<std::int64_t>(own.sourceOut);
	const std::int64_t from = in + framesAt(at.getTicks() - origin, framesPerTick);
	// The hidden tail is the audio under the clip's end and after it; a clip longer than its audio has none.
	const std::int64_t atEnd = endFrame(clip, framesPerTick);
	const std::int64_t to = atEnd <= out ? atEnd + framesAt(tailTicks(clip), framesPerTick) : out;
	return {static_cast<f_cnt_t>(std::clamp<std::int64_t>(from, 0, size)), static_cast<f_cnt_t>(std::clamp<std::int64_t>(to, 0, size))};
}

} // namespace prefades

void SamplePlayHandle::snapshotPreFades(const SampleClip* clip, const SampleWindow& window)
{
	if (!prefades::engaged(*clip) || m_clipFrames == 0) { return; }
	// The ramps as scheduled - the pre-fades clamped to the hidden audio there is - so each starts (or ends) silent.
	m_preFadeSpan = {static_cast<std::int64_t>(m_clipFrames), framesAt(prefades::leadTicks(*clip), m_outputFramesPerTick),
		framesAt(tailTicks(*clip), m_outputFramesPerTick), static_cast<std::int64_t>(m_fadeInFrames),
		static_cast<std::int64_t>(m_fadeOutFrames)};
	const SampleWindow own = clip->sampleWindow();
	const double ratio = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / static_cast<double>(m_sample->sampleRate());
	m_preRollFrames = window.sourceIn < own.sourceIn ? framesAt(static_cast<double>(own.sourceIn - window.sourceIn), ratio) : 0;
	// A copy of the clip's Sample (the same shared buffer: no allocation) whose window reaches past the clip's own.
	m_preFadeSample.emplace(*m_sample);
	m_preFadeSample->setStartFrame(static_cast<int>(window.sourceIn));
	m_preFadeSample->setEndFrame(static_cast<int>(window.sourceOut));
	m_sample = &*m_preFadeSample;
	m_preFaded = true;
}

void SamplePlayHandle::applyPreFaded(SampleFrame* buffer, f_cnt_t frames) const
{
	const std::int64_t base = static_cast<std::int64_t>(m_envelopeStart + m_frame) - m_preRollFrames;
	const double perTick = std::max(1e-9, m_outputFramesPerTick);
	const float from = envelopeGainAt(m_edits, static_cast<double>(base) / perTick);
	const float to = envelopeGainAt(m_edits, static_cast<double>(base + static_cast<std::int64_t>(frames)) / perTick);
	const float step = frames > 0 ? (to - from) / static_cast<float>(frames) : 0.f;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const std::int64_t at = base + static_cast<std::int64_t>(f);
		const float fade = preFadedGainAt(m_preFadeSpan, at, m_edits.fadeInShape, m_edits.fadeOutShape);
		const float gain = m_edits.gain * fade * (from + step * static_cast<float>(f));
		buffer[f][0] *= gain;
		buffer[f][1] *= gain;
	}
}

} // namespace lmms
