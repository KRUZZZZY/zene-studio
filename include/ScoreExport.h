/*
 * ScoreExport.h - a MIDI clip written out as a MusicXML score
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
#ifndef LMMS_SCORE_EXPORT_H
#define LMMS_SCORE_EXPORT_H

#include <QString>

#include "lmms_export.h"

namespace lmms
{

class MidiClip;

/*! 0.5.0 L07 (ED-086, the export half): @a clip as a MusicXML 4.0 partwise score - one part, in the time
 *  signature @a numerator / @a denominator. Notes and rests are quantised to a 64th (3 ticks; divisions are the
 *  engine's 48 ticks a quarter, so a duration is its tick count), notes starting together with one length become
 *  a chord, an earlier note is cut where the next one of its voice starts, and a value no single note can show
 *  is tied from standard ones, across bar lines too. Muted notes are left out.
 *
 *  Voices (W6): voice 1 holds, at every start, the shortest of the notes starting there, with rests in its gaps -
 *  for notes that share their lengths, every note, as before. Notes that start with shorter ones but last longer
 *  go to voice 2, 3, ... by length, written after a <backup> to the bar line with <forward> over their gaps, so
 *  every note keeps its own duration. */
LMMS_EXPORT QString musicXmlOf(const MidiClip& clip, int numerator, int denominator, const QString& title);

} // namespace lmms

#endif // LMMS_SCORE_EXPORT_H
