/*
 * ControlCommandsClipFrames.cpp - the frame-exact paths of clip.split and clip.trim (ED-021)
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

#include "ControlClipFrames.h"

#include <algorithm>

#include "ArrangementEdits.h"
#include "ControlVocabulary.h"
#include "SampleClip.h"
#include "SampleFrameEdits.h"
#include "Track.h"

namespace lmms::control::clipframes
{

namespace
{

const QString ClauseFrameSplit = QStringLiteral("ProjectJournal (Track checkpoint: Track::restoreState re-loads the "
	"track's serialized clips - 'srcin', 'srcout' and 'lead' included - which is how the GUI's own split reverses itself)");
const QString ClauseFrameTrim = QStringLiteral("ProjectJournal (Clip checkpoint taken before the first write: SampleClip::"
	"loadSettings re-reads the source (which resets the window), then 'pos', 'len', 'off', 'srcin'/'srcout', 'autoresize' and "
	"'lead' - the last reset when absent)");

bool zeroCrossing(const QJsonObject& args)
{
	return args.value(QStringLiteral("zero_crossing")).toBool(arrangement::modes().snapToZeroCrossings);
}

f_cnt_t framesArg(const QJsonObject& args, const char* key)
{
	return static_cast<f_cnt_t>(std::max(0.0, args.value(QLatin1String(key)).toDouble()));
}

//! The source frame a tick plus an extra frame count names, moved to the nearest zero crossing when asked.
f_cnt_t frameFor(SampleClip* clip, const QJsonObject& args, const char* tickKey, const char* framesKey)
{
	const auto tick = static_cast<tick_t>(args.value(QLatin1String(tickKey)).toDouble());
	const f_cnt_t frame = arrangement::frameAtTimeline(*clip, tick, framesArg(args, framesKey));
	return zeroCrossing(args) ? arrangement::nearestZeroCrossing(*clip, frame) : frame;
}

ControlResult refused(const QString& why)
{
	return ControlResult::failure(ControlErrorKind::InvalidArgs, why);
}

QJsonObject edges(const SampleClip* clip)
{
	return {{QStringLiteral("clip"), clipIdOf(clip)}, {QStringLiteral("position"), clip->startPosition().getTicks()},
		{QStringLiteral("length"), clip->length().getTicks()}, {QStringLiteral("end"), clip->endPosition().getTicks()},
		{QStringLiteral("offset"), clip->startTimeOffset().getTicks()},
		{QStringLiteral("source_in"), static_cast<qint64>(clip->sampleWindow().sourceIn)},
		{QStringLiteral("source_out"), static_cast<qint64>(clip->sampleWindow().sourceOut)},
		{QStringLiteral("lead_frames"), static_cast<qint64>(clip->leadFrames())}};
}

/*! The edges of a frame-exact trim, checked on the clip as it is (see trim()). The start moves only when it
 *  was asked to - a 'start' other than the clip's own, or 'start_frames' - so a trim of the end alone (the
 *  start passed as it is, as the clip menu's Trim End does) leaves the start where it was. */
struct TrimFrames
{
	f_cnt_t head = 0;
	f_cnt_t tail = 0;
	bool hasHead = false;
	bool hasTail = false;
};

bool checkTrim(SampleClip* clip, const TrimFrames& frames, QString* why)
{
	if (frames.hasHead && !arrangement::trimHeadToFrame(clip, frames.head, why, true)) { return false; }
	if (!frames.hasTail) { return true; }
	if (frames.hasHead && frames.tail <= frames.head)
	{
		*why = QStringLiteral("the end (frame %1) must be after the start (frame %2)").arg(frames.tail).arg(frames.head);
		return false;
	}
	return arrangement::trimTailToFrame(clip, frames.tail, why, true);
}

TrimFrames trimFrames(SampleClip* clip, const QJsonObject& args)
{
	TrimFrames frames;
	frames.hasHead = args.contains(QStringLiteral("start_frames"))
		|| static_cast<tick_t>(args.value(QStringLiteral("start")).toDouble()) != clip->startPosition().getTicks();
	frames.hasTail = args.contains(QStringLiteral("end"));
	if (frames.hasHead) { frames.head = frameFor(clip, args, "start", "start_frames"); }
	if (frames.hasTail) { frames.tail = frameFor(clip, args, "end", "end_frames"); }
	return frames;
}

} // namespace

bool wanted(const ClipRef& ref, const QJsonObject& args)
{
	if (dynamic_cast<SampleClip*>(ref.clip) == nullptr) { return false; }
	return args.contains(QStringLiteral("frames")) || args.contains(QStringLiteral("start_frames"))
		|| args.contains(QStringLiteral("end_frames")) || zeroCrossing(args);
}

ControlResult split(const ClipRef& ref, const QJsonObject& args)
{
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	QString why;
	if (!arrangement::frameEditable(*clip, &why)) { return refused(why); }
	const f_cnt_t frame = frameFor(clip, args, "position", "frames");
	if (!arrangement::canSplitAtFrame(*clip, frame, &why)) { return refused(why); }
	const QJsonObject before = edges(clip);
	ref.track->addJournalCheckPoint();
	SampleClip* right = arrangement::splitAtFrame(clip, frame, &why);
	QJsonObject result{{QStringLiteral("left"), clipIdOf(clip)}, {QStringLiteral("right"), clipIdOf(right)},
		{QStringLiteral("frame"), static_cast<qint64>(frame)}, {QStringLiteral("zero_crossing"), zeroCrossing(args)},
		{QStringLiteral("right_position"), right->startPosition().getTicks()},
		{QStringLiteral("lead_frames"), static_cast<qint64>(right->leadFrames())}};
	result.insert(QStringLiteral("__transaction"), transactionPayload(before, QStringLiteral("control.undo"),
		{{QStringLiteral("left"), clipIdOf(clip)}, {QStringLiteral("right"), clipIdOf(right)}}, true, ClauseFrameSplit));
	return ControlResult::success(result);
}

ControlResult trim(const ClipRef& ref, const QJsonObject& args)
{
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	QString why;
	if (!arrangement::frameEditable(*clip, &why)) { return refused(why); }
	// 'end_frames' is a frame offset from the 'end' tick, so without that tick there is nothing to offset
	// from: taking the frame path and trimming nothing would answer success without moving the clip.
	if (!args.contains(QStringLiteral("end")) && args.contains(QStringLiteral("end_frames")))
	{
		return refused(QStringLiteral("'end_frames' counts frames after 'end': send the end tick with it"));
	}
	const TrimFrames frames = trimFrames(clip, args);
	if (!frames.hasHead && !frames.hasTail) { return ControlResult::success(edges(clip)); }  // nothing to move
	if (!checkTrim(clip, frames, &why)) { return refused(why); }
	const QJsonObject before = edges(clip);
	clip->addJournalCheckPoint();
	if (frames.hasHead) { arrangement::trimHeadToFrame(clip, frames.head, &why); }
	if (frames.hasTail) { arrangement::trimTailToFrame(clip, frames.tail, &why); }
	QJsonObject result = edges(clip);
	result.insert(QStringLiteral("zero_crossing"), zeroCrossing(args));
	result.insert(QStringLiteral("__transaction"), transactionPayload(before, QStringLiteral("control.undo"),
		{{QStringLiteral("clip"), clipIdOf(clip)}}, true, ClauseFrameTrim));
	return ControlResult::success(result);
}

} // namespace lmms::control::clipframes
