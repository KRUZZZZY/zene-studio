/*
 * ControlCommandsFreezeBatch.cpp - Q050-327: freeze.tracks, several tracks frozen as one task and one undo step
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

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "BatchFreeze.h"
#include "ControlBounceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult refused(const QString& why)
{
	return ControlResult::failure(ControlErrorKind::Refused, why);
}

//! Every named track, resolved and checked, or a refusal naming the first that cannot be frozen.
ControlResult collectTracks(const QJsonObject& args, std::vector<Track*>* tracks)
{
	const QJsonValue list = args.value(QStringLiteral("tracks"));
	if (!list.isArray()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'tracks' must be an array of track ids")); }
	QSet<int> seen;
	for (const QJsonValue& value : list.toArray())
	{
		ControlResult error;
		Track* track = resolveTrack(value.toString(), &error);
		if (track == nullptr) { return error; }
		if (seen.contains(track->id()))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("%1 is named twice").arg(trackIdOf(track)));
		}
		seen.insert(track->id());
		if (track->type() != Track::Type::Instrument && track->type() != Track::Type::Sample)
		{
			return refused(QStringLiteral("%1 is not an instrument or sample track; no track was frozen").arg(trackIdOf(track)));
		}
		if (track->isFrozen())
		{
			return refused(QStringLiteral("%1 is already frozen (freeze.unfreeze first); no track was frozen").arg(trackIdOf(track)));
		}
		tracks->push_back(track);
	}
	if (tracks->size() < 2)
	{
		return refused(QStringLiteral("a batch freeze needs two or more tracks; use freeze.track or render.queue_bounce for one"));
	}
	return ControlResult::success();
}

QString outputFor(const QJsonObject& args, Track* track)
{
	const QString path = resolveOutPath(QJsonObject{}, track, QStringLiteral("frozen"), BounceInPlace::Range{});
	const QString dir = args.value(QStringLiteral("out_dir")).toString();
	return dir.isEmpty() ? path : QDir(dir).filePath(QFileInfo(path).fileName());
}

void discardAll(BatchFreezeJob* job)
{
	for (BatchFreezeTarget& target : job->targets) { BounceInPlace::discardPreparedRender(&target.prepared); }
}

ControlResult freezeTracks(const QJsonObject& args)
{
	std::vector<Track*> tracks;
	const ControlResult collected = collectTracks(args, &tracks);
	if (!collected.ok) { return collected; }
	Song* song = Engine::getSong();
	BatchFreezeJob job;
	job.args = args;
	job.documentIdentity = song->documentIdentity();
	job.tempo = static_cast<int>(song->getTempo());
	job.tempoMap = song->tempoMap().map();
	QJsonArray accepted;
	for (Track* track : tracks)
	{
		BatchFreezeTarget target;
		target.trackId = track->id();
		target.fingerprint = BatchFreeze::trackFingerprint(track);
		QString error;
		if (target.fingerprint.isEmpty()) { error = QStringLiteral("could not capture the track for stale-target checking"); }
		else
		{
			BounceInPlace::prepareRenderTracks({track}, outputFor(args, track), BounceInPlace::Range{}, 44100,
				&target.prepared, &error);
		}
		job.targets.push_back(std::move(target));
		if (!error.isEmpty())
		{
			discardAll(&job);
			return refused(QStringLiteral("%1: %2; no track was frozen").arg(trackIdOf(track), error));
		}
		accepted.append(QJsonObject{{QStringLiteral("track"), trackIdOf(track)},
			{QStringLiteral("out"), job.targets.back().prepared.outputPath}});
	}
	const int id = BatchFreeze::instance().start(std::move(job));
	if (id < 0)
	{
		return refused(QStringLiteral("%1 batch freezes are still running").arg(BatchFreeze::MaxBatches));
	}
	const BatchFreezeJob* started = BatchFreeze::instance().job(id);
	return ControlResult::success({{QStringLiteral("batch"), id},
		{QStringLiteral("task"), TaskCenter::instance().idOf(&BatchFreeze::instance(), id)},
		{QStringLiteral("state"), started != nullptr ? BatchFreeze::stateName(started->state) : QStringLiteral("running")},
		{QStringLiteral("tracks"), accepted}, {QStringLiteral("parallel"), BatchFreeze::parallelLimit()}});
}

} // namespace

void registerFreezeTracksCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("freeze.tracks");
	cmd.group = QStringLiteral("freeze");
	cmd.verb = QStringLiteral("tracks");
	cmd.description = QStringLiteral("Freeze two or more instrument or sample tracks ('tracks': ids) as ONE background task: "
		"their renders run at the same time (up to 'parallel' child renders), task.list shows one 'freeze' task with "
		"aggregate progress and task.cancel stops it. Returns at once with the batch, its task id and each track's output "
		"file. Nothing is applied until every render has succeeded; then every track is re-checked and all are frozen "
		"in ONE undo step (control.undo takes the whole batch off). A cancel, a failed render or a track, tempo or "
		"project that changed meanwhile ends the batch with no track frozen and no staged file left. Refused up front: "
		"fewer than two tracks, a repeated id, a track that is not an instrument or sample track, or one already "
		"frozen. 'out_dir' places the takes (default: the project's bounce folder). track.get_state reads each result.");
	cmd.argsSchema = objectSchema({{QStringLiteral("tracks"), arrayProperty()}, {QStringLiteral("out_dir"), stringProperty()}},
		{QStringLiteral("tracks")});
	cmd.resultSchema = objectSchema({{QStringLiteral("batch"), integerProperty()}, {QStringLiteral("task"), integerProperty()},
		{QStringLiteral("state"), stringProperty()}, {QStringLiteral("tracks"), arrayProperty()},
		{QStringLiteral("parallel"), integerProperty()}});
	cmd.mutating = true;
	cmd.handler = freezeTracks;
	registry.registerCommand(cmd);
}

} // namespace lmms
