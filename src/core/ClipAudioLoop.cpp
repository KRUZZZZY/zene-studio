/*
 * ClipAudioLoop.cpp - a sample clip's content looped (and rotated) inside the clip
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

#include "ClipAudioLoop.h"

#include <algorithm>
#include <cmath>

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

//! Source frames for @a ticks at the clip's rate, signed (TimePos::frames takes no negative tick).
std::int64_t framesFor(const SampleClip& clip, int ticks)
{
	const float framesPerTick = clip.clipFramesPerTick();
	const auto magnitude = static_cast<std::int64_t>(TimePos(std::abs(ticks)).frames(framesPerTick));
	return ticks < 0 ? -magnitude : magnitude;
}

} // namespace

namespace audioloop
{

QString refusal(const SampleClip& clip)
{
	if (!clip.rendersLinearly()) { return QStringLiteral("a warped or tempo-leading clip's audio cannot loop in this release"); }
	const ClipEdits& edits = clip.clipEdits();
	if (edits.transpose != 0) { return QStringLiteral("a transposed clip's audio cannot loop in this release"); }
	if (edits.hasSpeed()) { return QStringLiteral("a clip that changes speed cannot loop its audio in this release"); }
	if (clip.isRecord()) { return QStringLiteral("a clip armed to record holds no audio to loop"); }
	return QString();
}

bool engaged(const SampleClip& clip)
{
	const ClipEdits& edits = clip.clipEdits();
	if (!edits.hasAudioLoop() || !refusal(clip).isEmpty() || comped(clip)) { return false; }
	return edits.audioLoopStart + edits.audioLoopFrames <= static_cast<std::int64_t>(sampleOf(clip).sampleSize());
}

std::int64_t elapsedFrames(const SampleClip& clip, TimePos at)
{
	const int origin = clip.startPosition().getTicks() + clip.startTimeOffset().getTicks();
	// ED-021: a frame-exact edit's sub-tick lead holds the audio back, as sourceFrameAt does.
	return framesFor(clip, at.getTicks() - origin) - static_cast<std::int64_t>(clip.leadFrames());
}

std::int64_t fold(const ClipEdits& edits, std::int64_t position)
{
	const std::int64_t end = edits.audioLoopStart + edits.audioLoopFrames;
	if (edits.audioLoopFrames <= 0 || position < end) { return std::max<std::int64_t>(0, position); }
	return edits.audioLoopStart + (position - edits.audioLoopStart) % edits.audioLoopFrames;
}

SampleWindow window(const SampleClip& clip, TimePos at)
{
	const auto in = static_cast<std::int64_t>(clip.sampleWindow().sourceIn);
	const std::int64_t from = in + std::max<std::int64_t>(0, elapsedFrames(clip, at));
	const std::int64_t to = in + std::max<std::int64_t>(0, elapsedFrames(clip, clip.endPosition()));
	return {static_cast<f_cnt_t>(from), static_cast<f_cnt_t>(std::max(from, to))};
}

bool withLoop(const SampleClip& clip, int startTicks, int lengthTicks, ClipEdits* edits, QString* error)
{
	edits->audioLoopOffset = 0;  // a new loop (or none) plays from the in-point again
	if (lengthTicks <= 0)
	{
		edits->audioLoopStart = 0;
		edits->audioLoopFrames = 0;
		return true;
	}
	const std::int64_t start = static_cast<std::int64_t>(clip.sampleWindow().sourceIn) + framesFor(clip, startTicks);
	const std::int64_t frames = framesFor(clip, lengthTicks);
	const auto size = static_cast<std::int64_t>(sampleOf(clip).sampleSize());
	if (frames <= 0 || start + frames > size)
	{
		*error = QStringLiteral("a loop of %1 ticks from %2 ticks after the clip's start runs past the end of its audio "
			"(%3 source frames)").arg(lengthTicks).arg(startTicks).arg(size);
		return false;
	}
	edits->audioLoopStart = start;
	edits->audioLoopFrames = frames;
	return true;
}

} // namespace audioloop

/*! 0.5.0 ED-029 (audio): a looped clip plays a copy of its Sample (the same shared buffer: no allocation) whose loop
 *  points are the clip's loop, in Sample::Loop::On, starting from the folded frame this pass begins on. The window
 *  SampleTrack handed over is unfolded (audioloop::window), so the handle's length and its envelope start were
 *  already derived from it; only the frame the render reads first is folded here. */
void SamplePlayHandle::snapshotAudioLoop(const SampleClip* clip, const SampleWindow& window)
{
	if (!audioloop::engaged(*clip)) { return; }
	const ClipEdits& edits = clip->clipEdits();
	const auto in = static_cast<std::int64_t>(clip->sampleWindow().sourceIn);
	const std::int64_t elapsed = static_cast<std::int64_t>(window.sourceIn) - in;
	const std::int64_t first = audioloop::fold(edits, in + edits.audioLoopOffset + elapsed);
	m_loopSample.emplace(*m_sample);
	m_loopSample->setAllPointFrames(0, static_cast<int>(m_loopSample->sampleSize()), static_cast<int>(edits.audioLoopStart),
		static_cast<int>(edits.audioLoopStart + edits.audioLoopFrames));
	m_sample = &*m_loopSample;
	m_state.setFrameIndex(static_cast<int>(first));
	m_loopMode = Sample::Loop::On;
}

} // namespace lmms
