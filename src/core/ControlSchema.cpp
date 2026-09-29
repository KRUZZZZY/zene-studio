
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
#include "ControlResultCheck.h"

#include <cmath>
#include <cstdlib>

#include <QFile>
#include <QJsonArray>
#include <QJsonValue>
#include <QMutex>
#include <QSet>

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
	if (type == QLatin1String("null")) { return value.isNull(); }
	return true; // an unknown type never rejects a value
}

static QString pathLabel(const QString& path)
{
	return path.isEmpty() ? QStringLiteral("args") : path;
}

static QString checkType(const QJsonValue& value, const QJsonObject& spec, const QString& path)
{
	// "type" is one name, or - JSON Schema's own form, used by control::nullable() - a
	// list of names any one of which matches.
	const QJsonValue declared = spec.value(QStringLiteral("type"));
	QStringList types;
	for (const QJsonValue& name : declared.isArray() ? declared.toArray() : QJsonArray{declared})
	{
		if (!name.toString().isEmpty()) { types << name.toString(); }
	}
	if (types.isEmpty()) { return QString(); }
	for (const QString& type : types)
	{
		if (typeMatches(value, type)) { return QString(); }
	}
	return QStringLiteral("%1: expected %2").arg(pathLabel(path), types.join(QLatin1Char('|')));
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

namespace control
{

QString resultSchemaViolation(const QJsonObject& schema, const QJsonObject& result)
{
	if (schema.isEmpty()) { return QString(); }
	return validateValue(QJsonValue(result), schema, QStringLiteral("result"));
}

bool resultChecksEnabled()
{
	static const bool enabled = [] {
		const char* value = std::getenv("ZENE_CONTROL_CHECK_RESULTS");
		return value != nullptr && value[0] != '\0' && value[0] != '0';
	}();
	return enabled;
}

//! When ZENE_CONTROL_CHECK_RESULTS names a readable file rather than "1", that
//! file is the grandfather list: one `<command.id><TAB><reason>` per line, `#`
//! comments. Those commands are not checked; every other command is.
static const QSet<QString>& knownViolations()
{
	static const QSet<QString> known = [] {
		QSet<QString> ids;
		QFile file(QString::fromLocal8Bit(qgetenv("ZENE_CONTROL_CHECK_RESULTS")));
		if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) { return ids; }
		while (!file.atEnd())
		{
			const QString line = QString::fromUtf8(file.readLine()).trimmed();
			if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) { continue; }
			ids.insert(line.section(QLatin1Char('\t'), 0, 0).trimmed());
		}
		return ids;
	}();
	return known;
}

/*! R6.3, made measurable: with ZENE_CONTROL_CHECKED_LOG naming a file, every
 *  command id whose successful reply PASSED its schema check is appended to it,
 *  once per process. A whole ctest run with the variable set is then the list
 *  of commands the suite holds to their contract; tests/checked-coverage.py
 *  ratchets it. Control thread only (never the audio thread), so a file append
 *  under a mutex is fine; the variable is unset in every shipped run. */
static void noteChecked(const QString& commandId)
{
	static const QString path = QString::fromLocal8Bit(std::getenv("ZENE_CONTROL_CHECKED_LOG"));
	if (path.isEmpty()) { return; }
	static QMutex mutex;
	static QSet<QString> seen;
	QMutexLocker locker(&mutex);
	if (seen.contains(commandId)) { return; }
	seen.insert(commandId);
	QFile file(path);
	if (file.open(QIODevice::Append | QIODevice::Text)) { file.write((commandId + QLatin1Char('\n')).toUtf8()); }
}

void finishResult(const QString& commandId, const QJsonObject& resultSchema, ControlResult* result)
{
	result->result.remove(QStringLiteral("__transaction"));
	if (!result->ok || !resultChecksEnabled() || knownViolations().contains(commandId)) { return; }
	const QString why = resultSchemaViolation(resultSchema, result->result);
	if (why.isEmpty())
	{
		noteChecked(commandId);
		return;
	}
	// Refused, not a new wire kind: the closed error set is a protocol contract,
	// and this mode exists only in test runs.
	*result = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1 broke its own resultSchema (ZENE_CONTROL_CHECK_RESULTS): %2")
			.arg(commandId, why));
}

} // namespace control

} // namespace lmms
