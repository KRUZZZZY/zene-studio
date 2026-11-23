/*
 * ControlCommandsBounceLand.cpp - bounce.land: a track rendered onto a new audio track, the source muted
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

#include <memory>

#include <QJsonObject>

#include "BounceInPlace.h"
#include "ControlBounceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

//! A new audio track holding the render at the range's start, named after the source.
Track* landTrack(Song* song, const QString& name, const QString& path, tick_t at)
{
	Track* track = Track::create(Track::Type::Sample, song);
	if (track == nullptr) { return nullptr; }
	track->setName(name);
	if (auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(at)))) { clip->setSampleFile(path); }
	return track;
}

ControlResult bounceLand(const QJsonObject& args)
{
	ControlResult error;
	Track* source = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (source == nullptr) { return error; }
	BounceInPlace::Range range;
	if (const QString problem = parseRange(args, false, &range); !problem.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, problem);
	}
	// A bounce of a RANGE mutes only that range afterwards, so its render holds those pieces and their ringing tail.
	range.replacesRegion = !range.coversWholeTrack();
	const BounceInPlace::Result rendered = BounceInPlace::renderTrack(source,
		resolveOutPath(args, source, QStringLiteral("bounce"), range), range);
	if (!rendered.ok) { return ControlResult::failure(ControlErrorKind::Refused, rendered.error); }

	// The render happened before anything was recorded, so a failed render leaves the journal
	// untouched (SPEC A16). ONE action step: undo removes the landed track and restores the
	// source's mute; redo lands the same file again.
	Song* song = Engine::getSong();
	const QString name = source->name() + QObject::tr(" (bounce)");
	const QString path = rendered.path;
	const tick_t at = range.start;
	const bool wasMuted = source->isMuted();
	const QString sourceId = trackIdOf(source);
	// A whole-track bounce mutes the whole source (its whole output is replaced). A bounce of a RANGE mutes only the
	// range: clips are cut at its edges, automation lanes keep running outside it and a frozen take is rewritten
	// with only the overlap silent (cluster 3971) - that half of the step is what muteRangeSources records.
	const bool region = !range.coversWholeTrack();
	if (region)
	{
		const QString muteError = muteRangeSources({source}, range);
		if (!muteError.isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, muteError); }
	}
	// The landed track by its stable id, found again when the undo runs: a later
	// track.remove and its undo re-create the track as a NEW object under the same id,
	// so a pointer kept here would name a deleted track.
	auto landed = std::make_shared<QString>();
	addUndoStep(
		[landed, sourceId, wasMuted, region]() {
			ControlResult ignored;
			if (Track* track = resolveTrack(*landed, &ignored)) { removeTrack(track); }
			landed->clear();
			if (!region) { if (Track* current = resolveTrack(sourceId, &ignored)) { current->setMuted(wasMuted); } }
		},
		[landed, sourceId, song, name, path, at, region]() {
			*landed = trackIdOf(landTrack(song, name, path, at));
			if (!region) {
				ControlResult ignored;
				if (Track* current = resolveTrack(sourceId, &ignored)) { current->setMuted(true); }
			}
		});
	*landed = trackIdOf(landTrack(song, name, path, at));
	if (!region) { source->setMuted(true); }

	QJsonObject result = renderJson(rendered);
	result.insert(QStringLiteral("track"), trackIdOf(source));
	result.insert(QStringLiteral("landed_track"), *landed);
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(QJsonObject{{QStringLiteral("track"), trackIdOf(source)}, {QStringLiteral("muted"), wasMuted}},
			QStringLiteral("remove the landed track and restore the source's mutes"), QJsonObject(), true,
			QStringLiteral("action checkpoint: the recorded undo step removes the created track through "
				"control::removeTrack (as track.add's does); a Track checkpoint writes back the cuts and mutes of "
				"a bounced range")));
	return ControlResult::success(result);
}

} // namespace

void registerBounceLandCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("bounce.land");
	cmd.group = QStringLiteral("bounce");
	cmd.verb = QStringLiteral("land");
	cmd.description = QStringLiteral("Bounce a track in place, the way a DAW's 'bounce to audio' "
		"does: render its own output (as bounce.in_place does, the whole track or 'start'/'end' "
		"ticks) and put the render on a NEW audio track at the range's start, named '<track> "
		"(bounce)', with the source track muted. Reversible: one control.undo removes the new "
		"track and restores the source's mute. bounce.in_place stays the render-only verb; "
		"freeze.track plays a render on the source track itself. This command keeps its synchronous "
		"result contract for compatibility; use render.queue_bounce with operation bounce_land for a cancellable job.");
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("out"), stringProperty()},
		{QStringLiteral("start"), integerProperty()}, {QStringLiteral("end"), integerProperty()}}, {QStringLiteral("track")});
	cmd.resultSchema = objectSchema({{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("landed_track"), stringProperty()}, {QStringLiteral("path"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()}, {QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("bytes"), integerProperty()}, {QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("start_ticks"), integerProperty()}, {QStringLiteral("end_ticks"), integerProperty()}});
	cmd.mutating = true;
	cmd.handler = bounceLand;
	registry.registerCommand(cmd);
}

} // namespace lmms
