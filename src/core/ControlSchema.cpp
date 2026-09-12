
/*
 * ControlSchema.cpp - the JSON-schema subset the control registry validates
 *                     command arguments with (SPEC A11/A12).
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

// Split out of ControlRegistry.cpp so the registry stays a command table: this
// is the deliberately small schema subset (type, required, properties,
// additionalProperties, minimum, maximum, enum) every registered command's args
// are checked against before its handler runs.

#include "ControlRegistry.h"

#include <cmath>

#include <QJsonValue>

namespace lmms
{

static bool typeMatches(const QJsonValue& value, const QString& type)
{
	if (type == QLatin1String("string")) { return value.isString(); }
	if (type == QLatin1String("number")) { return value.isDouble(); }
	if (type == QLatin1String("integer"))
	{
		return value.isDouble() && std::floor(value.toDouble()) == value.toDouble();
	}
	if (type == QLatin1String("boolean")) { return value.isBool(); }
	if (type == QLatin1String("object")) { return value.isObject(); }
	if (type == QLatin1String("array")) { return value.isArray(); }
	return true; // an unknown type never rejects a value
}

static QString pathLabel(const QString& path)
{
	return path.isEmpty() ? QStringLiteral("args") : path;
}

static QString checkType(const QJsonValue& value, const QJsonObject& spec, const QString& path)
{
	const QString type = spec.value(QStringLiteral("type")).toString();
	if (type.isEmpty() || typeMatches(value, type)) { return QString(); }
	return QStringLiteral("%1: expected %2").arg(pathLabel(path), type);
}

static QString checkRange(const QJsonValue& value, const QJsonObject& spec, const QString& path)
{
	if (!value.isDouble()) { return QString(); }
	const QJsonValue minimum = spec.value(QStringLiteral("minimum"));
	if (minimum.isDouble() && value.toDouble() < minimum.toDouble())
	{
		return QStringLiteral("%1: %2 is below the minimum %3")
			.arg(pathLabel(path), QString::number(value.toDouble()), QString::number(minimum.toDouble()));
	}
	const QJsonValue maximum = spec.value(QStringLiteral("maximum"));
	if (maximum.isDouble() && value.toDouble() > maximum.toDouble())
	{
		return QStringLiteral("%1: %2 is above the maximum %3")
			.arg(pathLabel(path), QString::number(value.toDouble()), QString::number(maximum.toDouble()));
	}
	return QString();
}

static QString checkEnum(const QJsonValue& value, const QJsonObject& spec, const QString& path)
{
	if (!spec.contains(QStringLiteral("enum"))) { return QString(); }
	for (const QJsonValue& allowed : spec.value(QStringLiteral("enum")).toArray())
	{
		if (allowed == value) { return QString(); }
	}
	return QStringLiteral("%1: value not in the allowed set").arg(pathLabel(path));
}

static QString validateValue(const QJsonValue& value, const QJsonObject& spec, const QString& path);

//! Validates the properties present in \p object; empty string means OK.
static QString checkProperties(const QJsonObject& object, const QJsonObject& properties,
	bool noAdditional, const QString& path)
{
	for (auto it = object.begin(); it != object.end(); ++it)
	{
		if (!properties.contains(it.key()))
		{
			if (noAdditional)
			{
				return QStringLiteral("%1: unexpected property '%2'").arg(pathLabel(path), it.key());
			}
			continue;
		}
		const QString subPath = path.isEmpty() ? it.key() : path + QLatin1Char('.') + it.key();
		const QString reason = validateValue(it.value(), properties.value(it.key()).toObject(), subPath);
		if (!reason.isEmpty()) { return reason; }
	}
	return QString();
}

static QString checkObjectMembers(const QJsonValue& value, const QJsonObject& spec, const QString& path)
{
	if (!value.isObject() || !spec.contains(QStringLiteral("properties"))) { return QString(); }
	const QJsonObject object = value.toObject();
	for (const QJsonValue& name : spec.value(QStringLiteral("required")).toArray())
	{
		if (!object.contains(name.toString()))
		{
			return QStringLiteral("%1: missing required property '%2'")
				.arg(pathLabel(path), name.toString());
		}
	}
	const bool noAdditional = spec.contains(QStringLiteral("additionalProperties")) &&
		!spec.value(QStringLiteral("additionalProperties")).toBool(true);
	return checkProperties(object, spec.value(QStringLiteral("properties")).toObject(), noAdditional, path);
}

static QString validateValue(const QJsonValue& value, const QJsonObject& spec, const QString& path)
{
	QString reason = checkType(value, spec, path);
	if (reason.isEmpty()) { reason = checkRange(value, spec, path); }
	if (reason.isEmpty()) { reason = checkEnum(value, spec, path); }
	if (reason.isEmpty()) { reason = checkObjectMembers(value, spec, path); }
	return reason;
}

QString ControlRegistry::validateArgs(const QJsonObject& schema, const QJsonObject& args)
{
	if (schema.isEmpty()) { return QString(); }
	if (!args.isEmpty() || schema.contains(QStringLiteral("required")))
	{
		return validateValue(QJsonValue(args), schema, QString());
	}
	return QString();
}

} // namespace lmms
