/*
 * ControlCommandsSampleTools.cpp - crop a sample, make it unique, read a clip's fade curve
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
/*! 0.5.0 L05 (W3), a sample clip's source and shape:
 *  - sample.crop (ED-155): the frames outside the clip's window are discarded - a fresh buffer of just
 *    the window replaces the source, so the clip sounds the same and stores only what it plays;
 *  - sample.make_unique (ED-155): the clip gets its own copy of its source, so a later bake of a
 *    duplicate sharing it cannot reach this one;
 *  - clip.fade_curve (ED-180): the clip's fades, gain envelope and gain as one sampled gain curve - the
 *    shape the automation editor draws behind it.
 *  A new buffer is always a copy: a buffer another clip shares is never written. */

#include <algorithm>
#include <cmath>
#include <vector>

#include <QJsonArray>

#include "ClipEdits.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "WarpMarkers.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

SampleClip* sampleClipOf(const QJsonObject& args, ClipRef* ref, ControlResult* error)
{
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), ref, error)) { return nullptr; }
	auto* clip = dynamic_cast<SampleClip*>(ref->clip);
	if (clip == nullptr) { *error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clip' is an audio clip")); }
	else if (clip->sample().data() == nullptr) { *error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip has no audio")); return nullptr; }
	return clip;
}

//! Copy source frames [@a from, @a to) of @a clip into a fresh buffer and swap it in, playing @a window of it, one checkpoint.
void swapIn(SampleClip* clip, f_cnt_t from, f_cnt_t to, const SampleWindow& window)
{
	const SampleFrame* data = clip->sample().data();
	std::vector<SampleFrame> frames(data + from, data + to);
	const int rate = static_cast<int>(clip->sample().sampleRate());
	const int channels = clip->sample().channels();
	clip->addJournalCheckPoint();
	clip->replaceSample(std::make_shared<SampleBuffer>(std::move(frames), rate, QString(), channels), window);  // the window written by its owner
	Engine::getSong()->setModified();
}

ControlResult crop(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = sampleClipOf(args, &ref, &error);
	if (clip == nullptr) { return error; }
	const SampleWindow window = clip->sampleWindow();
	const auto size = static_cast<f_cnt_t>(clip->sample().sampleSize());
	if (window.sourceIn <= 0 && window.sourceOut >= size) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip plays its whole source: nothing to crop")); }
	// Warp markers pin frames of the old source: a crop from inside it, or past one of them, would leave them on the wrong audio.
	const WarpMarkers& markers = clip->warpMarkers();
	if (!markers.empty() && (window.sourceIn > 0 || markers[markers.size() - 1].sourceFrame > window.sourceOut))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip's warp markers pin frames this crop would move or drop "
			"(warp.remove them first)"));
	}
	swapIn(clip, window.sourceIn, window.sourceOut, SampleWindow::full(window.sourceOut - window.sourceIn));
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("frames"), static_cast<double>(window.sourceOut - window.sourceIn)},
		{QStringLiteral("discarded"), static_cast<double>(size - (window.sourceOut - window.sourceIn))}});
}

ControlResult makeUnique(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = sampleClipOf(args, &ref, &error);
	if (clip == nullptr) { return error; }
	const SampleWindow window = clip->sampleWindow();
	swapIn(clip, 0, static_cast<f_cnt_t>(clip->sample().sampleSize()), window);
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("frames"), static_cast<double>(clip->sample().sampleSize())}});
}

ControlResult fadeCurve(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const ClipEdits& edits = ref.clip->clipEdits();
	const tick_t length = std::max<tick_t>(1, ref.clip->length().getTicks());
	const int points = std::clamp(args.value(QStringLiteral("points")).toInt(64), 2, 1024);
	QJsonArray curve;
	for (int i = 0; i < points; ++i)
	{
		const double tick = static_cast<double>(length) * i / (points - 1);
		curve.append(QJsonObject{{QStringLiteral("tick"), tick}, {QStringLiteral("gain"), static_cast<double>(clipGainCurveAt(edits, length, tick))}});
	}
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("curve"), curve}});
}

//! ED-152 / ED-179: a sample clip's speed ramp and its tape-start and tape-stop speed fades.
ControlResult speed(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	if (clip == nullptr) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clip' is an audio clip")); }
	if (clip->warpStretchMode() != WarpStretchMode::Resample)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a clip that keeps its pitch while stretched cannot change speed: set its stretch mode to resample"));
	}
	const ClipEdits before = clip->clipEdits();
	ClipEdits after = before;
	after.speedFrom = std::clamp(args.value(QStringLiteral("from")).toInt(after.speedFrom), 25, 400);
	after.speedTo = std::clamp(args.value(QStringLiteral("to")).toInt(args.contains(QStringLiteral("from")) ? after.speedFrom : after.speedTo), 25, 400);
	after.speedInTicks = std::clamp(args.value(QStringLiteral("fade_in")).toInt(after.speedInTicks), 0, clip->length().getTicks());
	after.speedOutTicks = std::clamp(args.value(QStringLiteral("fade_out")).toInt(after.speedOutTicks), 0, clip->length().getTicks());
	if (after.speedFrom != before.speedFrom || after.speedTo != before.speedTo || after.speedInTicks != before.speedInTicks ||
		after.speedOutTicks != before.speedOutTicks)
	{
		clip->addJournalCheckPoint();
		clip->setClipEdits(after);
		Engine::getSong()->setModified();
	}
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("from"), after.speedFrom}, {QStringLiteral("to"), after.speedTo},
		{QStringLiteral("fade_in"), after.speedInTicks}, {QStringLiteral("fade_out"), after.speedOutTicks}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, bool mutating,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray{QStringLiteral("clip")}), mutating, handler);
}

} // namespace

void registerSampleToolCommands(ControlRegistry& registry)
{
	const QJsonObject clip{{QStringLiteral("clip"), stringProperty()}};
	add(registry, "sample.crop", QStringLiteral("Discard the source frames outside an audio clip's window: a fresh buffer of just what "
		"the clip plays replaces its source, so it sounds the same and stores less. Never written into a buffer another clip "
		"shares. One undo step (Clip checkpoint)."), clip, true, crop);
	add(registry, "sample.make_unique", QStringLiteral("Give an audio clip its own copy of its source, so a later bake of a duplicate "
		"that shares it cannot change this one. One undo step (Clip checkpoint)."), clip, true, makeUnique);
	QJsonObject speedArgs = clip;
	for (const char* key : {"from", "to", "fade_in", "fade_out"}) { speedArgs.insert(QLatin1String(key), integerProperty()); }
	add(registry, "clip.speed", QStringLiteral("An audio clip's speed, the pitch going with it (a tape): it ramps from 'from' to 'to' "
		"percent (25..400; 'to' follows 'from' when only 'from' is given - a fixed varispeed) across the clip, rises from "
		"nothing over the first 'fade_in' ticks (a tape start) and falls to nothing over the last 'fade_out' ticks (a tape "
		"stop). The clip keeps its length; a slower clip plays less of its audio. Refused for a clip that keeps its pitch "
		"while stretched. Saved with the clip. One undo step (Clip checkpoint)."), speedArgs, true, speed);
	QJsonObject curve = clip;
	curve.insert(QStringLiteral("points"), integerProperty(2, 1024));
	add(registry, "clip.fade_curve", QStringLiteral("A clip's fades, gain envelope and gain as one gain curve, 'points' samples over "
		"its length (64): [{tick, gain}] - what the automation editor draws behind the clip. Read only."), curve, false, fadeCurve);
}

} // namespace lmms
