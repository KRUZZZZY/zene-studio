/*
 * LibraryCellMidi.h - a launcher cell's pattern notes, saved into the library
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

#ifndef LMMS_LIBRARY_CELL_MIDI_H
#define LMMS_LIBRARY_CELL_MIDI_H

#include <QString>

#include "lmms_export.h"

namespace lmms
{
class PatternStore;
}

namespace lmms::librarymidi
{

/*! One PatternStore pattern's notes, written to the shared library as a Standard MIDI File.
 *
 *  WHAT IS SAVED. A MIDI launcher cell references a pattern COLUMN in the PatternStore. The
 *  column's music is every MidiClip note on that column, across all of the store's tracks, so
 *  the entry is one type-0 file carrying them all at the song's tempo (Q050-273.2). Inserting
 *  the entry into another cell plays those notes; the source cell's pattern track settings are
 *  playback settings of the cell, not bytes in the file.
 *
 *  ATOMIC. The target is a QSaveFile: only a complete file replaces the destination, so a
 *  refused save leaves no partial file and never replaces the bytes already there.
 *
 *  REAL TIME. Reading notes and writing the file run on the calling (control / model) thread.
 *  No audio-thread code path reaches this header.
 */
class LMMS_EXPORT CellMidi
{
public:
	struct Result
	{
		bool ok = false;
		QString error;  //!< why it was refused, when !ok
		int notes = 0;  //!< notes in the saved file
	};

	/*! Writes @a pattern's notes from @a store to @a target at @a bpm, replacing @a target only
	 *  when the whole file was written. A pattern with no notes is refused: an empty entry is
	 *  not reusable music. */
	static auto save(PatternStore* store, int pattern, int bpm, const QString& target) -> Result;
};

} // namespace lmms::librarymidi

#endif // LMMS_LIBRARY_CELL_MIDI_H
