/*
 * NoteKeyNav.cpp - moving between notes with the keyboard, and saying what a note is
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

#include "NoteKeyNav.h"

#include <algorithm>
#include <tuple>

#include <QCoreApplication>

#include "MusicalTime.h"
#include "NoteGrid.h"

namespace lmms::gui::notekeys
{

namespace
{

QString tr(const char* text) { return QCoreApplication::translate("NoteKeyNav", text); }

using Order = std::tuple<int, int, int>;

//! The keyboard's order: start, then pitch, then the note's id (two notes on one key and tick stay apart).
Order orderOf(const Note* note)
{
	return {static_cast<int>(note->pos().getTicks()), note->key(), note->id()};
}

//! The first note in the order after @a from (forward) or before it, or the end note when @a from is null.
const Note* inTime(const NoteVector& notes, const Note* from, bool forward)
{
	const Note* best = nullptr;
	for (const Note* note : notes)
	{
		const bool beyond = from == nullptr || (forward ? orderOf(note) > orderOf(from) : orderOf(note) < orderOf(from));
		const bool closer = best == nullptr || (forward ? orderOf(note) < orderOf(best) : orderOf(note) > orderOf(best));
		if (beyond && closer) { best = note; }
	}
	return best;
}

bool sounding(const Note* a, const Note* b)
{
	const int aStart = static_cast<int>(a->pos().getTicks());
	const int bStart = static_cast<int>(b->pos().getTicks());
	return aStart < bStart + std::max(1, static_cast<int>(b->length().getTicks()))
		&& bStart < aStart + std::max(1, static_cast<int>(a->length().getTicks()));
}

//! The nearest note above (or below) @a from among those sounding with it.
const Note* inPitch(const NoteVector& notes, const Note* from, bool up)
{
	const Note* best = nullptr;
	for (const Note* note : notes)
	{
		if (note == from || !sounding(note, from)) { continue; }
		const bool beyond = up ? note->key() > from->key() : note->key() < from->key();
		const bool closer = best == nullptr || (up ? note->key() < best->key() : note->key() > best->key());
		if (beyond && closer) { best = note; }
	}
	return best;
}

} // namespace

bool before(const Note* a, const Note* b)
{
	return orderOf(a) < orderOf(b);
}

const Note* neighbour(const NoteVector& notes, const Note* from, Step step)
{
	switch (step)
	{
		case Step::Next: return inTime(notes, from, true);
		case Step::Previous: return inTime(notes, from, false);
		case Step::First: return inTime(notes, nullptr, true);
		case Step::Last: return inTime(notes, nullptr, false);
		case Step::Above: return from != nullptr ? inPitch(notes, from, true) : inTime(notes, nullptr, true);
		case Step::Below: return from != nullptr ? inPitch(notes, from, false) : inTime(notes, nullptr, false);
	}
	return nullptr;
}

const Note* firstSelected(const NoteVector& notes)
{
	const Note* first = nullptr;
	for (const Note* note : notes)
	{
		if (note->selected() && (first == nullptr || before(note, first))) { first = note; }
	}
	return first;
}

QString describe(const Note& note)
{
	const auto start = static_cast<tick_t>(note.pos().getTicks());
	QString text = tr("%1, %2, %3 long, velocity %4").arg(NoteGrid::keyName(note.key()), musicaltime::positionText(start),
		musicaltime::lengthText(static_cast<tick_t>(note.length().getTicks()), start)).arg(static_cast<int>(note.getVolume()));
	if (note.muted()) { text += tr(", muted"); }
	return text;
}

QString summary(const NoteVector& notes)
{
	if (notes.empty()) { return tr("No notes"); }
	const auto selected = std::count_if(notes.begin(), notes.end(), [](const Note* note) { return note->selected(); });
	return tr("%1 notes, %2 selected").arg(notes.size()).arg(selected);
}

} // namespace lmms::gui::notekeys
