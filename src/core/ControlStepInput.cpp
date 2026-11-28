/*
 * ControlStepInput.cpp - the per-clip step-entry cursor (see include/ControlStepInput.h)
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

#include "ControlStepInput.h"

#include <algorithm>

#include <QHash>

#include "Song.h"  // MaxSongLength

namespace lmms::control
{

namespace
{

/*! clip id -> step cursor in ticks.
 *
 *  A function-local static, so the map is built on the first step entry and never during
 *  static initialisation. It is touched only from the control thread (the command handler and
 *  the note editor), which is why it needs no lock; see the header for why it is not project
 *  state. Entries are one int per clip a session has step-entered - a clip id is never reused,
 *  so the map cannot mis-target a clip made later.
 */
QHash<QString, int>& cursors()
{
	static QHash<QString, int> store;
	return store;
}

} // namespace

int stepInputCursor(const QString& clipId)
{
	return cursors().value(clipId, 0);
}

void setStepInputCursor(const QString& clipId, int ticks)
{
	if (clipId.isEmpty()) { return; }
	cursors().insert(clipId, std::clamp(ticks, 0, static_cast<int>(MaxSongLength)));
}

} // namespace lmms::control
