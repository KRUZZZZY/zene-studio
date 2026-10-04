/*
 * CollabCommandTap.cpp - the seam between the command registry and the
 *                        peer-to-peer collaboration session (0.5.0 F6, WF-050)
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

#include "CollabCommandTap.h"

namespace lmms::collab
{

namespace
{

struct TapState
{
	CommandTap::Interceptor interceptor;
	CommandTap::Observer observer;
	int applying = 0;  //!< RemoteApplyScope nesting (UI thread)
};

TapState& state()
{
	static TapState s;
	return s;
}

} // namespace

void CommandTap::install(Interceptor interceptor, Observer observer)
{
	state().interceptor = std::move(interceptor);
	state().observer = std::move(observer);
}

void CommandTap::clear()
{
	state().interceptor = {};
	state().observer = {};
}

bool CommandTap::intercepted(const QString& id, bool mutating, int depth, const QJsonObject& args, ControlResult* result)
{
	if (depth > 0 || RemoteApplyScope::active() || !state().interceptor) { return false; }
	return state().interceptor(id, mutating, args, result);
}

void CommandTap::observed(const QString& id, const QJsonObject& args, bool mutating, bool changed, int depth)
{
	if (depth == 0 && state().observer) { state().observer(id, args, mutating, changed); }
}

RemoteApplyScope::RemoteApplyScope()
{
	++state().applying;
}

RemoteApplyScope::~RemoteApplyScope()
{
	--state().applying;
}

bool RemoteApplyScope::active()
{
	return state().applying > 0;
}

} // namespace lmms::collab
