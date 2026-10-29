/*
 * ConsoleStrip.h - the mixer strip's Console section: trim, polarity, EQ, and the channel's Console drive and glue
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

#ifndef LMMS_CONSOLE_STRIP_H
#define LMMS_CONSOLE_STRIP_H

#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;
class Effect;
class MixerChannel;

/*! Q050-088: what the mixer strip's optional Console section edits on ONE channel - the strip's own
 *  trim, polarity and three-band EQ (MixerStrip.h, the models mixer.set_strip and mixer.channel_eq
 *  write) and, when the channel carries a Console device (mixer.console_apply puts one there), that
 *  instance's drive and its glue compressor (plugins/Console). mixer.console_strip writes and reads
 *  them as one undo step; the strip's section and the agent take that one command. Nothing here runs
 *  on the audio thread: the settings are the models the DSP already reads. */
namespace consolestrip
{

struct Settings
{
	float trim = 0.f;
	bool polarityLeft = false;
	bool polarityRight = false;
	float eqLow = 0.f;
	float eqMid = 0.f;
	float eqMidFreq = 1000.f;
	float eqHigh = 0.f;
	bool hasConsole = false;  //!< the channel carries a Console; the five below are its values
	float drive = 30.f;
	bool glue = false;
	float glueThreshold = -10.f;
	float glueRatio = 4.f;
	float glueMakeup = 0.f;
	bool operator==(const Settings&) const = default;
};

//! The first Console device in @a channel's chain, or nullptr.
LMMS_EXPORT Effect* consoleOf(const MixerChannel& channel);
//! The model a ParameterControls device saves under @a key, or nullptr.
LMMS_EXPORT AutomatableModel* parameterByKey(Effect* effect, const QString& key);
LMMS_EXPORT Settings read(const MixerChannel& channel);
//! Write @a settings onto @a channel - the Console values only while it carries one.
LMMS_EXPORT void apply(MixerChannel& channel, const Settings& settings);
//! The section as mixer.console_strip answers it; the Console fields are null without a Console.
LMMS_EXPORT QJsonObject toJson(const Settings& settings);
/*! @a args (mixer.console_strip's field names) over @a settings. False, with @a error, for a value out of
 *  range or of the wrong type, and @a refused true when it names a Console field on a channel without
 *  one; nothing is written either way (the caller writes only a merge that succeeded). */
LMMS_EXPORT bool merge(const QJsonObject& args, Settings* settings, QString* error, bool* refused);

} // namespace consolestrip

} // namespace lmms

#endif // LMMS_CONSOLE_STRIP_H
