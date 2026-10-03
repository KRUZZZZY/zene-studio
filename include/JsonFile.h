/*
 * JsonFile.h - read and write one JSON object as a file
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

#ifndef LMMS_JSON_FILE_H
#define LMMS_JSON_FILE_H

#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

namespace lmms::jsonfile
{

/*! 0.5.0 W3: the one way the export / import / backup commands (theme.*, settings.*) keep a JSON
 *  document on disk. Control thread. */

//! Write @a doc to @a path atomically (indented); false with @a error when it cannot be written.
LMMS_EXPORT bool write(const QString& path, const QJsonObject& doc, QString* error);

//! The object @a path holds; empty with @a error when it cannot be read or is not a JSON object.
LMMS_EXPORT QJsonObject read(const QString& path, QString* error);

} // namespace lmms::jsonfile

#endif // LMMS_JSON_FILE_H
