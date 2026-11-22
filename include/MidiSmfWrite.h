/*
 * MidiSmfWrite.h - notes as a Standard MIDI File
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

/*! The one Standard MIDI File writer the library saves use (browser.save_clip): type 0, 48 ticks per
 *  quarter note - the song's own grid - at the given tempo. Shared so an arrangement clip's notes and a
 *  launcher pattern's notes cannot drift into two different files for the same music.
 *
 *  A note's start and length are LMMS ticks. A note-off past the tick type's largest value cannot be
 *  written, and a delta time larger than MIDI's four-byte variable-length quantity cannot either: both are
 *  refusals, with the message the command shows, rather than a clamped file that means something else. */
#ifndef LMMS_MIDI_SMF_WRITE_H
#define LMMS_MIDI_SMF_WRITE_H

#include <vector>

#include <QByteArray>
#include <QString>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms::midismf
{

struct Note
{
	tick_t start = 0;
	tick_t length = 1;  //!< at least 1
	int key = 0;        //!< MIDI note number 0..127
	int velocity = 1;   //!< MIDI velocity 1..127
};

struct Result
{
	bool ok = false;
	QByteArray bytes;  //!< the complete file when ok
	QString error;     //!< why nothing was written, when !ok
};

/*! @a notes as a type-0 Standard MIDI File at @a bpm. Notes need not be sorted. Empty @a notes writes the
 *  tempo and end-of-track events only, as a MIDI clip with no notes always did. */
LMMS_EXPORT Result write(const std::vector<Note>& notes, int bpm);

} // namespace lmms::midismf

#endif // LMMS_MIDI_SMF_WRITE_H
