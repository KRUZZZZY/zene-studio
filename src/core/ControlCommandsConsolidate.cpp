/*
 * ControlCommandsConsolidate.cpp - R3.2: clip.consolidate, a region's clips become one clip
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

/*! clip.consolidate renders the clips of a sample-track region - every lane, through the
 *  composite's gate, with their gain and fades - into one file (ClipConsolidate.h says why
 *  that is not a bounce) and replaces them with one clip that plays it. The order is the
 *  freeze verbs' order: render FIRST, so a render that fails writes nothing and records
 *  nothing; then one Track checkpoint, so one control.undo brings every source clip and the
 *  composite back. A clip that crosses the region's edge is refused rather than cut: which
 *  half to keep is the caller's decision (clip.split first). Muted clips are left where they
 *  are - they sound nothing, so there is nothing of theirs to consolidate.
 */

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ClipConsolidate.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLane.h"

namespace lmms
{

namespace
{

const QString ClauseConsolidate = QStringLiteral("ProjectJournal (Track checkpoint: "
	"Track::restoreState re-loads the track's serialized clips and its take lanes, so the "
	"source clips and the composite come back together)");

struct Region
{
	tick_t start = 0;
	tick_t end = 0;
	std::vector<SampleClip*> inside;
	int mutedKept = 0;
};

std::vector<SampleClip*> sampleClipsOf(SampleTrack* track)
{
	std::vector<SampleClip*> clips;
	for (Clip* clip : track->getClips())
	{
		if (auto* sampleClip = dynamic_cast<SampleClip*>(clip)) { clips.push_back(sampleClip); }
	}
	return clips;
}

//! The extent of the unmuted clips: the region a call that names none consolidates.
void unmutedExtent(const std::vector<SampleClip*>& clips, Region* region)
{
	bool any = false;
	for (const SampleClip* clip : clips)
	{
		if (clip->isMuted()) { continue; }
		const tick_t from = clip->startPosition().getTicks();
		const tick_t to = clip->endPosition().getTicks();
		region->start = any ? std::min(region->start, from) : from;
		region->end = any ? std::max(region->end, to) : to;
		any = true;
	}
}

//! Sorts the clips against the region: inside (consolidated), muted (kept), outside
//! (ignored); a clip crossing an edge is the refusal returned.
QString sortIntoRegion(const std::vector<SampleClip*>& clips, Region* region)
{
	for (SampleClip* clip : clips)
	{
		const tick_t from = clip->startPosition().getTicks();
		const tick_t to = clip->endPosition().getTicks();
		if (to <= region->start || from >= region->end) { continue; }
		if (clip->isMuted()) { ++region->mutedKept; continue; }
		if (from < region->start || to > region->end)
		{
			return QStringLiteral("%1 (%2..%3) crosses the region's edge (%4..%5); split it there "
				"first (clip.split) - which half belongs to the consolidated clip is the caller's call")
				.arg(control::clipIdOf(clip)).arg(from).arg(to).arg(region->start).arg(region->end);
		}
		region->inside.push_back(clip);
	}
	return QString();
}

//! The region the call names, or the extent of the track's unmuted clips when it names
//! none; empty on success. Fills the clips inside it and refuses one that crosses an edge
//! (the error kind goes to \a kind).
QString collectRegion(SampleTrack* track, const QJsonObject& args, Region* region, ControlErrorKind* kind)
{
	*kind = ControlErrorKind::InvalidArgs;
	const bool hasStart = args.contains(QStringLiteral("start"));
	if (hasStart != args.contains(QStringLiteral("end")))
	{
		return QStringLiteral("'start' and 'end' go together: give both, or neither for every clip");
	}
	const std::vector<SampleClip*> clips = sampleClipsOf(track);
	if (hasStart)
	{
		region->start = static_cast<tick_t>(args.value(QStringLiteral("start")).toDouble());
		region->end = static_cast<tick_t>(args.value(QStringLiteral("end")).toDouble());
		if (region->end <= region->start) { return QStringLiteral("'end' must be past 'start'"); }
	}
	else { unmutedExtent(clips, region); }
	if (const QString crossing = sortIntoRegion(clips, region); !crossing.isEmpty()) { return crossing; }
	if (region->inside.empty())
	{
		*kind = ControlErrorKind::Refused;
		return QStringLiteral("no unmuted clip of %1 lies in %2..%3: nothing to consolidate")
			.arg(control::trackIdOf(track)).arg(region->start).arg(region->end);
	}
	return QString();
}

//! Beside the project (or in the temp directory for an unsaved one), and never a name
//! that already exists: an earlier consolidated clip - one an undo can bring back - may
//! still be playing the file a reused name would overwrite.
QString uniqueOutPath(const QJsonObject& args, const Track* track, const Region& region)
{
	const QString requested = args.value(QStringLiteral("out")).toString();
	if (!requested.isEmpty()) { return requested; }
	const QString project = Engine::getSong()->projectFileName();
	const QString stem = project.isEmpty() ? QStringLiteral("untitled") : QFileInfo(project).completeBaseName();
	const QDir directory((project.isEmpty() ? QDir(QDir::tempPath()) : QFileInfo(project).dir())
		.filePath(stem + QStringLiteral("-consolidated")));
	const QString base = QStringLiteral("%1-%2-%3").arg(control::trackIdOf(track)).arg(region.start).arg(region.end);
	QString candidate = directory.filePath(base + QStringLiteral(".wav"));
	for (int n = 2; QFileInfo::exists(candidate); ++n)
	{
		candidate = directory.filePath(QStringLiteral("%1-%2.wav").arg(base).arg(n));
	}
	return candidate;
}

/*! ONE Track checkpoint, then the swap: the source clips go, one clip playing @a path takes
 *  the region, on the base lane when the track has lanes, and the composite over the region
 *  names it. @a recomped says whether there was a composite to repaint. */
SampleClip* replaceWithConsolidated(SampleTrack* track, const Region& region, const QString& path,
	bool* recomped)
{
	track->addJournalCheckPoint();
	track->saveJournallingState(false);
	for (SampleClip* clip : region.inside)
	{
		track->removeClip(clip);
		delete clip;
	}
	auto* fresh = dynamic_cast<SampleClip*>(track->createClip(TimePos(region.start)));
	fresh->setSampleFile(path);
	fresh->changeLength(TimePos(region.end - region.start));
	fresh->setAutoResize(false);
	TakeLaneModel& lanes = track->takeLanes();
	*recomped = lanes.laneCount() > 0 && !lanes.segments().empty();
	if (lanes.laneCount() > 0) { control::assignTakeLane(fresh, lanes.baseLane()); }
	if (*recomped) { lanes.selectSegment(region.start, region.end, lanes.baseLane()); }
	track->restoreJournallingState();
	return fresh;
}

ControlResult clipConsolidate(const QJsonObject& args)
{
	ControlResult error;
	Track* resolved = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (resolved == nullptr) { return error; }
	auto* track = dynamic_cast<SampleTrack*>(resolved);
	if (track == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 is not a sample track: consolidation renders audio clips")
				.arg(control::trackIdOf(resolved)));
	}
	if (track->isFrozen())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is frozen: its clips are not what plays; freeze.unfreeze first")
				.arg(control::trackIdOf(track)));
	}
	Region region;
	ControlErrorKind kind = ControlErrorKind::InvalidArgs;
	const QString problem = collectRegion(track, args, &region, &kind);
	if (!problem.isEmpty()) { return ControlResult::failure(kind, problem); }

	const ClipConsolidate::Result rendered = ClipConsolidate::render(region.inside, region.start,
		region.end, uniqueOutPath(args, track, region));
	if (!rendered.ok) { return ControlResult::failure(ControlErrorKind::Refused, rendered.error); }

	QJsonArray sources;
	for (const SampleClip* clip : region.inside) { sources.append(control::clipIdOf(clip)); }
	QJsonObject before;
	before.insert(QStringLiteral("track"), control::trackIdOf(track));
	before.insert(QStringLiteral("clips"), sources);

	bool recomped = false;
	SampleClip* fresh = replaceWithConsolidated(track, region, rendered.path, &recomped);

	QJsonObject result;
	result.insert(QStringLiteral("track"), control::trackIdOf(track));
	result.insert(QStringLiteral("clip"), control::clipIdOf(fresh));
	result.insert(QStringLiteral("path"), rendered.path);
	result.insert(QStringLiteral("sample_rate"), rendered.sampleRate);
	result.insert(QStringLiteral("frames"), rendered.frames);
	result.insert(QStringLiteral("start"), static_cast<qint64>(region.start));
	result.insert(QStringLiteral("end"), static_cast<qint64>(region.end));
	result.insert(QStringLiteral("consolidated"), sources);
	result.insert(QStringLiteral("muted_kept"), region.mutedKept);
	result.insert(QStringLiteral("recomped"), recomped);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(before, QStringLiteral("UNIMPLEMENTED: restore the source clips"),
			QJsonObject(), true, ClauseConsolidate));
	return ControlResult::success(result);
}

} // namespace

void registerClipConsolidateCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.consolidate");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("consolidate");
	cmd.description = QStringLiteral("Replace the clips of a sample-track region with ONE clip "
		"playing their audio: every lane through the composite's gate, with each clip's window, "
		"warp, gain and fades - the clips' own audio, not the track's output (the track's devices, "
		"fader and pan still apply once, on playback). `start`/`end` (ticks) name the region; "
		"neither means every unmuted clip. A clip crossing the region's edge is refused (clip.split "
		"it first); muted clips are left in place. The file is stereo float WAV at the engine rate, "
		"at `out` or beside the project under a name never reused. Rendered BEFORE anything "
		"changes, so a failed render writes nothing; one control.undo restores every source clip "
		"and the composite (Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("start"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("end"), control::integerProperty(1, MaxSongLength)},
		{QStringLiteral("out"), control::stringProperty()},
	}, {QStringLiteral("track")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("path"), control::stringProperty()},
		{QStringLiteral("sample_rate"), control::integerProperty(1, 1 << 20)},
		{QStringLiteral("frames"), control::numberProperty()},
		{QStringLiteral("start"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("end"), control::integerProperty(1, MaxSongLength)},
		{QStringLiteral("consolidated"), control::arrayProperty()},
		{QStringLiteral("muted_kept"), control::integerProperty(0, 1 << 20)},
		{QStringLiteral("recomped"), control::booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return clipConsolidate(args); };
	registry.registerCommand(cmd);
}

} // namespace lmms
