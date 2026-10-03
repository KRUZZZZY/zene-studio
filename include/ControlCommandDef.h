/*
 * ControlCommandDef.h - the one way a command file registers a plain command
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
#ifndef LMMS_CONTROL_COMMAND_DEF_H
#define LMMS_CONTROL_COMMAND_DEF_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms::control
{

//! A command's handler: a plain function of its arguments.
using PlainHandler = ControlResult (*)(const QJsonObject&);

/*! Register the command @a id ("group.verb"; the group and the verb are taken from it) with @a description,
 *  the argument properties @a args (made into a closed object schema, additionalProperties false, with
 *  @a required), an object result, @a mutating and @a handler. The registration every command file used to
 *  carry its own copy of. */
LMMS_EXPORT void defineCommand(ControlRegistry& registry, const QString& id, const QString& description,
	const QJsonObject& args, const QStringList& required, bool mutating, PlainHandler handler);

} // namespace lmms::control

#endif // LMMS_CONTROL_COMMAND_DEF_H
