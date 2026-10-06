/*
 * ControlCommandsKeyNames.cpp - key_names.*: per-track key labels and the drum preset
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

/*! 0.5.0 L07 (key labels and note naming, the drum lane view): a name per key of a track, shown by
 *  the note editor's keyboard and drum view. Each edit is one undo step that puts the track's
 *  previous names back. */

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
#include "TrackKeyNames.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonArray namesJson(const QHash<int, QString>& names)
{
	QJsonArray out;
	for (auto it = names.begin(); it != names.end(); ++it)
	{
		out.append(QJsonObject{{QStringLiteral("key"), it.key()}, {QStringLiteral("name"), it.value()}});
	}
	return out;
}

QJsonArray namesJson(int trackId)
{
	return namesJson(TrackKeyNames::instance().names(trackId));
}

//! Replace track @a track's names with @a next as ONE undo step.
ControlResult storeNames(Track* track, const QHash<int, QString>& next)
{
	const int id = track->id();
	const QHash<int, QString> before = TrackKeyNames::instance().names(id);
	addUndoStep([id, before] { TrackKeyNames::instance().setNames(id, before); },
		[id, next] { TrackKeyNames::instance().setNames(id, next); });
	TrackKeyNames::instance().setNames(id, next);
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("names"), namesJson(id)}};
	// The recorded step restores `before`, so the transaction has to carry it: a reversible
	// record with an empty before-state describes no state to return to.
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("names"), namesJson(before)}},
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step puts the track's previous key names back")));
	return ControlResult::success(result);
}

ControlResult setName(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	QHash<int, QString> next = TrackKeyNames::instance().names(track->id());
	const int key = args.value(QStringLiteral("key")).toInt();
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { next.remove(key); } else { next.insert(key, name); }
	return storeNames(track, next);
}

ControlResult preset(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const QString which = args.value(QStringLiteral("preset")).toString();
	return storeNames(track, which == QStringLiteral("none") ? QHash<int, QString>() : TrackKeyNames::generalMidiDrums());
}

ControlResult getNames(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("names"), namesJson(track->id())}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("key_names.") + QLatin1String(verb), description, args, mutating, handler);
}

} // namespace

void registerKeyNameCommands(ControlRegistry& registry)
{
	TrackKeyNames::instance();  // registers the section before any project loads
	add(registry, "get", QStringLiteral("A track's key names ([{key, name}], the engine's keys: 0 = C0, 57 = A4)."),
		objectSchema({{QStringLiteral("track"), stringProperty()}}, {QStringLiteral("track")}), false, getNames);
	add(registry, "set", QStringLiteral("Name key 'key' of a track ('name' empty removes it) - the note editor's keyboard and "
		"drum view show it. Saved with the project; one undo step."),
		objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("key"), integerProperty(0, NumKeys - 1)},
			{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("track"), QStringLiteral("key")}), true, setName);
	add(registry, "preset", QStringLiteral("Give a track the General MIDI drum names ('gm_drums') or none ('none'). One undo step."),
		objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("preset"),
			enumProperty({QStringLiteral("gm_drums"), QStringLiteral("none")})}}, {QStringLiteral("track"), QStringLiteral("preset")}),
		true, preset);
}

} // namespace lmms
