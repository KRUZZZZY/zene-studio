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
 *  one undo step; the clip's window is kept over what it played, or reset when the source changes length. */

#include <algorithm>
#include <cmath>
#include <vector>

#include <QJsonArray>

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

const QStringList& operations()
{
	static const QStringList names{QStringLiteral("gain"), QStringLiteral("silence"), QStringLiteral("delete"), QStringLiteral("reverse"),
		QStringLiteral("fade_in"), QStringLiteral("fade_out"), QStringLiteral("crop")};
	return names;
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
	const double span = static_cast<double>(std::max<std::size_t>(1, to - from - 1));
	for (std::size_t i = from; i < to; ++i)
	{
		const double t = static_cast<double>(i - from) / span;
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

ControlResult editRange(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	if (clip == nullptr || clip->sample().data() == nullptr) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clip' is an audio clip with audio")); }
	const auto size = static_cast<std::size_t>(clip->sample().sampleSize());
	const auto from = static_cast<std::size_t>(std::max(0, args.value(QStringLiteral("from")).toInt()));
	const auto to = std::min(size, static_cast<std::size_t>(std::max(0, args.value(QStringLiteral("to")).toInt())));
	const QString op = args.value(QStringLiteral("op")).toString();
	if (to <= from || !operations().contains(op)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a range 'from' < 'to' inside the source, and an 'op'")); }
	const double db = args.value(QStringLiteral("gain_db")).toDouble(0.0);
	if (db < -60.0 || db > 24.0)  // the range sample.amplify and clip.set_gain declare; far beyond it the gain overflows
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("gain_db %1 is outside -60..24 dB").arg(db));
	}
	const SampleWindow window = clip->sampleWindow();
	if (deletesWindow(window, op, from, to))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("that deletes everything the clip plays (clip.delete removes the clip)"));
	}
	const float gain = std::pow(10.f, static_cast<float>(db) / 20.f);
	const SampleFrame* data = clip->sample().data();
	std::vector<SampleFrame> result = applied(std::vector<SampleFrame>(data, data + size), op, from, to, gain);
	const int rate = static_cast<int>(clip->sample().sampleRate());
	const auto frames = static_cast<f_cnt_t>(result.size());
	clip->addJournalCheckPoint();
	// Same length: the clip keeps playing the window it played; a new length plays the whole new source.
	clip->replaceSample(std::make_shared<SampleBuffer>(std::move(result), rate), windowAfter(window, op, from, to, frames));
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("op"), op}, {QStringLiteral("frames"), static_cast<double>(frames)},
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
		"operations. A fresh copy of the source is swapped in, never a buffer another clip shares. One undo step (Clip checkpoint).");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("from"), integerProperty()},
		{QStringLiteral("to"), integerProperty()}, {QStringLiteral("op"), enumProperty(operations())}, {QStringLiteral("gain_db"), numberProperty()}},
		QJsonArray{QStringLiteral("clip"), QStringLiteral("from"), QStringLiteral("to"), QStringLiteral("op")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = editRange;
	registry.registerCommand(cmd);
}

} // namespace lmms
