/*
 * MetronomeOptions.h - the click's level, accent, sound and when it sounds
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
#ifndef LMMS_METRONOME_OPTIONS_H
#define LMMS_METRONOME_OPTIONS_H

#include <cstddef>

#include <QJsonObject>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

class PlayHandle;

/*! 0.5.0 L03 (WF-081): how the metronome clicks - its level, whether the bar's first beat is accented,
 *  which of the built-in sounds it uses, and whether it clicks only while recording. Preferences (the
 *  config file, never the project); the audio thread reads them as atomics at each click. */
namespace metronomeoptions
{

struct Options
{
	float level = 1.f;        //!< 0..2, the click's amplification
	bool accent = true;       //!< the bar's first beat uses the accented sound
	bool recordOnly = false;  //!< click only while recording (a count-in clicks either way)
	int sound = 0;            //!< index into soundNames()
};

LMMS_EXPORT const QStringList& soundNames();
LMMS_EXPORT Options options();
//! Store @a next (and the config file).
LMMS_EXPORT void setOptions(const Options& next);
LMMS_EXPORT QJsonObject toJson(const Options& options);

//! Audio thread: whether a click sounds now (@a forced: a count-in).
LMMS_EXPORT bool sounds(bool forced, bool recording);
//! Audio thread: the play handle of one click - the accented sound for @a downbeat - at @a offset.
LMMS_EXPORT PlayHandle* click(bool downbeat, std::size_t offset);

} // namespace metronomeoptions

} // namespace lmms

#endif // LMMS_METRONOME_OPTIONS_H
