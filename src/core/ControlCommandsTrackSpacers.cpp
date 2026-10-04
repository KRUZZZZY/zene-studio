/*
 * ControlCommandsTrackSpacers.cpp - track.set_spacer / track.spacers (WF-107)
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

#include <QJsonArray>

#include "ControlArrangingCommands.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackSpacers.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject spacerJson(const Track* track)
{
	const TrackSpacers::Spacer spacer = TrackSpacers::instance().spacer(track->id());
	return {{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("name"), track->name()},
		{QStringLiteral("height"), spacer.height}, {QStringLiteral("label"), spacer.label}};
}

void apply(int trackId, const TrackSpacers::Spacer& spacer)
{
	TrackSpacers::instance().setSpacer(trackId, spacer);
}

ControlResult setSpacer(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const int id = track->id();
	const TrackSpacers::Spacer before = TrackSpacers::instance().spacer(id);
	const TrackSpacers::Spacer after{args.value(QStringLiteral("height")).toInt(before.height == 0 ? 24 : before.height),
		args.value(QStringLiteral("label")).toString(before.label).trimmed()};
	apply(id, after);
	const TrackSpacers::Spacer now = TrackSpacers::instance().spacer(id);
	if (!(now == before))
	{
		addUndoStep([id, before]() { apply(id, before); }, [id, now]() { apply(id, now); });
		Engine::getSong()->setModified();
	}
	return ControlResult::success(spacerJson(track));
}

ControlResult listSpacers(const QJsonObject&)
{
	QJsonArray out;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (TrackSpacers::instance().spacer(track->id()).height > 0) { out.append(spacerJson(track)); }
	}
	return ControlResult::success({{QStringLiteral("spacers"), out}, {QStringLiteral("count"), out.size()}});
}

} // namespace

void registerTrackSpacerCommands(ControlRegistry& registry)
{
	TrackSpacers::instance();  // registers the project section before a project loads
	defineCommand(registry, QStringLiteral("track.set_spacer"), QStringLiteral("A gap below 'track' in the track list, to "
		"divide the list into sections: 'height' in pixels (0 removes it, at most 200; 24 when a new spacer leaves it out) "
		"and an optional 'label' drawn in it. Not a track - it has no clips, device or channel - so it moves and goes with "
		"its track. Saved with the project; one undo step."),
		{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("height"), integerProperty(0, TrackSpacers::MaxHeight)},
			{QStringLiteral("label"), stringProperty()}}, {QStringLiteral("track")}, true, setSpacer);
	defineCommand(registry, QStringLiteral("track.spacers"), QStringLiteral("The tracks with a spacer below them, in track "
		"order: the track, its name, the spacer's height and label."), {}, {}, false, listSpacers);
}

void registerArrangingCommands(ControlRegistry& registry)
{
	registerTrackSpacerCommands(registry);
	registerReferenceCommands(registry);
	registerDocumentCommands(registry);
}

} // namespace lmms
