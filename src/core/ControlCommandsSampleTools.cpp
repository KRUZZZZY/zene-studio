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
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
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

//! Copy source frames [@a from, @a to) of @a clip into a fresh buffer and swap it in, one checkpoint.
void swapIn(SampleClip* clip, f_cnt_t from, f_cnt_t to)
{
	const SampleFrame* data = clip->sample().data();
	std::vector<SampleFrame> frames(data + from, data + to);
	const int rate = static_cast<int>(clip->sample().sampleRate());
	clip->addJournalCheckPoint();
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(std::move(frames), rate));
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
	swapIn(clip, window.sourceIn, window.sourceOut);
	clip->setSampleWindow(SampleWindow::full(window.sourceOut - window.sourceIn));
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
	swapIn(clip, 0, static_cast<f_cnt_t>(clip->sample().sampleSize()));
	clip->setSampleWindow(window);
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("frames"), static_cast<double>(clip->sample().sampleSize())}});
}

ControlResult fadeCurve(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const ClipEdits& edits = ref.clip->clipEdits();
	const auto length = static_cast<f_cnt_t>(std::max<tick_t>(1, ref.clip->length().getTicks()));
	const int points = std::clamp(args.value(QStringLiteral("points")).toInt(64), 2, 1024);
	QJsonArray curve;
	for (int i = 0; i < points; ++i)
	{
		const double tick = static_cast<double>(length) * i / (points - 1);
		const float fade = clipFadeGainAt(static_cast<f_cnt_t>(tick), length, edits.fadeInTicks, edits.fadeOutTicks, edits.fadeInShape, edits.fadeOutShape);
		curve.append(QJsonObject{{QStringLiteral("tick"), tick}, {QStringLiteral("gain"), static_cast<double>(edits.gain * fade * envelopeGainAt(edits, tick))}});
	}
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("curve"), curve}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, bool mutating,
	ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.id = QLatin1String(id);
	const int dot = cmd.id.indexOf(QLatin1Char('.'));
	cmd.group = cmd.id.left(dot);
	cmd.verb = cmd.id.mid(dot + 1);
	cmd.description = description;
	cmd.argsSchema = objectSchema(args, QJsonArray{QStringLiteral("clip")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
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
	QJsonObject curve = clip;
	curve.insert(QStringLiteral("points"), integerProperty(2, 1024));
	add(registry, "clip.fade_curve", QStringLiteral("A clip's fades, gain envelope and gain as one gain curve, 'points' samples over "
		"its length (64): [{tick, gain}] - what the automation editor draws behind the clip. Read only."), curve, false, fadeCurve);
}

} // namespace lmms
