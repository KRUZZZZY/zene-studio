/*
 * ControlCommandsWarpQuantize.cpp - warp.quantize: quantise audio - a sample clip's transients warped onto the grid
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

/*! ED-137 (0.5.0 L07): quantise audio. The clip's transients (detection::detectOnsetFrames, the onset
 *  rule the tempo detector counts with) become warp markers, each moved `strength` of the way to the
 *  nearest `grid` step from where the clip plays it now. The new marker set replaces the old one in
 *  ONE Clip journal checkpoint (the markers live in the clip's <warp> element), so one undo takes it
 *  back; the audio itself is not touched. */

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "ClipTransients.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "ControlWarpSupport.h"
#include "SampleClip.h"

namespace lmms
{

using namespace control;

namespace
{

//! Each transient at the tick it plays at now, moved toward the grid; increasing ticks only, so the
//! marker set stays monotonic (a transient that would land on or before the previous one is left out).
int quantisedMarkers(const SampleClip& clip, const std::vector<f_cnt_t>& transients, int grid, double strength, MarkerArray* markers)
{
	const tick_t origin = clip.startPosition().getTicks() + clip.startTimeOffset().getTicks();
	int count = 0;
	// "Has a marker been emitted yet", NOT a tick value: a marker offset may
	// legitimately be negative (the engine's rule is strictly increasing, not
	// non-negative), and a -1 sentinel dropped every transient that lands at or
	// before tick -1 - so a clip whose existing map puts its audio before its own
	// origin reported "no transient found" although the detector found four.
	tick_t last = 0;
	bool emitted = false;
	// The marker set is written in PLAY frames - the coordinate the warp map
	// itself is written in (the render path asks the map about play frames) -
	// and a reversed clip's transients reach that coordinate in the opposite
	// order: the source frame that sounds LAST is the first one played. So the
	// transients are walked in play-frame order, which is the only order in
	// which the set is strictly increasing in both coordinates. A forward clip's
	// play frame is its source frame, so its set is exactly the one it was.
	std::vector<std::pair<f_cnt_t, f_cnt_t>> ordered;
	ordered.reserve(transients.size());
	for (const f_cnt_t frame : transients) { ordered.emplace_back(clip.playFrameFor(frame), frame); }
	std::sort(ordered.begin(), ordered.end());
	for (const auto& [playFrame, frame] : ordered)
	{
		if (count >= WarpMarkers::MaxMarkers) { break; }
		const tick_t now = clip.timelinePosAt(frame).getTicks() - origin;
		const tick_t target = static_cast<tick_t>(std::lround(static_cast<double>(now) / grid)) * grid;
		const tick_t moved = now + static_cast<tick_t>(std::lround((target - now) * strength));
		if (emitted && moved <= last) { continue; }
		(*markers)[static_cast<std::size_t>(count++)] = WarpMarker{playFrame, moved};
		last = moved;
		emitted = true;
	}
	return count;
}

ControlResult quantizeAudio(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = resolveSampleClip(args, &ref, &error);
	if (clip == nullptr) { return error; }
	const int grid = args.value(QStringLiteral("grid")).toInt(48);
	const double strength = args.value(QStringLiteral("strength")).toDouble(1.0);
	if (grid < 1 || strength < 0.0 || strength > 1.0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'grid' is >= 1 tick and 'strength' 0..1"));
	}
	const std::vector<f_cnt_t> transients = clipTransientFrames(*clip);
	MarkerArray markers{};
	const int count = quantisedMarkers(*clip, transients, grid, strength, &markers);
	if (count == 0) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no transient found in the clip's audio")); }
	const QJsonObject before = warpBefore(ref, *clip);
	clip->addJournalCheckPoint();
	if (!applyMarkers(clip, markers, count, &error)) { return error; }
	QJsonObject result = warpState(ref, *clip);
	result.insert(QStringLiteral("transients"), static_cast<int>(transients.size()));
	result.insert(QStringLiteral("__transaction"), warpInverse(before));
	return ControlResult::success(result);
}

} // namespace

void registerWarpQuantizeCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("warp");
	cmd.verb = QStringLiteral("quantize");
	cmd.id = QStringLiteral("warp.quantize");
	cmd.description = QStringLiteral("Quantise a sample clip's audio: its transients (the onset rule detect.* uses) become "
		"warp markers, each moved 'strength' (0..1, default 1) of the way to the nearest 'grid' step (ticks, default 48 = "
		"1/4) from where the clip plays it now; the new markers replace the old ones (at most 128, in order). The audio "
		"is not changed - the warp is, so warp.stretch picks how it sounds. Refused not_found when the audio has no "
		"transient. One undo step (Clip checkpoint).");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("grid"), integerProperty(1, 192 * 4)},
		{QStringLiteral("strength"), numberProperty()}}, {QStringLiteral("clip")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = quantizeAudio;
	registry.registerCommand(cmd);
}

} // namespace lmms
