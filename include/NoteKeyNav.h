/*
 * NoteKeyNav.h - moving between notes with the keyboard, and saying what a note is
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

#ifndef LMMS_GUI_NOTE_KEY_NAV_H
#define LMMS_GUI_NOTE_KEY_NAV_H

#include <QString>

#include "Note.h"
#include "lmms_export.h"

/*! 0.5.0 UX decision, theme 10: the note area, the expression lane and the item list move through a clip's notes
 *  in one order - by start, then pitch (low to high) - and describe a note in one form, so a keyboard or screen-reader
 *  user meets the same note in each. */
namespace lmms::gui::notekeys
{

enum class Step
{
	Next,     //!< the next note in time (a chord's notes low to high)
	Previous, //!< the previous one
	Above,    //!< the nearest higher note sounding with it (the chord's next note up)
	Below,    //!< the nearest lower one
	First,    //!< the clip's first note
	Last      //!< its last
};

//! Whether @a a comes before @a b in that order (start, pitch, then id).
LMMS_EXPORT bool before(const Note* a, const Note* b);

//! The note @a step away from @a from in @a notes, or nullptr when there is none. From no note, Next and Above
//! give the first note and Previous and Below the last.
LMMS_EXPORT const Note* neighbour(const NoteVector& notes, const Note* from, Step step);
//! The selected note of @a notes that comes first in that order (whatever order the clip keeps them in), or nullptr.
LMMS_EXPORT const Note* firstSelected(const NoteVector& notes);

//! "C5, 1.1.1, 0.0.1 long, velocity 100" - pitch, start and length in the time format, velocity; ", muted" when it is.
LMMS_EXPORT QString describe(const Note& note);
//! "12 notes, 2 selected" (or "No notes").
LMMS_EXPORT QString summary(const NoteVector& notes);

} // namespace lmms::gui::notekeys

#endif // LMMS_GUI_NOTE_KEY_NAV_H
