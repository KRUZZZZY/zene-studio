/*
 * MusicalTime.h - how musician-facing positions and lengths read and are typed
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

#ifndef LMMS_MUSICAL_TIME_H
#define LMMS_MUSICAL_TIME_H

#include <QObject>
#include <QStringList>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms::musicaltime
{

/*! 0.5.0 theme 7: the one time format every musician-facing field shows - the header's position, the
 *  Inspector's clip and note fields, the tempo map's positions, the comp bounds. Bars|beats by default;
 *  ticks or time (m:ss.mmm, through the tempo map) are the alternatives. One preference for all of them,
 *  in the config file (ui/timeformat), set by transport.time_format.
 *  - A position reads "bar.beat.sixteenth" (1-based, under the song's meter), and ".tick" after it when it
 *    is not on a sixteenth, so the text typed back is the same tick.
 *  - A length reads "bars.beats.sixteenths" (0-based: "1.0.0" is one bar, "0.1.0" one beat), the same way.
 *  - Typing takes any of them, whatever is shown: "5.2.3" bars|beats; "768 ticks" or "768t"; "0:08.250"
 *    or "8.25s"; "1.5 bars" (a length). A plain number is read in the unit shown (bars, ticks or seconds).
 *  The "sixteenth" is a quarter of the meter's beat, as the header always showed it. Called on the UI
 *  thread only. */
enum class Format
{
	BarsBeats = 0,
	Ticks = 1,
	Time = 2
};

//! Emits changed() whenever the format is set, so an open field can show the new one.
class LMMS_EXPORT FormatSignal : public QObject
{
	Q_OBJECT
signals:
	void changed();
};

//! "bars_beats", "ticks", "time" - the command's enum, in Format order.
LMMS_EXPORT const QStringList& formatNames();
//! "Bars|Beats", "Ticks", "Time" - what a menu shows.
LMMS_EXPORT QString formatLabel(Format format);
LMMS_EXPORT Format currentFormat();
LMMS_EXPORT void setFormat(Format next);
LMMS_EXPORT FormatSignal* formatSignal();

//! Position @a tick (in Time: the time from @a origin to @a origin + @a tick) as @a as reads it.
LMMS_EXPORT QString positionText(tick_t tick, tick_t origin = 0, Format as = currentFormat());
//! The same without the tick remainder - what a running counter shows ("5.1.1" while it plays).
LMMS_EXPORT QString counterText(tick_t tick, Format as = currentFormat());
//! Length @a length (negative: "-" and the length) starting at @a at, as @a as reads it.
LMMS_EXPORT QString lengthText(tick_t length, tick_t at = 0, Format as = currentFormat());
//! "bar.beat" (both 1-based): the short form a position had before theme 7.
LMMS_EXPORT QString barBeatText(tick_t tick);

//! Typed position text into @a tick (relative to @a origin for a time); false for text that is none.
LMMS_EXPORT bool positionFromText(const QString& text, tick_t* tick, tick_t origin = 0, Format as = currentFormat());
//! Typed length text (a leading "-" allowed) into @a length, a length that starts at @a at.
LMMS_EXPORT bool lengthFromText(const QString& text, tick_t* length, tick_t at = 0, Format as = currentFormat());

} // namespace lmms::musicaltime

#endif // LMMS_MUSICAL_TIME_H
