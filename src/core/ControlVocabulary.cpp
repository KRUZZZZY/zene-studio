/*
 * ControlVocabulary.cpp - the one definition of the command surface's shared
 *                         vocabulary (schemas + id grammar).
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
 */

// Why this file exists (2026-09-12). Four separate merge repairs were needed
// because lanes branched from different bases and each re-derived the same small
// helpers. The last one was four schema helpers defined identically in two
// translation units, i.e. a duplicate-symbol link failure. Before that, twelve
// command files each carried a private copy of `schemaObject`/`stringProperty`
// in an anonymous namespace - no link error, but the same drift with a quieter
// failure mode. The vocabulary of the command surface belongs in one place, so
// it is here: one header, one definition, and callers include the header.
//
// ControlSchema.cpp is a DIFFERENT file and stays: it is the args validator the
// registry runs (type/required/properties/additionalProperties/minimum/maximum/
// enum). It declares nothing outward, which is why this vocabulary could not
// simply live there without two unrelated concerns sharing one name.

#include "ControlVocabulary.h"

#include <utility>

namespace lmms
{

namespace control
{

// ---------------------------------------------------------------------------
// the JSON-schema subset
// ---------------------------------------------------------------------------

QJsonObject objectSchema(QJsonObject properties, QJsonArray required)
{
	QJsonObject schema;
	schema.insert(QStringLiteral("type"), QStringLiteral("object"));
	schema.insert(QStringLiteral("properties"), std::move(properties));
	schema.insert(QStringLiteral("required"), std::move(required));
	schema.insert(QStringLiteral("additionalProperties"), false);
	return schema;
}

QJsonObject stringProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
}

QJsonObject booleanProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}};
}

QJsonObject numberProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}};
}

QJsonObject integerProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
}

QJsonObject integerProperty(int minimum, int maximum)
{
	QJsonObject property{{QStringLiteral("type"), QStringLiteral("integer")}};
	property.insert(QStringLiteral("minimum"), minimum);
	property.insert(QStringLiteral("maximum"), maximum);
	return property;
}

QJsonObject arrayProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}};
}

QJsonObject objectProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
}

QJsonObject tickProperty()
{
	return integerProperty(0, 0x7fffffff);
}

// ---------------------------------------------------------------------------
// the id grammar
// ---------------------------------------------------------------------------

QString trackId(int index)
{
	return QStringLiteral("trk-%1").arg(index);
}

QString clipId(int ordinal)
{
	return QStringLiteral("clip-%1").arg(ordinal);
}

QString noteId(int index)
{
	return QStringLiteral("note-%1").arg(index);
}

QString channelId(int index)
{
	return QStringLiteral("ch-%1").arg(index);
}

QString deviceId(int index)
{
	return QStringLiteral("dev-%1").arg(index);
}

QString effectId(int index)
{
	return QStringLiteral("fx-%1").arg(index);
}

int idToIndex(const QString& id, const QString& prefix)
{
	if (!id.startsWith(prefix)) { return -1; }
	bool ok = false;
	const int index = id.mid(prefix.size()).toInt(&ok);
	return ok && index >= 0 ? index : -1;
}

} // namespace control

} // namespace lmms
