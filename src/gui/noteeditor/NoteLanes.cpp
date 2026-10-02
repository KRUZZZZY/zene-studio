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
	case NoteLane::Velocity:
	case NoteLane::Pan:
	case NoteLane::Chance:
	{
		QJsonObject edit{{QStringLiteral("note"), noteId}};
		if (lane == NoteLane::Velocity) { edit.insert(QStringLiteral("velocity"), v); }
		if (lane == NoteLane::Pan) { edit.insert(QStringLiteral("pan"), v); }
		if (lane == NoteLane::Chance) { edit.insert(QStringLiteral("probability"), v / 100.0); }
		args.insert(QStringLiteral("edits"), QJsonArray{edit});
		return {QStringLiteral("note.edit"), args};
	}
	case NoteLane::Timbre: args.insert(QStringLiteral("timbre"), v); break;
	case NoteLane::Pressure: args.insert(QStringLiteral("pressure"), v); break;
	case NoteLane::Pitch: args.insert(QStringLiteral("pitch"), v); break;
	}
	args.insert(QStringLiteral("note"), noteId);
	return {QStringLiteral("note.expression_set"), args};
}

} // namespace lmms::gui::notelanes
