/*
 * ControlCompSupport.cpp - the comp.* group's shared helpers (see the header).
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
 */

#include "ControlCompSupport.h"

#include "Clip.h"
#include "ControlEdit.h"   // enumerateClips(), resolveClip(), ClipRef
#include "ControlRegistry.h"
#include "SampleClip.h"
#include "TimePos.h"
#include "Track.h"

namespace lmms
{
namespace control
{

QString takeClipId(const Clip* clip)
{
	if (clip == nullptr) { return QString(); }
	for (const ClipRef& ref : enumerateClips())
	{
		if (ref.clip == clip) { return clipId(ref.ordinal); }
	}
	return QString();
}


bool resolveTakeClip(const QString& id, ClipRef* ref, ControlResult* error)
{
	if (!resolveClip(id, ref, error)) { return false; }
	if (dynamic_cast<SampleClip*>(ref->clip) != nullptr) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("take lanes carry audio takes in this release; %1 is a %2 clip "
			"(docs/COMPING.md)")
			.arg(clipId(ref->ordinal), ref->clip->nodeName()));
	return false;
}


QJsonObject laneState(const Track* track, const TakeLane& lane)
{
	QJsonObject out;
	out.insert(QStringLiteral("lane"), lane.index);
	out.insert(QStringLiteral("name"), lane.name);
	QJsonArray takes;
	if (track != nullptr)
	{
		for (const Clip* clip : track->getClips())
		{
			if (clip == nullptr || clip->laneIndex() != lane.index) { continue; }
			const QString id = takeClipId(clip);
			if (!id.isEmpty()) { takes.append(id); }
		}
	}
	out.insert(QStringLiteral("takes"), takes);
	out.insert(QStringLiteral("take_count"), takes.size());
	return out;
}


QJsonArray lanesState(const Track* track)
{
	QJsonArray lanes;
	if (track == nullptr) { return lanes; }
	for (const TakeLane& lane : track->takeLanes().lanes())
	{
		lanes.append(laneState(track, lane));
	}
	return lanes;
}


QJsonObject segmentState(const TakeLaneSegment& seg)
{
	QJsonObject out;
	out.insert(QStringLiteral("begin"), seg.beginTick);
	out.insert(QStringLiteral("end"), seg.endTick);
	out.insert(QStringLiteral("length"), seg.endTick - seg.beginTick);
	out.insert(QStringLiteral("lane"), seg.laneIndex);
	out.insert(QStringLiteral("srcpos"), seg.sourceOffset);
	return out;
}


QJsonArray resolvedState(const Track* track)
{
	QJsonArray resolved;
	if (track == nullptr) { return resolved; }
	const TakeLaneModel& model = track->takeLanes();
	for (const TakeLaneSegment& seg : model.segments())
	{
		QJsonObject entry = segmentState(seg);
		int lane = 0;
		// ONE lookup for both questions (which take clip, which source frame):
		// the model's takeAt() is the same lookup resolveSource() uses, so the
		// reported binding and the engine's own resolution cannot disagree.
		Clip* take = model.takeAt(track->getClips(), seg.beginTick, &lane);
		if (take == nullptr)
		{
			entry.insert(QStringLiteral("status"), QStringLiteral("unresolved"));
			entry.insert(QStringLiteral("clip"), QJsonValue());
			entry.insert(QStringLiteral("frame"), QJsonValue());
			resolved.append(entry);
			continue;
		}
		entry.insert(QStringLiteral("status"), QStringLiteral("bound"));
		entry.insert(QStringLiteral("clip"), takeClipId(take));
		entry.insert(QStringLiteral("frame"),
			static_cast<double>(take->sourceFrameAt(TimePos(seg.beginTick))));
		resolved.append(entry);
	}
	return resolved;
}


QJsonObject compositeState(const Track* track)
{
	QJsonObject out;
	QJsonArray segments;
	int begin = 0;
	int end = 0;
	bool comped = false;
	if (track != nullptr)
	{
		for (const TakeLaneSegment& seg : track->takeLanes().segments())
		{
			segments.append(segmentState(seg));
		}
		comped = track->takeLanes().span(&begin, &end);
	}
	out.insert(QStringLiteral("segments"), segments);
	out.insert(QStringLiteral("resolved"), resolvedState(track));
	out.insert(QStringLiteral("count"), segments.size());
	out.insert(QStringLiteral("comped"), comped);
	out.insert(QStringLiteral("begin"), begin);
	out.insert(QStringLiteral("end"), end);
	return out;
}

} // namespace control
} // namespace lmms
