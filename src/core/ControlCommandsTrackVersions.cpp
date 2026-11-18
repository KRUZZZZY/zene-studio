/*
 * ControlCommandsTrackVersions.cpp - save and switch named versions of a track's clips
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
#include <QDomDocument>
#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackVersions.h"

namespace lmms
{

using namespace control;

namespace
{

using Set = TrackVersions::Set;

//! The track and its versions, or false with @a error when 'track' names none.
bool versionsOf(const QJsonObject& args, Track** track, Set* versions, ControlResult* error)
{
	*track = resolveTrack(args.value(QStringLiteral("track")).toString(), error);
	if (*track == nullptr) { return false; }
	*versions = TrackVersions::instance().of((*track)->id());
	return true;
}

QJsonObject report(const Track* track, const Set& versions)
{
	return {{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("active"), versions.active},
		{QStringLiteral("versions"), QJsonArray::fromStringList(versions.clips.keys())}};
}

//! Store @a after for the track @a id, one undo step putting @a before back.
void commit(int id, const Set& before, const Set& after)
{
	TrackVersions::instance().set(id, after);
	addUndoStep([id, before] { TrackVersions::instance().set(id, before); }, [id, after] { TrackVersions::instance().set(id, after); });
	Engine::getSong()->setModified();
}

ControlResult listVersions(const QJsonObject& args)
{
	Track* track = nullptr;
	Set versions;
	ControlResult error;
	if (!versionsOf(args, &track, &versions, &error)) { return error; }
	return ControlResult::success(report(track, versions));
}

ControlResult saveVersion(const QJsonObject& args)
{
	Track* track = nullptr;
	Set before;
	ControlResult error;
	if (!versionsOf(args, &track, &before, &error)) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a version needs a name")); }
	Set after = before;
	const QString clips = TrackVersions::clipsOf(track);
	if (!before.active.isEmpty()) { after.clips.insert(before.active, clips); }
	after.clips.insert(name, clips);
	after.active = name;
	commit(track->id(), before, after);
	return ControlResult::success(report(track, after));
}

ControlResult switchVersion(const QJsonObject& args)
{
	Track* track = nullptr;
	Set before;
	ControlResult error;
	if (!versionsOf(args, &track, &before, &error)) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (!before.clips.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the track has no version '%1'").arg(name)); }
	if (name == before.active) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("'%1' is the version playing").arg(name)); }
	const QString stored = before.clips.value(name);
	if (!QDomDocument().setContent(stored))  // review L19 #15: checked before the undo step, so nothing is recorded
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the stored version '%1' cannot be read; the track is left as it is").arg(name));
	}
	Set after = before;
	if (!after.active.isEmpty()) { after.clips.insert(after.active, TrackVersions::clipsOf(track)); }  // kept as it was left
	addUndoStep(QVector<JournallingObject*>{track});
	TrackVersions::replaceClips(track, stored);
	after.active = name;
	commit(track->id(), before, after);
	return ControlResult::success(report(track, after));
}

ControlResult removeVersion(const QJsonObject& args)
{
	Track* track = nullptr;
	Set before;
	ControlResult error;
	if (!versionsOf(args, &track, &before, &error)) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (!before.clips.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the track has no version '%1'").arg(name)); }
	Set after = before;
	after.clips.remove(name);
	if (after.active == name) { after.active.clear(); }  // the clips stay on the track; only the stored copy goes
	commit(track->id(), before, after);
	return ControlResult::success(report(track, after));
}

} // namespace

void registerTrackVersionCommands(ControlRegistry& registry)
{
	TrackVersions::instance();  // registers its project section before a project loads
	const QJsonObject track{{QStringLiteral("track"), stringProperty()}};
	QJsonObject named = track;
	named.insert(QStringLiteral("name"), stringProperty());
	const QStringList both{QStringLiteral("track"), QStringLiteral("name")};
	defineCommand(registry, QStringLiteral("track.version_list"), QStringLiteral("A track's versions: their names and the one playing "
		"('active', empty before any is saved). Read only."), track, {QStringLiteral("track")}, false, listVersions);
	defineCommand(registry, QStringLiteral("track.version_save"), QStringLiteral("Keep the track's clips as the version 'name' (replacing "
		"a version of that name) and make it the one playing. Saved with the project. One undo step."), named, both, true, saveVersion);
	defineCommand(registry, QStringLiteral("track.version_switch"), QStringLiteral("Switch the track to its version 'name': the version "
		"playing is kept as it was left, and the track's clips become the named version's. One undo step puts both back."),
		named, both, true, switchVersion);
	defineCommand(registry, QStringLiteral("track.version_remove"), QStringLiteral("Forget the track's version 'name'. The track's clips "
		"stay as they are. One undo step."), named, both, true, removeVersion);
}

} // namespace lmms
