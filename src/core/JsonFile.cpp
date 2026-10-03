/*
 * JsonFile.cpp - read and write one JSON object as a file
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

#include "JsonFile.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace lmms::jsonfile
{

bool write(const QString& path, const QJsonObject& doc, QString* error)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(doc).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
	{
		*error = QStringLiteral("cannot write '%1': %2").arg(path, file.errorString());
		return false;
	}
	return true;
}

QJsonObject read(const QString& path, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = QStringLiteral("cannot read '%1': %2").arg(path, file.errorString());
		return {};
	}
	QJsonParseError parse;
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
	if (!doc.isObject())
	{
		*error = QStringLiteral("'%1' is not a JSON object (%2)").arg(path, parse.errorString());
		return {};
	}
	return doc.object();
}

} // namespace lmms::jsonfile
