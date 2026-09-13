/*
 * ControlCommandsWarp.cpp - the warp.* command group (SPEC A11-A16), read half:
 *                           the shared helpers and warp.list, which is the
 *                           state every other verb returns.
 *
 * The engine half of #597 landed before this file: `WarpMarkers` is a
 * fixed-capacity value type (include/WarpMarkers.h), the `<warp>` element is
 * read and written by SampleClip (src/core/SampleClip.cpp), and
 * SampleClip::setWarpMarkers / setWarpTempoMode / setSourceTempo are the
 * authoring entry points. What was missing is the AGENT SURFACE: with no
 * warp.* commands a marker can only be authored by editing the project file,
 * which is exactly the gap AGENT-TOOLING.md section 1 makes a defect.
 *
 * The mutating verbs live in ControlCommandsWarpEdit.cpp, the same split the
 * automation group uses (ControlCommandsAutomation.cpp / ...AutomationEdit.cpp).
 * This group edits markers and nothing else: it does not change the mapping,
 * the resampler, the stretch mode or the rendering, so the numbers docs/WARP.md
 * measured are untouched by it.
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

#include <algorithm>
#include <limits>
#include <span>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlWarpSupport.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace control
{

namespace
{

//! The qulonglong/qlonglong argument helper the refusal messages use: a
//! source frame is unsigned and an offset is signed, and QString::arg has no
//! overload that takes either without the cast.
template <typename T>
qint64 wireInteger(T value)
{
	return static_cast<qint64>(value);
}

} // namespace

QString tempoModeName(WarpTempoMode mode)
{
	return mode == WarpTempoMode::SourceTempo ? QStringLiteral("source") : QStringLiteral("follow");
}

QJsonObject markerJson(const WarpMarker& marker, int index)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("index"), index);
	entry.insert(QStringLiteral("source_frame"), wireInteger(marker.sourceFrame));
	entry.insert(QStringLiteral("offset_ticks"), wireInteger(marker.offsetTicks));
	return entry;
}

namespace
{

QJsonArray markerListJson(const SampleClip& clip)
{
	const WarpMarkers& warp = clip.warpMarkers();
	QJsonArray out;
	for (int i = 0; i < warp.size(); ++i) { out.append(markerJson(warp[i], i)); }
	return out;
}

} // namespace

QJsonObject warpState(const ClipRef& ref, const SampleClip& clip)
{
	QJsonObject out;
	out.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	out.insert(QStringLiteral("track"), trackIdOf(ref.track));
	out.insert(QStringLiteral("warped"), !clip.warpMarkers().empty());
	out.insert(QStringLiteral("tempo_mode"), tempoModeName(clip.warpTempoMode()));
	out.insert(QStringLiteral("source_tempo"), static_cast<double>(clip.sourceTempo()));
	out.insert(QStringLiteral("marker_count"), clip.warpMarkers().size());
	out.insert(QStringLiteral("max_markers"), WarpMarkers::MaxMarkers);
	out.insert(QStringLiteral("markers"), markerListJson(clip));
	return out;
}

QJsonObject warpBefore(const ClipRef& ref, const SampleClip& clip)
{
	QJsonObject before;
	before.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	before.insert(QStringLiteral("markers"), markerListJson(clip));
	before.insert(QStringLiteral("tempo_mode"), tempoModeName(clip.warpTempoMode()));
	before.insert(QStringLiteral("source_tempo"), static_cast<double>(clip.sourceTempo()));
	return before;
}

QJsonObject warpInverse(const QJsonObject& before)
{
	QJsonObject args;
	args.insert(QStringLiteral("clip"), before.value(QStringLiteral("clip")));
	args.insert(QStringLiteral("markers"), before.value(QStringLiteral("markers")));
	args.insert(QStringLiteral("mode"), before.value(QStringLiteral("tempo_mode")));
	args.insert(QStringLiteral("source_tempo"), before.value(QStringLiteral("source_tempo")));
	// SPEC A16: the marker set, the tempo mode and the source tempo are all
	// fields of the clip's serialized <warp> element, so the clip's own journal
	// checkpoint is the inverse and this descriptor is what a reader re-issues
	// by hand. transactionPayload's "applies" defaults to the journal, which is
	// what control.undo unwinds.
	return transactionPayload(before, QStringLiteral("warp.set"), args, true,
		QStringLiteral("ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the "
			"<warp> child element - markers, tempo mode and source tempo - and "
			"SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)"));
}

SampleClip* resolveSampleClip(const QJsonObject& args, ClipRef* ref, ControlResult* error)
{
	const QString id = args.value(QStringLiteral("clip")).toString();
	if (!resolveClip(id, ref, error)) { return nullptr; }
	auto* clip = dynamic_cast<SampleClip*>(ref->clip);
	if (clip == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not a sample clip: warp markers pin positions on a SampleClip's "
				"audio, and this clip sits on a %2 track").arg(id, trackTypeNameOf(ref->track->type())));
		return nullptr;
	}
	return clip;
}

bool readMarker(const QJsonValue& value, WarpMarker* marker, ControlResult* error)
{
	if (!value.isObject())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a marker must be an object of the form "
				"{\"source_frame\": <integer>, \"offset_ticks\": <integer>}"));
		return false;
	}
	const QJsonObject object = value.toObject();
	const QJsonValue frameValue = object.value(QStringLiteral("source_frame"));
	const QJsonValue offsetValue = object.value(QStringLiteral("offset_ticks"));
	const double sourceFrame = frameValue.toDouble(-1.0);
	if (!frameValue.isDouble() || sourceFrame < 0.0
		|| sourceFrame > static_cast<double>(MaxSchemaInteger) || !offsetValue.isDouble())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a marker needs an integer 'source_frame' in 0..%1 and an integer "
				"'offset_ticks'").arg(MaxSchemaInteger));
		return false;
	}
	marker->sourceFrame = static_cast<f_cnt_t>(sourceFrame);
	marker->offsetTicks = static_cast<tick_t>(offsetValue.toDouble());
	return true;
}

MarkerArray markerArrayOf(const WarpMarkers& warp, int* count)
{
	MarkerArray out{};
	*count = warp.size();
	std::copy(warp.all().begin(), warp.all().end(), out.begin());
	return out;
}

int indexOfSourceFrame(const WarpMarkers& warp, f_cnt_t sourceFrame)
{
	for (int i = 0; i < warp.size(); ++i)
	{
		if (warp[i].sourceFrame == sourceFrame) { return i; }
	}
	return -1;
}

void offsetBounds(const WarpMarkers& warp, int index, tick_t* low, tick_t* high)
{
	*low = index > 0 ? static_cast<tick_t>(warp[index - 1].offsetTicks + 1)
		: std::numeric_limits<tick_t>::min();
	*high = index + 1 < warp.size() ? static_cast<tick_t>(warp[index + 1].offsetTicks - 1)
		: std::numeric_limits<tick_t>::max();
}

bool applyMarkers(SampleClip* clip, const MarkerArray& markers, int count, ControlResult* error)
{
	if (count == 0) { clip->clearWarpMarkers(); return true; }
	WarpMarkers candidate;
	const std::span<const WarpMarker> wanted(markers.data(), static_cast<std::size_t>(count));
	if (!candidate.set(wanted) || !clip->setWarpMarkers(wanted))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the marker set is not strictly increasing in source frame and timeline "
				"position: the engine refuses it and nothing was written"));
		return false;
	}
	return true;
}

QJsonObject warpStateSchema(QJsonObject extra)
{
	QJsonObject properties{
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("warped"), booleanProperty()},
		{QStringLiteral("tempo_mode"), stringProperty()},
		{QStringLiteral("source_tempo"), numberProperty()},
		{QStringLiteral("marker_count"), integerProperty(0, WarpMarkers::MaxMarkers)},
		{QStringLiteral("max_markers"), integerProperty(0, WarpMarkers::MaxMarkers)},
		{QStringLiteral("markers"), arrayProperty()},
	};
	for (auto it = extra.begin(); it != extra.end(); ++it) { properties.insert(it.key(), it.value()); }
	return objectSchema(properties);
}

QJsonObject tempoModeProperty()
{
	return QJsonObject{
		{QStringLiteral("type"), QStringLiteral("string")},
		{QStringLiteral("enum"), QJsonArray{QStringLiteral("follow"), QStringLiteral("source")}}};
}

QJsonObject markerProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
}

QJsonObject markersProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
		{QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
}

QJsonObject offsetTicksProperty()
{
	// Signed: a marker may legitimately pin a source frame to a position before
	// the clip's own origin (the engine's rule is "strictly increasing", not
	// "non-negative"), so the bound is the schema's own integer range.
	return integerProperty(-MaxSchemaInteger, MaxSchemaInteger);
}

} // namespace control

namespace
{

ControlResult warpList(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = control::resolveSampleClip(args, &ref, &error);
	if (clip == nullptr) { return error; }
	return ControlResult::success(control::warpState(ref, *clip));
}

} // namespace

void registerWarpCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("warp.list");
		cmd.group = QStringLiteral("warp");
		cmd.verb = QStringLiteral("list");
		cmd.description = QStringLiteral("The warp map of a sample clip: its markers (the source "
			"frame each one pins and the clip-relative timeline offset it lands on, in the order the "
			"engine holds them), its tempo mode, its declared source tempo and the engine's marker "
			"cap. Read-only.");
		cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}},
			{QStringLiteral("clip")});
		cmd.resultSchema = warpStateSchema();
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return warpList(args); };
		registry.registerCommand(cmd);
	}

	registerWarpEditCommands(registry);
}

} // namespace lmms
