/*
 * SampleFrameEdits.cpp - frame-exact splits and trims of sample clips, and zero-crossing snap
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

#include "SampleFrameEdits.h"

#include <algorithm>
#include <cmath>

#include "ArrangementEdits.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Track.h"

namespace lmms::arrangement
{

namespace
{

//! A tick count this close to a whole tick is that tick (the map's doubles are not exact).
constexpr double TickEpsilon = 1e-6;

const Sample& sampleOf(const SampleClip& clip) { return const_cast<SampleClip&>(clip).sample(); }

//! The (fractional) timeline tick at which @a clip sounds source frame @a frame - SampleClip's linear map.
double tickOfFrame(const SampleClip& clip, f_cnt_t frame)
{
	const double origin = clip.startPosition().getTicks() + clip.startTimeOffset().getTicks();
	const double fromIn = static_cast<double>(frame) - static_cast<double>(clip.sampleWindow().sourceIn);
	return origin + (fromIn + static_cast<double>(clip.leadFrames())) / clip.clipFramesPerTick();
}

//! Where a start at a frame lands: the whole tick at or before it, and the source frames after that tick.
struct HeadPlace
{
	tick_t tick = 0;
	f_cnt_t lead = 0;
};

HeadPlace headPlace(const SampleClip& clip, f_cnt_t frame)
{
	const double at = tickOfFrame(clip, frame);
	const double framesPerTick = clip.clipFramesPerTick();
	HeadPlace place{static_cast<tick_t>(std::floor(at + TickEpsilon)), 0};
	const auto lead = std::max(0LL, std::llround((at - place.tick) * framesPerTick));
	if (lead >= std::llround(framesPerTick)) { ++place.tick; }  // rounds up onto the next tick
	else { place.lead = static_cast<f_cnt_t>(lead); }
	return place;
}

//! The first whole tick at or after the moment frame @a frame sounds.
tick_t tailTick(const SampleClip& clip, f_cnt_t frame)
{
	return static_cast<tick_t>(std::ceil(tickOfFrame(clip, frame) - TickEpsilon));
}

//! The window is written by its owner (the one-writer rule, WriteRefusalGateTest): the clip's own source
//! and the window to play of it, in one SampleClip call. Auto-resize goes off first, so the owner call's
//! length update leaves the length set here alone.
void authorWindow(SampleClip* clip, const SampleWindow& window)
{
	clip->setAutoResize(false);
	clip->replaceSample(clip->sample().buffer(), window);
}

void applyHead(SampleClip* clip, f_cnt_t frame, const HeadPlace& head, tick_t end)
{
	const f_cnt_t out = clip->sampleWindow().sourceOut;
	clip->movePosition(TimePos(head.tick));
	clip->changeLength(TimePos(end - head.tick));
	clip->setStartTimeOffset(TimePos(0));
	authorWindow(clip, {frame, out});
	clip->setLeadFrames(head.lead);
}

void applyTail(SampleClip* clip, f_cnt_t frame, tick_t tail)
{
	clip->changeLength(TimePos(tail - clip->startPosition().getTicks()));
	authorWindow(clip, {clip->sampleWindow().sourceIn, frame});
}

void say(QString* why, const QString& text)
{
	if (why != nullptr) { *why = text; }
}

//! Whether a split at @a frame leaves audio on both sides, inside the clip's ticks.
bool splitFits(const SampleClip& clip, f_cnt_t frame, QString* why)
{
	const SampleWindow window = clip.sampleWindow();
	if (frame <= window.sourceIn || frame >= window.sourceOut)
	{
		say(why, QStringLiteral("frame %1 is not inside the audio the clip plays (frames %2..%3)")
			.arg(frame).arg(window.sourceIn).arg(window.sourceOut));
		return false;
	}
	const double at = tickOfFrame(clip, frame);
	if (at <= clip.startPosition().getTicks() || at >= clip.endPosition().getTicks())
	{
		say(why, QStringLiteral("frame %1 sounds at tick %2, not inside the clip (%3..%4)").arg(frame).arg(at, 0, 'f', 2)
			.arg(clip.startPosition().getTicks()).arg(clip.endPosition().getTicks()));
		return false;
	}
	return true;
}

//! The clip's channels summed at playback frame @a frame (a reversed source plays its buffer backwards).
float summedAt(const Sample& sample, f_cnt_t frame)
{
	const auto size = static_cast<f_cnt_t>(sample.sampleSize());
	const SampleFrame& value = sample.data()[sample.reversed() ? size - 1 - frame : frame];
	return value[0] + value[1];
}

//! Whether the audio crosses zero between frames @a frame - 1 and @a frame (or is silent at @a frame).
bool crossesAt(const Sample& sample, f_cnt_t frame)
{
	if (frame < 1 || frame >= static_cast<f_cnt_t>(sample.sampleSize())) { return false; }
	const float before = summedAt(sample, frame - 1);
	const float here = summedAt(sample, frame);
	return here == 0.0f || (before < 0.0f) != (here < 0.0f);
}

//! Of the two frames around a crossing at @a frame, the one closer to zero.
f_cnt_t quieterSide(const Sample& sample, f_cnt_t frame)
{
	return std::abs(summedAt(sample, frame - 1)) < std::abs(summedAt(sample, frame)) ? frame - 1 : frame;
}

} // namespace

bool canSplitAtFrame(const SampleClip& clip, f_cnt_t frame, QString* why)
{
	return frameEditable(clip, why) && splitFits(clip, frame, why);
}

bool frameEditable(const SampleClip& clip, QString* why)
{
	if (!clip.rendersLinearly())
	{
		say(why, QStringLiteral("frame-exact edits need a clip without warp markers or a source tempo: its frames do not "
			"map to ticks in a straight line"));
		return false;
	}
	if (sampleOf(clip).sampleSize() < 2 || clip.clipFramesPerTick() <= 0.0f)
	{
		say(why, QStringLiteral("the clip holds no audio to cut at a frame"));
		return false;
	}
	return true;
}

f_cnt_t frameAtTimeline(const SampleClip& clip, tick_t tick, f_cnt_t outputFrames)
{
	const double outputPerTick = Engine::framesPerTick(Engine::audioEngine()->outputSampleRate());
	const double at = tick + (outputPerTick > 0.0 ? static_cast<double>(outputFrames) / outputPerTick : 0.0);
	const double origin = clip.startPosition().getTicks() + clip.startTimeOffset().getTicks();
	const double frame = static_cast<double>(clip.sampleWindow().sourceIn) + (at - origin) * clip.clipFramesPerTick()
		- static_cast<double>(clip.leadFrames());
	const auto size = static_cast<long long>(sampleOf(clip).sampleSize());
	return static_cast<f_cnt_t>(std::clamp(std::llround(frame), 0LL, size));
}

f_cnt_t nearestZeroCrossing(SampleClip& clip, f_cnt_t frame)
{
	const Sample& sample = clip.sample();
	const auto window = static_cast<f_cnt_t>(sample.sampleRate()) * ZeroCrossingWindowMs / 1000;
	for (f_cnt_t distance = 0; distance <= window; ++distance)
	{
		if (frame >= distance && crossesAt(sample, frame - distance)) { return quieterSide(sample, frame - distance); }
		if (crossesAt(sample, frame + distance)) { return quieterSide(sample, frame + distance); }
	}
	return frame;
}

SampleClip* splitAtFrame(SampleClip* clip, f_cnt_t frame, QString* why)
{
	if (!canSplitAtFrame(*clip, frame, why)) { return nullptr; }
	// Both halves are placed from the clip's map BEFORE either changes: the clone's own copy constructor
	// resets its offset, so it cannot be asked.
	const HeadPlace head = headPlace(*clip, frame);
	const tick_t tail = tailTick(*clip, frame);
	const tick_t end = clip->endPosition().getTicks();
	Track* track = clip->getTrack();
	track->saveJournallingState(false);
	auto* right = dynamic_cast<SampleClip*>(clip->clone());
	applyTail(clip, frame, tail);
	applyHead(right, frame, head, end);
	track->restoreJournallingState();
	return right;
}

bool trimHeadToFrame(SampleClip* clip, f_cnt_t frame, QString* why, bool checkOnly)
{
	if (!frameEditable(*clip, why)) { return false; }
	const HeadPlace head = headPlace(*clip, frame);
	const tick_t end = clip->endPosition().getTicks();
	if (frame >= clip->sampleWindow().sourceOut || head.tick < 0 || head.tick >= end)
	{
		say(why, QStringLiteral("a start at frame %1 would not be inside the clip's audio and before its end (tick %2)")
			.arg(frame).arg(end));
		return false;
	}
	if (!checkOnly) { applyHead(clip, frame, head, end); }
	return true;
}

bool trimTailToFrame(SampleClip* clip, f_cnt_t frame, QString* why, bool checkOnly)
{
	if (!frameEditable(*clip, why)) { return false; }
	const tick_t tail = tailTick(*clip, frame);
	if (frame <= clip->sampleWindow().sourceIn || tail <= clip->startPosition().getTicks())
	{
		say(why, QStringLiteral("an end at frame %1 would not be after the clip's start").arg(frame));
		return false;
	}
	if (!checkOnly) { applyTail(clip, std::min(frame, static_cast<f_cnt_t>(clip->sample().sampleSize())), tail); }
	return true;
}

bool splitAtZeroCrossing(Clip* clip, tick_t position)
{
	auto* sample = dynamic_cast<SampleClip*>(clip);
	if (!modes().snapToZeroCrossings || sample == nullptr || !frameEditable(*sample, nullptr)) { return false; }
	const f_cnt_t frame = nearestZeroCrossing(*sample, frameAtTimeline(*sample, position, 0));
	if (!canSplitAtFrame(*sample, frame, nullptr)) { return false; }
	clip->getTrack()->addJournalCheckPoint();
	return splitAtFrame(sample, frame, nullptr) != nullptr;
}

void snapEdgeToZeroCrossing(Clip* clip, bool resized, bool head)
{
	auto* sample = dynamic_cast<SampleClip*>(clip);
	if (!resized || !modes().snapToZeroCrossings || sample == nullptr || !frameEditable(*sample, nullptr)) { return; }
	const tick_t edge = head ? clip->startPosition().getTicks() : clip->endPosition().getTicks();
	const f_cnt_t frame = nearestZeroCrossing(*sample, frameAtTimeline(*sample, edge, 0));
	if (head) { trimHeadToFrame(sample, frame, nullptr); }
	else { trimTailToFrame(sample, frame, nullptr); }
}

} // namespace lmms::arrangement
