/*
 * ScriptCommandBridge.h - zene.command(): every registered control command, from Lua
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

#ifndef LMMS_SCRIPT_COMMAND_BRIDGE_H
#define LMMS_SCRIPT_COMMAND_BRIDGE_H

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

struct lua_State;

namespace lmms
{

/*! The scripting spine: one Lua entry point for the whole control surface, so a script can do
 *  whatever an agent on the socket (or an MCP client) can, through the same registry, the same
 *  argument validation, the same journal and the same transaction record - and every command
 *  added later is scriptable the day it is registered, with no binding to write.
 *
 *    local reply, err, kind = zene.command("track.add", {type = "instrument"})
 *    for _, id in ipairs(zene.commands()) do print(id) end
 *
 *  A Lua table becomes the JSON arguments (string keys -> object, 1..n keys -> array); the reply
 *  comes back as a table, or nil plus the error message and kind. The call is applied on the
 *  apply side like every other script write (a queued ScriptDawBindings::Op::RegistryCall that
 *  the script waits for), never on the worker thread.
 *
 *  The sandbox holds (docs/LUA-COMPATIBILITY-POLICY.md §5): a command that would leave it is
 *  refused - running or scheduling a script (re-entry), quitting, the crash and telemetry
 *  uploads, writing settings or the audio device, a network download, writing the user's
 *  customisations (shortcuts, header, saved layouts, macros) or running a macro - and every path argument
 *  (sandboxPaths) is resolved inside the project directory with the same symlink and `..` rule
 *  as zene.projectFile(), or refused. */
namespace ScriptCommandBridge
{

//! One queued call; lives on the worker's stack until the apply side has run it.
struct Request
{
	QString id;
	QJsonObject args;
	ControlResult result = ControlResult::failure(ControlErrorKind::Busy,
		QStringLiteral("the script command queue was full; the command was not run"));
};

//! Install zene.command and zene.commands on @a L (called from ScriptDawBindings::registerAll).
LMMS_EXPORT void registerAll(lua_State* L);

//! Apply side: run @a request through the registry.
LMMS_EXPORT void applyRequest(Request* request);

//! Why @a id may not be called from a script, or empty when it may.
LMMS_EXPORT QString refusalFor(const QString& id);

//! Resolve every path argument of command @a id in @a args inside the project directory, in
//! place; false (with @a error) when one escapes it. A path argument is one whose key names a
//! path (path, file, dir, folder, directory, out, output, destination, project, root, template,
//! or `_<one of those>`) or one the command takes as a path under another name (a per-command
//! table: project.diff's a/b, project.merge's base/ours/theirs/report, ...).
LMMS_EXPORT bool sandboxPaths(const QString& id, QJsonObject& args, QString* error);

//! Whether argument @a key of command @a id is a path, by the rule sandboxPaths applies (the
//! key's name, or the per-command table). The interface's command form uses the same rule to
//! give such an argument a file picker.
LMMS_EXPORT bool isPathArgument(const QString& id, const QString& key);

} // namespace ScriptCommandBridge

} // namespace lmms

#endif // LMMS_SCRIPT_COMMAND_BRIDGE_H
