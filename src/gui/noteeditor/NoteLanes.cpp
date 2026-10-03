/*
 * NoteLanes.cpp - the note editor's per-note expression lanes, as data
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

#include "NoteLanes.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QJsonArray>

#include "Note.h"

namespace lmms::gui::notelanes
{

namespace
{

QString tr(const char* text) { return QCoreApplication::translate("NoteLanes", text); }

//! A one-note note.edit setting @a field.
Write noteEdit(QJsonObject args, const QString& noteId, const QString& field, double value)
{
	args.insert(QStringLiteral("edits"), QJsonArray{QJsonObject{{QStringLiteral("note"), noteId}, {field, value}}});
	return {QStringLiteral("note.edit"), args};
}

} // namespace

QList<NoteLaneSpec> all()
{
	return {
		{NoteLane::Velocity, QStringLiteral("velocity"), tr("Velocity"), 0, 200, 0, 200, QStringLiteral("")},
		{NoteLane::Pan, QStringLiteral("pan"), tr("Pan"), -100, 100, 0, 200, QStringLiteral("")},
		{NoteLane::Chance, QStringLiteral("chance"), tr("Chance"), 0, 100, 0, 100, QStringLiteral("%")},
		{NoteLane::Timbre, QStringLiteral("timbre"), tr("Timbre"), 0, 127, 0, 127, QStringLiteral("")},
		{NoteLane::Pressure, QStringLiteral("pressure"), tr("Pressure"), 0, 127, 0, 127, QStringLiteral("")},
		{NoteLane::Pitch, QStringLiteral("pitch"), tr("Micro-pitch"), -4800, 4800, 0, 400, QStringLiteral("ct")},
		{NoteLane::Release, QStringLiteral("release"), tr("Release velocity"), 0, 200, 0, 200, QStringLiteral("")},
		{NoteLane::Spread, QStringLiteral("spread"), tr("Velocity spread"), 0, 100, 0, 100, QStringLiteral("%")},
		{NoteLane::Repeats, QStringLiteral("repeats"), tr("Repeats"), 0, 16, 0, 16, QStringLiteral("")},
	};
}

NoteLaneSpec spec(NoteLane lane)
{
	for (const NoteLaneSpec& s : all())
	{
		if (s.lane == lane) { return s; }
	}
	return all().front();
}

bool fromId(const QString& id, NoteLane& out)
{
	for (const NoteLaneSpec& s : all())
	{
		if (s.id == id)
		{
			out = s.lane;
			return true;
		}
	}
	return false;
}

int valueOf(const Note& note, NoteLane lane)
{
	switch (lane)
	{
	case NoteLane::Velocity: return static_cast<int>(note.getVolume());
	case NoteLane::Pan: return static_cast<int>(note.getPanning());
	case NoteLane::Chance: return static_cast<int>(std::lround(note.probability() * 100.f));
	case NoteLane::Timbre: return note.mpeTimbre();
	case NoteLane::Pressure: return note.mpePressure();
	case NoteLane::Pitch: return note.mpePitchCents();
	case NoteLane::Release: return std::max(0, note.releaseVelocity());
	case NoteLane::Spread: return static_cast<int>(std::lround(note.velocityJitter() * 100.f));
	case NoteLane::Repeats: return note.conditions().repeats;
	}
	return 0;
}

int clamp(NoteLane lane, int value)
{
	const NoteLaneSpec s = spec(lane);
	return std::clamp(value, s.minimum, s.maximum);
}

Write writeFor(NoteLane lane, const QString& clipId, const QString& noteId, int value)
{
	const int v = clamp(lane, value);
	QJsonObject args{{QStringLiteral("clip"), clipId}};
	switch (lane)
	{
	case NoteLane::Velocity: return noteEdit(args, noteId, QStringLiteral("velocity"), v);
	case NoteLane::Pan: return noteEdit(args, noteId, QStringLiteral("pan"), v);
	case NoteLane::Chance: return noteEdit(args, noteId, QStringLiteral("probability"), v / 100.0);
	case NoteLane::Release: return noteEdit(args, noteId, QStringLiteral("release_velocity"), v);
	case NoteLane::Spread: return noteEdit(args, noteId, QStringLiteral("velocity_spread"), v / 100.0);
	case NoteLane::Repeats: return noteEdit(args, noteId, QStringLiteral("repeats"), v);
	case NoteLane::Timbre: args.insert(QStringLiteral("timbre"), v); break;
	case NoteLane::Pressure: args.insert(QStringLiteral("pressure"), v); break;
	case NoteLane::Pitch: args.insert(QStringLiteral("pitch"), v); break;
	}
	args.insert(QStringLiteral("note"), noteId);
	return {QStringLiteral("note.expression_set"), args};
}

} // namespace lmms::gui::notelanes
