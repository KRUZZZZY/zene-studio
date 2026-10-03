/*
 * RecordOptions.h - count-in, pre-roll and the record mode
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
#ifndef LMMS_RECORD_OPTIONS_H
#define LMMS_RECORD_OPTIONS_H

#include <QJsonObject>
#include <QStringList>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Song;

/*! 0.5.0 L03: how a recording pass starts and what it does to what is there, kept in the config file
 *  (recording/countin, recording/preroll, recording/mode) - the user's preference, not the project:
 *  - WF-080 count-in and pre-roll: the transport starts 'count_in' + 'pre_roll' bars before the play
 *    position, the metronome clicks through the count-in bars whether or not it is on, and audio is
 *    captured only from the play position (the punch gate opened there, unless a punch region is
 *    armed - then the region decides). Stopping puts the punch region back as the timeline has it.
 *  - WF-082 record mode: overdub keeps the notes a MIDI pass plays over; replace removes the notes
 *    already in the clip that a recorded note overlaps. (An audio pass always lands as its own take.)
 *  beginPass / endPass run on the UI thread around Song::playAndRecord and Song::stop;
 *  countingIn() is read by the metronome on the render thread (one relaxed atomic). */
namespace recordoptions
{

enum class Mode
{
	Overdub = 0,
	Replace = 1
};

struct Options
{
	int countInBars = 0;  //!< 0..4
	int preRollBars = 0;  //!< 0..8
	Mode mode = Mode::Overdub;
};

LMMS_EXPORT const QStringList& modeNames();
LMMS_EXPORT Options options();
LMMS_EXPORT void setOptions(const Options& next);
LMMS_EXPORT QJsonObject toJson(const Options& options);

//! Before a recording pass starts: seek back and open the capture gate at the play position. Answers
//! that position (where Stop returns to), or -1 when there is no count-in or pre-roll.
LMMS_EXPORT tick_t beginPass(Song* song);
//! After the transport stops: the timeline's own punch region back on the gate, the count-in over.
LMMS_EXPORT void endPass(Song* song);
//! Whether @a tick lies in the count-in of the pass running now (render thread; lock-free).
LMMS_EXPORT bool countingIn(tick_t tick);

} // namespace recordoptions

} // namespace lmms

#endif // LMMS_RECORD_OPTIONS_H
