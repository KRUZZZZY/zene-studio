/*
 * SmfNoteRead.h - the notes of a Standard MIDI File, read defensively
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
/*! 0.5.0 L04 (Q050-031): what a plug-in editor hands the arranger when it drags a generated MIDI file is a
 *  Standard MIDI File, and a file from outside is data to survive, not to trust. readSmfNotes() reads every
 *  note of every track into one list in LMMS' own tick domain (48 ticks per quarter note, whatever the file's
 *  division), and refuses - with the reason, writing nothing anywhere - a file that is not a Standard MIDI
 *  File, is truncated, uses an SMPTE division, has a data byte with its high bit set, or holds no note.
 *  Tempo and metre are not read: a clip carries notes in beats, and tempo is the session's. Pure: no engine,
 *  no file other than the bytes it is given, so a test hands it any bytes. */
#ifndef LMMS_SMF_NOTE_READ_H
#define LMMS_SMF_NOTE_READ_H

#include <vector>

#include <QByteArray>
#include <QString>

#include "lmms_export.h"

namespace lmms::interchange
{

//! The largest file read at all, and the most notes one file may carry: bounds against a hostile or runaway file.
constexpr int SmfNoteMaxBytes = 16 * 1024 * 1024;
constexpr int SmfNoteMaxCount = 200000;

struct SmfNote
{
	long long start = 0;   //!< LMMS ticks from the start of the file
	long long length = 1;  //!< LMMS ticks, at least 1
	int key = 0;           //!< MIDI note number 0..127, which is the engine's own key
	int velocity = 100;    //!< MIDI velocity 1..127
	int channel = 0;       //!< 0..15
};

struct SmfNoteRead
{
	bool ok = false;
	QString error;               //!< why not, when !ok
	int format = 0;
	int trackCount = 0;
	int division = 0;            //!< ticks per quarter note in the file
	std::vector<SmfNote> notes;  //!< sorted by start, then key
	long long endTick = 0;       //!< LMMS ticks: the end of the last note
};

/*! Reads the notes of \a bytes. Note-on with velocity 0 is a note-off. A note never closed is closed at its track's
 *  end; a note-off with no note is ignored. */
LMMS_EXPORT SmfNoteRead readSmfNotes(const QByteArray& bytes);

//! How many elements the open-note queues have moved so far in this process. Closing the oldest open note of a
//! pitch must cost nothing for the notes behind it: a queue whose front removal shuffles its whole tail pays one
//! shuffle per close, which makes reading a track of many overlapping notes of one pitch quadratic. This is the
//! seam SmfNoteReadTest reads (a count, never a clock) to hold the reader linear in the overlap.
LMMS_EXPORT long long smfNoteQueueShiftCount();

} // namespace lmms::interchange

#endif // LMMS_SMF_NOTE_READ_H
