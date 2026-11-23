/*
 * ControlCommandsRangeBounce.cpp - range.bounce: a selection bounced to a new track
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
/*! 0.5.0 L12 (ED-205): bounce a selection to a new track. The arranger range's tracks (range.set),
 *  over the range's ticks, are rendered together - their summed contribution to the mix, in a child
 *  render of a copy of the session (BounceInPlace::renderTracks) - and the file lands on a new audio
 *  track at the range's start; with 'mute_sources' (the default) the bounced part of each source
 *  track is muted: clips inside the range (cut at its edges), the same part of a frozen take, and
 *  automation only inside its clips - so the song sounds as before while audio and automation outside
 *  the selection keep running. One undo step removes the new track, uncuts the clips, restores the
 *  takes and puts the mutes back. */

#include <memory>
#include <vector>

#include "ArrangementEdits.h"
#include "BounceInPlace.h"
#include "ControlBounceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlTrackCreation.h"
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

struct Landing
{
	QString path;
	tick_t at = 0;
	int landed = -1;  //!< -1 once the undo took it away; a track deleted some other way is not found, so nothing is removed
};

void land(Landing* l)
{
	Track* track = Track::create(Track::Type::Sample, Engine::getSong());
	track->setName(QObject::tr("Bounce"));
	if (auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(l->at)))) { clip->setSampleFile(l->path); }
	l->landed = track->id();
}

void unland(Landing* l)
{
	if (Track* track = trackWithId(l->landed)) { removeTrack(track); }
	l->landed = -1;
}

ControlResult rangeBounce(const QJsonObject& args)
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no range is selected (range.set)")); }
	std::vector<Track*> tracks;
	for (Track* track : range.tracks()) { tracks.push_back(track); }
	if (tracks.empty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the range covers no track")); }
	BounceInPlace::Range window;
	window.start = range.start();
	window.end = range.end();
	// With 'mute_sources' the bounced pieces are muted afterwards, so the render holds exactly those pieces and the
	// tail they ring on (cluster 3971); without it the render is the plain window.
	const bool muteSources = args.value(QStringLiteral("mute_sources")).toBool(true);
	window.replacesRegion = muteSources;
	const QString out = resolveOutPath(args, tracks.front(), QStringLiteral("selection"), window);
	const BounceInPlace::Result rendered = BounceInPlace::renderTracks(tracks, out, window);
	if (!rendered.ok) { return ControlResult::failure(ControlErrorKind::Refused, rendered.error); }
	// The mutes of the bounced region are a Track checkpoint (or a rewritten frozen take) recorded by
	// muteRangeSources; the registry merges it with the landing action below into ONE undo step. A take that
	// cannot be rewritten refuses before any model change; the render file that was just written stays.
	if (muteSources)
	{
		const QString muteError = muteRangeSources(tracks, window);
		if (!muteError.isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, muteError); }
	}
	auto l = std::make_shared<Landing>();
	l->path = rendered.path;
	l->at = window.start;
	addUndoStep([l]() { unland(l.get()); }, [l]() { land(l.get()); });
	land(l.get());
	QJsonObject result = renderJson(rendered);
	result.insert(QStringLiteral("landed_track"), trackIdOf(trackWithId(l->landed)));
	result.insert(QStringLiteral("tracks"), static_cast<int>(tracks.size()));
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("tracks"), static_cast<int>(tracks.size())}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: the recorded undo step removes the landed track "
			"and a Track checkpoint puts back the cuts and the mutes of the bounced region; its redo lands the same file again")));
	return ControlResult::success(result);
}

} // namespace

void registerRangeBounceCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("range.bounce");
	cmd.group = QStringLiteral("range");
	cmd.verb = QStringLiteral("bounce");
	cmd.description = QStringLiteral("Bounce the selection to a new track: the range's tracks (range.set), over its ticks, rendered "
		"together into one file ('out', or beside the project) that lands on a new audio track at the range's start; "
		"'mute_sources' (true when left out) mutes the bounced tracks so the song sounds as before. One undo step. This command "
		"keeps its synchronous result contract for compatibility; use render.queue_bounce with operation range_bounce for a cancellable job.");
	cmd.argsSchema = objectSchema({{QStringLiteral("out"), stringProperty()}, {QStringLiteral("mute_sources"), booleanProperty()}});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = rangeBounce;
	registry.registerCommand(cmd);
}

} // namespace lmms
