/*
 * ControlNoteShared.cpp - the note-editing vocabulary's one definition
 *                         (include/ControlNoteShared.h).
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

#include "ControlNoteShared.h"

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "MidiClip.h"

namespace lmms
{
namespace control
{

bool readScope(const QJsonObject& args, NoteScope* out, ControlResult* error)
{
	const QString scope = args.value(QStringLiteral("scope")).toString();
	if (scope.isEmpty() || scope == QLatin1String("clip")) { *out = NoteScope::Clip; return true; }
	if (scope == QLatin1String("selection")) { *out = NoteScope::Selection; return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'scope' is '%1'; it is 'clip' (the whole clip) or 'selection' (the notes "
			"note.select chose)").arg(scope));
	return false;
}

NoteVector scopeNotes(const MidiClip& clip, NoteScope scope, const QString& clipIdString)
{
	NoteVector notes;
	if (scope == NoteScope::Clip)
	{
		notes.assign(clip.notes().begin(), clip.notes().end());
		return notes;
	}
	for (int index : selectedNoteIndices(clipIdString))
	{
		if (index >= 0 && index < static_cast<int>(clip.notes().size()))
		{
			notes.push_back(clip.notes()[index]);
		}
	}
	return notes;
}

QString scopeName(NoteScope scope)
{
	return scope == NoteScope::Clip ? QStringLiteral("clip") : QStringLiteral("selection");
}

} // namespace control
} // namespace lmms
