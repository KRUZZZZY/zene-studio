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
 *  track at the range's start; with 'mute_sources' (the default) the bounced tracks are muted so the
 *  song sounds as before. One undo step removes the new track and puts the mutes back. */

#include <memory>
#include <vector>

#include <QPointer>

#include "ArrangementEdits.h"
#include "BounceInPlace.h"
#include "ControlBounceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
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
	bool muteSources = true;
	std::vector<QPointer<Track>> sources;
	std::vector<bool> wereMuted;
	Track* landed = nullptr;
};

void land(Landing* l)
{
	Track* track = Track::create(Track::Type::Sample, Engine::getSong());
	track->setName(QObject::tr("Bounce"));
	if (auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(l->at)))) { clip->setSampleFile(l->path); }
	l->landed = track;
	for (const QPointer<Track>& source : l->sources)
	{
		if (source && l->muteSources) { source->setMuted(true); }
	}
}

void unland(Landing* l)
{
	if (l->landed != nullptr) { removeTrack(l->landed); }
	l->landed = nullptr;
	for (std::size_t i = 0; i < l->sources.size(); ++i)
	{
		if (l->sources[i]) { l->sources[i]->setMuted(l->wereMuted[i]); }
	}
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
	const QString out = resolveOutPath(args, tracks.front(), QStringLiteral("selection"), window);
	const BounceInPlace::Result rendered = BounceInPlace::renderTracks(tracks, out, window);
	if (!rendered.ok) { return ControlResult::failure(ControlErrorKind::Refused, rendered.error); }
	auto l = std::make_shared<Landing>();
	l->path = rendered.path;
	l->at = window.start;
	l->muteSources = args.value(QStringLiteral("mute_sources")).toBool(true);
	for (Track* track : tracks)
	{
		l->sources.emplace_back(track);
		l->wereMuted.push_back(track->isMuted());
	}
	addUndoStep([l]() { unland(l.get()); }, [l]() { land(l.get()); });
	land(l.get());
	QJsonObject result = renderJson(rendered);
	result.insert(QStringLiteral("landed_track"), trackIdOf(l->landed));
	result.insert(QStringLiteral("tracks"), static_cast<int>(tracks.size()));
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("tracks"), static_cast<int>(tracks.size())}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: the recorded undo step removes the landed track "
			"and puts the bounced tracks' mutes back; its redo lands the same file again")));
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
		"'mute_sources' (true when left out) mutes the bounced tracks so the song sounds as before. One undo step.");
	cmd.argsSchema = objectSchema({{QStringLiteral("out"), stringProperty()}, {QStringLiteral("mute_sources"), booleanProperty()}});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = rangeBounce;
	registry.registerCommand(cmd);
}

} // namespace lmms
