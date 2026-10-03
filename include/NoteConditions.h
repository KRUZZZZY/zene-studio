/*
 * NoteConditions.h - a note's play conditions and annotations: repeats, recurrence, articulation, group, lyric
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
#ifndef LMMS_NOTE_CONDITIONS_H
#define LMMS_NOTE_CONDITIONS_H

#include <QString>

#include "lmms_export.h"

class QDomElement;

namespace lmms
{

/*! 0.5.0 L07 (W3): what a note carries beyond pitch, time and velocity - plain integers, so the
 *  audio thread copies them with the note and never allocates or frees for them. Each one is
 *  written to the note's element only when it is set, so a note (and a project) that uses none of
 *  them saves byte for byte as before.
 *  - ED-133 repeats: the note is played this many extra times, evenly spaced inside its length;
 *  - ED-133 recurrence: over a pattern of 'recurLength' passes of the loop, the note plays on the
 *    passes whose bit is set in 'recurMask' (length 2, mask 1: the first of every two);
 *  - ED-121 articulation: which of the track's articulations the note asks for (1-based; 0 none);
 *  - ED-069 group: notes sharing a group id are selected and moved together (0 none);
 *  - ED-088 lyric: the syllable sung on the note, interned (lyricText), 0 none. */
struct LMMS_EXPORT NoteConditions
{
	static constexpr int MaxRepeats = 16;
	static constexpr int MaxRecurLength = 16;
	static constexpr int MaxArticulations = 64;

	int repeats = 0;
	int recurLength = 0;
	int recurMask = 0;
	int articulation = 0;
	int group = 0;
	int lyric = 0;

	bool recursOnPass(int pass) const
	{
		return recurLength <= 0 || ((recurMask >> (pass % recurLength)) & 1) != 0;
	}
	bool isNeutral() const;
	bool operator==(const NoteConditions& other) const;

	void save(QDomElement& note) const;
	//! Absent attributes read as neutral (the reset-on-absence rule a journal restore relies on).
	void load(const QDomElement& note);
};

//! The id of @a text in the lyric table (added on first use; "" is 0). UI thread.
LMMS_EXPORT int internLyric(const QString& text);
//! The text of lyric @a id, "" for 0 or an unknown id. UI thread.
LMMS_EXPORT QString lyricText(int id);

} // namespace lmms

#endif // LMMS_NOTE_CONDITIONS_H
