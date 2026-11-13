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

#include "AudioEngine.h"
#include "Clip.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "TakeLane.h"
#include "TimePos.h"
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

//! True when every stored version parses. A version that does not is never copied (review L19 #15).
bool allReadable(const Set& versions)
{
	for (auto version = versions.clips.constBegin(); version != versions.clips.constEnd(); ++version)
	{
		if (!QDomDocument().setContent(version.value())) { return false; }
	}
	return true;
}

//! The clips of @a stored (a <clips> document) added to @a track as clips of take lane @a lane, each a new clip
//! (a fresh id, in no take group, not linked). Returns the new clips as a <clips> document.
QString addLaneCopies(Track* track, const QString& stored, int lane)
{
	QDomDocument doc;
	if (!doc.setContent(stored)) { return QString(); }
	QDomDocument out;
	QDomElement root = out.createElement(QStringLiteral("clips"));
	out.appendChild(root);
	// Same guard as TrackVersions::replaceClips: the audio thread walks the track's clips while these are added.
	auto guard = Engine::audioEngine()->requestChangesGuard();
	for (QDomElement element = doc.documentElement().firstChildElement(); !element.isNull(); element = element.nextSiblingElement())
	{
		element.removeAttribute(QStringLiteral("id"));  // no id: restoreState takes a fresh one, so the copy is a new clip
		Clip* clip = track->createClip(TimePos(0));
		clip->restoreState(element);
		clip->clearTakeGroupId();
		clip->setLinkId(0);
		clip->setLaneIndex(lane);
		clip->saveState(out, root);
	}
	return out.toString(-1);
}

//! @a versions with @a copies appended to every stored version, so switching versions keeps the lane's takes
//! (the copies are kept with each version, not only with the one playing).
Set withCopies(const Set& versions, const QString& copies)
{
	Set after = versions;
	for (auto version = after.clips.begin(); version != after.clips.end(); ++version)
	{
		version.value() = TrackVersions::appendClips(version.value(), copies);
	}
	return after;
}

//! Adds the take lane for a version. Lane 0 is the base lane every untagged clip sits on, so a track with no
//! lanes gets its base lane (named \p base) first: the version's lane is then never lane 0. Returns the
//! version's lane index, or -1 when the model has no room.
int addVersionLane(TakeLaneModel& lanes, const QString& base, const QString& name)
{
	if (lanes.laneCount() == 0 && lanes.addLane(base) < 0) { return -1; }
	return lanes.addLane(name);
}

//! Makes the stored version 'name' a take lane of the track: the lane holds copies of its clips as they are now.
//! The copy is taken at this moment; later edits to the version do not follow. One undo step.
ControlResult versionToLane(const QJsonObject& args)
{
	Track* track = nullptr;
	Set before;
	ControlResult error;
	if (!versionsOf(args, &track, &before, &error)) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a take lane needs a name")); }
	if (!before.clips.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the track has no version '%1'").arg(name)); }
	if (!allReadable(before)) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a stored version cannot be read; the track is left as it is")); }
	const bool needsBase = track->takeLanes().laneCount() == 0;
	if (!track->takeLanes().canAddLanes(needsBase ? 2 : 1)) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the track has no room for another take lane")); }
	addUndoStep(QVector<JournallingObject*>{track});
	// The base lane takes the active version's name, or 'Main' when that is empty or equals the requested name.
	const bool baseNameFree = !before.active.isEmpty() && before.active != name;
	const QString base = baseNameFree ? before.active : QStringLiteral("Main");
	const int lane = addVersionLane(track->takeLanes(), base, name);
	if (lane <= 0) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the track could not add the take lane")); }
	const QString copies = addLaneCopies(track, before.clips.value(name), lane);
	const Set after = withCopies(before, copies);
	commit(track->id(), before, after);
	QJsonObject result = report(track, after);
	result.insert(QStringLiteral("lane"), lane);
	return ControlResult::success(result);
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
	defineCommand(registry, QStringLiteral("track.version_to_lane"), QStringLiteral("Add a take lane named 'name' holding copies of the "
		"stored version's clips as they are now (later edits to the version do not follow). The lane is a comp source. One undo step."),
		named, both, true, versionToLane);
}

} // namespace lmms
