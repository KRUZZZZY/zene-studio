/*
 * CollabCommandTap.h - the seam between the command registry and the
 *                      peer-to-peer collaboration session (0.5.0 F6, WF-050)
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

#ifndef LMMS_COLLAB_COMMAND_TAP_H
#define LMMS_COLLAB_COMMAND_TAP_H

#include <functional>

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms::collab
{

/*! The two calls ControlRegistry makes for the collaboration session - a hook, not a fork of the dispatcher.
 *
 *  Compiled in every configuration (it needs no Qt Network): with no session installed both calls are a
 *  test of an empty std::function, so a build without collaboration, or one that never hosts, pays nothing.
 *  Everything runs on the UI thread, where the registry runs its handlers; nothing here is on an audio path.
 */
class LMMS_EXPORT CommandTap
{
public:
	/*! A joined peer decides whether a command invoked HERE runs here. True means it was handled
	 *  elsewhere - forwarded to the host, or refused - and \a result holds the reply for the caller. */
	using Interceptor = std::function<bool(const QString& id, bool mutating, const QJsonObject& args, ControlResult* result)>;
	//! Told of every top-level command that finished (any client), with whether it changed anything.
	using Observer = std::function<void(const QString& id, const QJsonObject& args, bool mutating, bool changed)>;

	static void install(Interceptor interceptor, Observer observer);
	static void clear();

	//! Registry side, before a handler runs. Never intercepts a nested command or one being applied from the host.
	static bool intercepted(const QString& id, bool mutating, int depth, const QJsonObject& args, ControlResult* result);
	//! Registry side, after a handler ran. Only top-level commands (\a depth 0) reach the observer.
	static void observed(const QString& id, const QJsonObject& args, bool mutating, bool changed, int depth);
};

//! While one is alive the interceptor lets every command through: the peer is applying the host's stream.
class LMMS_EXPORT RemoteApplyScope
{
public:
	RemoteApplyScope();
	~RemoteApplyScope();
	RemoteApplyScope(const RemoteApplyScope&) = delete;
	RemoteApplyScope& operator=(const RemoteApplyScope&) = delete;
	static bool active();
};

} // namespace lmms::collab

#endif // LMMS_COLLAB_COMMAND_TAP_H
