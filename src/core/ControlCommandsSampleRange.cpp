/*
 * ControlCommandsSampleRange.cpp - a sample clip's source edited over a range of frames
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
/*! 0.5.0 L05 (ED-156): the sample editor's operations over a selection of a sample clip's source -
 *  gain, silence, delete (the rest closes up), reverse, fade in or out, and crop to the selection. Each
 *  bakes a fresh copy of the source and swaps it in (a buffer another clip shares is never written), as
 *  one undo step; the clip's window is kept over what it played, or reset when the source changes length.
 *
 *  Q050-106 adds `mono`: the whole source downmixed with the documented (L + R) / 2 rule onto both
 *  sides, with the source layout recorded as one channel (SampleBuffer::channels) so the clip's
 *  serialized state, arrangement.get_state and one undo all report mono / stereo honestly. */

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleOperators.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

const QStringList& operations()
{
	static const QStringList names{QStringLiteral("gain"), QStringLiteral("silence"), QStringLiteral("delete"), QStringLiteral("reverse"),
		QStringLiteral("fade_in"), QStringLiteral("fade_out"), QStringLiteral("crop"), QStringLiteral("mono")};
	return names;
}

QString layoutName(int channels)
{
	return channels == 1 ? QStringLiteral("mono") : QStringLiteral("stereo");
}

/*! `mono` is the one operation that is not a range edit: it downmixes the WHOLE source with the
 *  documented per-frame mean, (L + R) / 2, on both playback sides. It keeps the source's rate,
 *  frame count and the clip's window, records the new layout on the buffer and takes one Clip
 *  checkpoint, exactly like the range bakers below. */
ControlResult bakeMono(const ClipRef& ref, SampleClip* clip, const SampleWindow& window,
	std::size_t size, int rate)
{
	if (clip->sample().channels() == 1)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is already mono").arg(clipId(ref.id)));
	}
	const auto* data = clip->sample().data();
	std::vector<SampleFrame> result = sampleops::toMono(data, static_cast<f_cnt_t>(size));
	clip->addJournalCheckPoint();
	clip->replaceSample(std::make_shared<SampleBuffer>(std::move(result), rate, QString(), 1), window);
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)},
		{QStringLiteral("op"), QStringLiteral("mono")},
		{QStringLiteral("frames"), static_cast<double>(size)},
		{QStringLiteral("sample_rate"), rate},
		{QStringLiteral("channels"), 1},
		{QStringLiteral("layout"), layoutName(1)},
		{QStringLiteral("window"), QJsonArray{static_cast<double>(window.sourceIn),
			static_cast<double>(window.sourceOut)}}});
}

//! @a frames with operation @a op applied over [@a from, @a to).
std::vector<SampleFrame> applied(std::vector<SampleFrame> frames, const QString& op, std::size_t from, std::size_t to, float gain)
{
	const auto begin = frames.begin() + static_cast<std::ptrdiff_t>(from);
	const auto end = frames.begin() + static_cast<std::ptrdiff_t>(to);
	if (op == QLatin1String("delete")) { frames.erase(begin, end); return frames; }
	if (op == QLatin1String("crop")) { return std::vector<SampleFrame>(begin, end); }
	if (op == QLatin1String("reverse")) { std::reverse(begin, end); return frames; }
	// The last frame of the range reaches the fade's end (0 for a fade-out), as sampleops::fade does.
	const std::size_t span = to - from - 1;  // a one-frame range has no ramp: its only frame IS the endpoint
	for (std::size_t i = from; i < to; ++i)
	{
		const double t = span == 0 ? 1.0 : static_cast<double>(i - from) / static_cast<double>(span);
		const float factor = op == QLatin1String("silence") ? 0.f : op == QLatin1String("fade_in") ? static_cast<float>(t)
			: op == QLatin1String("fade_out") ? static_cast<float>(1.0 - t) : gain;
		frames[i] = SampleFrame(frames[i][0] * factor, frames[i][1] * factor);
	}
	return frames;
}

/*! The window the clip plays after @a op over [@a from, @a to): unchanged when the length is, the frames left after
 *  a delete closed up around it (review W3-batchB #2: a delete must not retarget the clip to the whole source), the
 *  whole kept range after a crop. */
SampleWindow windowAfter(const SampleWindow& window, const QString& op, std::size_t from, std::size_t to, f_cnt_t frames)
{
	if (op == QLatin1String("crop")) { return SampleWindow::full(frames); }
	if (op != QLatin1String("delete")) { return window; }
	const auto cut = static_cast<f_cnt_t>(to - from);
	const auto shifted = [from, to, cut](f_cnt_t at) {
		if (at <= static_cast<f_cnt_t>(from)) { return at; }
		return at >= static_cast<f_cnt_t>(to) ? at - cut : static_cast<f_cnt_t>(from);
	};
	const SampleWindow kept = SampleWindow::clamped(shifted(window.sourceIn), shifted(window.sourceOut), frames);
	return kept.empty() ? SampleWindow::full(frames) : kept;
}

//! Whether @a op deletes every frame the clip plays (it would be left playing nothing).
bool deletesWindow(const SampleWindow& window, const QString& op, std::size_t from, std::size_t to)
{
	return op == QLatin1String("delete") && window.sourceIn >= from && window.sourceOut <= to;
}

/*! Whether @a op over [@a from, @a to) would move or drop a marker of @a markers: a crop makes the new frame 0
 *  the old @a from, and a delete closes the range up so every later frame moves earlier - either leaves a marker
 *  pinning a different frame of the audio, or one the source no longer holds. The ops that write frames in place
 *  (gain, silence, reverse, the fades) keep every source frame where it was, so they never strand one. */
bool movesWarpMarkers(const WarpMarkers& markers, const QString& op, std::size_t from, std::size_t to)
{
	if (markers.empty()) { return false; }
	if (op == QLatin1String("crop")) { return true; }  // frame 0 is a different frame of the audio
	if (op != QLatin1String("delete")) { return false; }
	// The set is sorted by source frame, so only the last marker decides whether any frame moves.
	return static_cast<std::size_t>(markers[markers.size() - 1].sourceFrame) >= from;
}



//! Whether @a op applies @a db as a gain and it is out of the range the bakers declare.
bool gainRefused(const QString& op, double db)
{
	// gain_db is 'gain's parameter: an op that applies no gain is not refused over it.
	if (op != QLatin1String("gain")) { return false; }
	return db < -60.0 || db > 24.0;  // the range sample.amplify and clip.set_gain declare; far beyond it the gain overflows
}

//! The clip and the clamped [from, to) a range edit addresses - the part every op shares.
struct RangeTarget
{
	ClipRef ref;
	SampleClip* clip = nullptr;
	std::size_t size = 0;
	std::size_t from = 0;
	std::size_t to = 0;
	QString op;
};

//! Whether a range op is missing one of its two bounds ('mono' needs neither).
bool halfRange(const QJsonObject& args, const QString& op)
{
	if (op == QLatin1String("mono")) { return false; }
	return !args.contains(QStringLiteral("from")) || !args.contains(QStringLiteral("to"));
}

bool resolveRange(const QJsonObject& args, RangeTarget* target, ControlResult* error)
{
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &target->ref, error)) { return false; }
	auto* clip = dynamic_cast<SampleClip*>(target->ref.clip);
	if (clip == nullptr || clip->sample().data() == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'clip' is an audio clip with audio"));
		return false;
	}
	target->clip = clip;
	target->size = static_cast<std::size_t>(clip->sample().sampleSize());
	target->op = args.value(QStringLiteral("op")).toString();
	const bool monoSink = target->op == QLatin1String("mono");
	// 'mono' is a whole-source op, so an agent that only wants the downmix need not
	// know the frame count: absent from/to mean the whole source. Every range op
	// still insists on both bounds.
	if (halfRange(args, target->op))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a range 'from' < 'to' inside the source, and an 'op'"));
		return false;
	}
	target->from = static_cast<std::size_t>(std::max(0, args.value(QStringLiteral("from")).toInt()));
	target->to = args.contains(QStringLiteral("to"))
		? std::min(target->size, static_cast<std::size_t>(std::max(0, args.value(QStringLiteral("to")).toInt())))
		: (monoSink ? target->size : 0);
	if (target->to <= target->from || !operations().contains(target->op))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a range 'from' < 'to' inside the source, and an 'op'"));
		return false;
	}
	return true;
}

ControlResult editRange(const QJsonObject& args)
{
	RangeTarget target;
	ControlResult error;
	if (!resolveRange(args, &target, &error)) { return error; }
	SampleClip* clip = target.clip;
	const std::size_t size = target.size;
	const std::size_t from = target.from;
	const std::size_t to = target.to;
	const QString& op = target.op;
	const double db = args.value(QStringLiteral("gain_db")).toDouble(0.0);
	if (gainRefused(op, db))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("gain_db %1 is outside -60..24 dB").arg(db));
	}
	const SampleWindow window = clip->sampleWindow();
	// A crop re-bases frame 0 onto 'from' and a delete closes the range up, so a marker would then pin a
	// different frame of the audio, or one the source no longer holds (the same refusal sample.crop makes).
	if (movesWarpMarkers(clip->warpMarkers(), op, from, to))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip's warp markers pin frames that %1 would "
			"move or drop (warp.remove them first)").arg(op));
	}
	if (deletesWindow(window, op, from, to))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("that deletes everything the clip plays (clip.delete removes the clip)"));
	}
	const int rate = static_cast<int>(clip->sample().sampleRate());
	if (op == QLatin1String("mono"))
	{
		// The layout is a property of the whole source, not of a selection: refuse a partial
		// range rather than silently converting only part of it.
		if (from != 0 || to != size)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("'mono' converts the whole source: use from 0 and to %1").arg(size));
		}
		return bakeMono(target.ref, clip, window, size, rate);
	}
	const float gain = std::pow(10.f, static_cast<float>(db) / 20.f);
	const SampleFrame* data = clip->sample().data();
	std::vector<SampleFrame> result = applied(std::vector<SampleFrame>(data, data + size), op, from, to, gain);
	const auto frames = static_cast<f_cnt_t>(result.size());
	const int channels = clip->sample().channels();
	clip->addJournalCheckPoint();
	// Same length: the clip keeps playing the window it played; a new length plays the whole new source.
	clip->replaceSample(std::make_shared<SampleBuffer>(std::move(result), rate, QString(), channels),
		windowAfter(window, op, from, to, frames));
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("clip"), clipId(target.ref.id)}, {QStringLiteral("op"), op}, {QStringLiteral("frames"), static_cast<double>(frames)},
		{QStringLiteral("sample_rate"), rate}, {QStringLiteral("channels"), channels},
		{QStringLiteral("layout"), layoutName(channels)},
		{QStringLiteral("window"), QJsonArray{static_cast<double>(clip->sampleWindow().sourceIn), static_cast<double>(clip->sampleWindow().sourceOut)}}});
}

} // namespace

void registerSampleRangeCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("sample.edit_range");
	cmd.group = QStringLiteral("sample");
	cmd.verb = QStringLiteral("edit_range");
	cmd.description = QStringLiteral("Edit an audio clip's source over source frames ['from', 'to'): 'op' gain ('gain_db'), silence, "
		"delete (the rest closes up), reverse, fade_in, fade_out or crop (keep only the range) - the sample editor's "
		"operations - or 'mono', which converts the WHOLE source (from 0, to the frame count) with the documented "
		"per-frame arithmetic mean (L + R) / 2 on both playback sides, keeping the rate, frame count and clip "
		"settings and reporting channels/layout - 'mono' needs only 'clip' (absent from/to default to the whole "
		"source), while every range op requires both bounds. A fresh copy of the source is swapped in, never a "
		"buffer another clip shares. One undo step (Clip checkpoint).");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("from"), integerProperty()},
		{QStringLiteral("to"), integerProperty()}, {QStringLiteral("op"), enumProperty(operations())}, {QStringLiteral("gain_db"), numberProperty()}},
		QJsonArray{QStringLiteral("clip"), QStringLiteral("op")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = editRange;
	registry.registerCommand(cmd);
}

} // namespace lmms
