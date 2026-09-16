/*
 * ClipLinkTestSupport.h - the fixtures the clip.link_* tests share: every scene
 *                         they build is built through the CONTROL SURFACE
 *                         (ControlRegistry::invoke), because that is the door the
 *                         release contract accepts (feature-list row 6, board
 *                         task #645).
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

// WHY A HEADER. The proof lives in two binaries - ClipLinkTest (the relation,
// the propagation, the unlink, one undo, the A16 rows and the UI-absence line)
// and ClipLinkPersistenceTest (the save/reload round trip and the typed
// refusals) - and both need the same fixtures. Every function here is stateless
// and inline: it takes an id or a name and reads the engine back through the
// registry, so neither binary can drift from the other's idea of a scene.

#ifndef LMMS_TEST_CLIP_LINK_TEST_SUPPORT_H
#define LMMS_TEST_CLIP_LINK_TEST_SUPPORT_H

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlRegistry.h"

namespace cliplinktest
{

// The fixtures drive lmms's own control surface, so they name it directly (a
// test that has to write lmms:: in front of every type is a test nobody reads).
using namespace lmms;

// Declared before the two scene builders below, which call it.
inline QString addTrack(const QString& type);

// -----------------------------------------------------------------------
// fixtures, all through the control surface (the release's own door)
// -----------------------------------------------------------------------

//! Adds an instrument track and a MIDI clip on it; returns the clip id.
inline QString addMidiClip(const QString& name)
{
	ControlRegistry* registry = ControlRegistry::instance();
	const QString track = addTrack(QStringLiteral("instrument"));
	if (track.isEmpty()) { return QString(); }
	const ControlResult clip = registry->invoke(QStringLiteral("clip.add"),
		QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
			{QStringLiteral("length"), 384}, {QStringLiteral("name"), name}});
	if (!clip.ok) { return QString(); }
	return clip.result.value(QStringLiteral("clip")).toString();
}

//! Adds a sample track and the audio clip on it; returns the clip id.
inline QString addSampleClip(const QString& name)
{
	ControlRegistry* registry = ControlRegistry::instance();
	const QString track = addTrack(QStringLiteral("sample"));
	if (track.isEmpty()) { return QString(); }
	const ControlResult clip = registry->invoke(QStringLiteral("clip.add"),
		QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
			{QStringLiteral("length"), 384}, {QStringLiteral("name"), name}});
	if (!clip.ok) { return QString(); }
	return clip.result.value(QStringLiteral("clip")).toString();
}

inline QString addTrack(const QString& type)
{
	const ControlResult added = ControlRegistry::instance()->invoke(QStringLiteral("track.add"),
		QJsonObject{{QStringLiteral("type"), type}});
	if (!added.ok) { return QString(); }
	return added.result.value(QStringLiteral("track")).toString();
}

//! The clip's own authoritative read of its membership - a fresh lookup of the
//! object the id names, not a remembered pointer. -1 when the query itself failed.
inline int clipLinkId(const QString& clipId)
{
	const ControlResult state = ControlRegistry::instance()->invoke(
		QStringLiteral("clip.link_get_state"), QJsonObject{{QStringLiteral("clip"), clipId}});
	if (!state.ok) { return -1; }
	return state.result.value(QStringLiteral("group")).toInt();
}

//! The clip's start position in ticks, read from arrangement.get_state's flat
//! `clips` array (each entry is a clipState(), whose id key is `id`). -1 when no
//! clip of that id is in the arrangement, so a caller can tell "not found" from
//! "at tick 0".
inline int clipStart(const QString& clipId)
{
	const ControlResult state = ControlRegistry::instance()->invoke(
		QStringLiteral("arrangement.get_state"));
	if (!state.ok) { return -1; }
	for (const QJsonValue& clipValue : state.result.value(QStringLiteral("clips")).toArray())
	{
		const QJsonObject clip = clipValue.toObject();
		if (clip.value(QStringLiteral("id")).toString() == clipId)
		{
			return clip.value(QStringLiteral("position")).toInt();
		}
	}
	return -1;
}

//! The clip's notes, read through roll.get_state - the surface's own view of the
//! note list the group shares.
inline QJsonArray notesOf(const QString& clipId)
{
	const ControlResult rolled = ControlRegistry::instance()->invoke(
		QStringLiteral("roll.get_state"), QJsonObject{{QStringLiteral("clip"), clipId}});
	if (!rolled.ok) { return QJsonArray(); }
	return rolled.result.value(QStringLiteral("notes")).toArray();
}

inline int noteCount(const QString& clipId) { return notesOf(clipId).size(); }

//! The id of the note at @a index of @a clipId's list, from roll.get_state -
//! the id the ENGINE gave THAT clip's note object.
//!
//! Asked for per clip, and that is the point: a link group's members mirror each
//! other's content as their OWN Note objects (ClipLinks::writeContent copies the
//! note list), and R4 of SPEC-stable-ids.md makes a copy a NEW object with a new
//! id - so the id a note.add to clip B returned names B's object, never A's
//! mirror of it. Addressing A with B's id is what this file measured as
//! "no note note-12 (the clip has 2)". Empty when the index is outside the list.
inline QString noteIdAt(const QString& clipId, int index)
{
	const QJsonArray notes = notesOf(clipId);
	if (index < 0 || index >= notes.size()) { return QString(); }
	return notes.at(index).toObject().value(QStringLiteral("id")).toString();
}
inline int noteKeyAt(const QString& clipId, int index)
{
	const QJsonArray notes = notesOf(clipId);
	if (index < 0 || index >= notes.size()) { return -1; }
	return notes.at(index).toObject().value(QStringLiteral("key")).toInt();
}
inline int notePositionAt(const QString& clipId, int index)
{
	const QJsonArray notes = notesOf(clipId);
	if (index < 0 || index >= notes.size()) { return -1; }
	return notes.at(index).toObject().value(QStringLiteral("position")).toInt();
}
inline int noteLengthAt(const QString& clipId, int index)
{
	const QJsonArray notes = notesOf(clipId);
	if (index < 0 || index >= notes.size()) { return -1; }
	return notes.at(index).toObject().value(QStringLiteral("length")).toInt();
}
inline int noteVelocityAt(const QString& clipId, int index)
{
	const QJsonArray notes = notesOf(clipId);
	if (index < 0 || index >= notes.size()) { return -1; }
	return notes.at(index).toObject().value(QStringLiteral("velocity")).toInt();
}

//! The group object of the requested size, or an empty object.
inline QJsonObject firstGroupOfSize(const QJsonArray& groups, int size)
{
	for (const QJsonValue& value : groups)
	{
		const QJsonObject group = value.toObject();
		if (group.value(QStringLiteral("size")).toInt() == size) { return group; }
	}
	return QJsonObject();
}

/*! A doc file, read from the source tree this test binary was built out of.
 *  ctest runs the binary from <build>/tests, so the worktree root is two
 *  directories up; the candidates are tried in order and an unreadable file
 *  is an empty string, which the caller turns into a SKIP rather than a
 *  fabricated pass. */
inline QString readDoc(const QString& name)
{
	const QStringList candidates = {
		QCoreApplication::applicationDirPath() + QStringLiteral("/../../docs/") + name,
		QCoreApplication::applicationDirPath() + QStringLiteral("/../../../docs/") + name,
	};
	for (const QString& path : candidates)
	{
		QFile file(path);
		if (file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			return QString::fromUtf8(file.readAll());
		}
	}
	return QString();
}


} // namespace cliplinktest

#endif // LMMS_TEST_CLIP_LINK_TEST_SUPPORT_H
