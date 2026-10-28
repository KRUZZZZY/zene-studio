/*
 * ConnectionRestoreScope.h - a journal restore states a model's controller connection in full (Q050-331)
 *                                       reversibility contract (0.5.0 F6)
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
#ifndef LMMS_CONNECTION_RESTORE_SCOPE_H
#define LMMS_CONNECTION_RESTORE_SCOPE_H

#include "lmms_export.h"

namespace lmms
{

/*! While one is alive on this thread, AutomatableModel::loadSettings() takes its element as the WHOLE truth about
 *  the model's controller connection: a model whose element carries none loses the one it has. Outside a scope a
 *  load only adds what the element carries, as it always has (a preset loaded onto a mapped knob keeps the
 *  mapping).
 *
 *  JournallingObject::restoreState() - undo, redo, and the chain/track snapshots restored through it - opens one:
 *  a restore must put the object back as it was saved, and before Q050-331 a controller binding a preset load had
 *  added to a parameter the checkpoint reused survived the undo of that load. Never on the audio thread. */
class LMMS_EXPORT ConnectionRestoreScope
{
public:
	ConnectionRestoreScope();
	~ConnectionRestoreScope();
	ConnectionRestoreScope(const ConnectionRestoreScope&) = delete;
	ConnectionRestoreScope& operator=(const ConnectionRestoreScope&) = delete;

	static bool active();

private:
	bool m_previous;
};

} // namespace lmms

#endif // LMMS_CONNECTION_RESTORE_SCOPE_H
