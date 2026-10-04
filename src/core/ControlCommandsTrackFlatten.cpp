/*
 * ControlCommandsTrackFlatten.cpp - a track replaced by the audio of its processing
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
/*! 0.5.0 L12 (ED-207): track.flatten - commit a track's processing: its output (its instrument or clips
 *  through its devices, fader, pan and sends - BounceInPlace's render, the one bounce.land uses) replaces
 *  the track itself, as an audio track of the same name at the same place in the list. Rendered BEFORE
 *  anything changes, so a failed render writes nothing; one undo step removes the audio track and puts
 *  the source track back - with its clips, devices and settings - from the document captured before. */

#include <memory>

#include <QJsonObject>

#include "BounceInPlace.h"
#include "ControlBounceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

Track* control::landAudioTrack(const QString& name, const QString& path, tick_t at, int index)
{
	Track* track = Track::create(Track::Type::Sample, Engine::getSong());
	if (track == nullptr) { return nullptr; }
	track->setName(name);
	if (auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(at)))) { clip->setSampleFile(path); }
	if (index >= 0) { moveTrackToIndex(track, index); }
	return track;
}

namespace
{

//! landAudioTrack, answering the new track's stable id (-1 for none): the undo step below holds ids, never
//! pointers, because track.remove and its undo free a track and build an equal one.
int landAudioTrackId(const QString& name, const QString& path, tick_t at, int index)
{
	const Track* track = landAudioTrack(name, path, at, index);
	return track != nullptr ? track->id() : -1;
}

ControlResult flatten(const QJsonObject& args)
{
	ControlResult error;
	Track* source = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (source == nullptr) { return error; }
	BounceInPlace::Range range;
	if (const QString problem = parseRange(args, false, &range); !problem.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, problem);
	}
	QString xml;
	if (!captureTrackXml(source, &xml, StructuralSnapshotLimit))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 is too large to keep for an undo; bounce.land "
			"renders it beside the track instead").arg(trackIdOf(source)));
	}
	const BounceInPlace::Result rendered = BounceInPlace::renderTrack(source, resolveOutPath(args, source, QStringLiteral("flatten"), range), range);
	if (!rendered.ok) { return ControlResult::failure(ControlErrorKind::Refused, rendered.error); }

	Song* song = Engine::getSong();
	const int index = trackIndexIn(song, source);
	const QString name = source->name();
	const QString sourceId = trackIdOf(source);
	const QString path = rendered.path;
	const tick_t at = range.start;
	auto landed = std::make_shared<int>(-1);
	auto restored = std::make_shared<int>(-1);
	addStructuralUndoStep(
		[landed, restored, song, xml, index]() {
			if (Track* live = trackWithId(*landed)) { removeTrack(live); }
			*landed = -1;
			Track* back = restoreTrackFromXml(xml, song);
			*restored = back != nullptr ? back->id() : -1;
			if (back != nullptr) { moveTrackToIndex(back, index); }
		},
		[landed, restored, name, path, at, index]() {
			if (Track* live = trackWithId(*restored)) { removeTrack(live); }
			*restored = -1;
			*landed = landAudioTrackId(name, path, at, index);
		},
		xml.size());
	removeTrack(source);
	*landed = landAudioTrackId(name, path, at, index);

	QJsonObject result = renderJson(rendered);
	result.insert(QStringLiteral("flattened"), sourceId);
	result.insert(QStringLiteral("track"), trackIdOf(trackWithId(*landed)));
	return ControlResult::success(result);
}

} // namespace

void registerTrackFlattenCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.flatten");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("flatten");
	cmd.description = QStringLiteral("Flatten a track: render its output - its instrument or clips through its devices, fader, "
		"pan and sends, as bounce.land does (the whole track, or 'start'/'end' ticks; the file at 'out' or beside the project) - "
		"and REPLACE the track with an audio track of the same name at the same place playing the render, so the processing "
		"is committed and its CPU freed. Refused for a track whose document is too large to keep for an undo. One undo step "
		"puts the source track back with its clips, devices and settings.");
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("out"), stringProperty()},
		{QStringLiteral("start"), integerProperty()}, {QStringLiteral("end"), integerProperty()}}, {QStringLiteral("track")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = flatten;
	registry.registerCommand(cmd);
}

} // namespace lmms
