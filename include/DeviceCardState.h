/*
 * DeviceCardState.h - what a device card says about its device: bypass, A/B, latency, hosting, preset
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

#ifndef LMMS_GUI_DEVICE_CARD_STATE_H
#define LMMS_GUI_DEVICE_CARD_STATE_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{
class Effect;
class InstrumentTrack;
}

namespace lmms::gui::devicestate
{

/*! 0.5.0 theme 12: one device's state as its card shows it - in words, so no part of it is carried by colour alone:
 *  bypassed, which A/B slot is heard (only once one is stored: plugin.ab), the delay it adds (the compensated latency,
 *  in samples and milliseconds), whether its out-of-process host crashed or was quarantined after repeated crashes
 *  (oop.get_state), and the preset it last loaded or saved. Read on the interface thread from the device itself and
 *  from read-only reports; nothing here runs a mutating command. */
struct State
{
	bool effect = false;    //!< an effect (an instrument has no bypass, A/B or latency of its own here)
	bool bypassed = false;
	bool processing = false;
	bool abDefined = false; //!< a slot is stored, so A/B means something
	bool onB = false;
	bool hasA = false;
	bool hasB = false;
	int latencyFrames = 0;
	QString hosting;        //!< "" (fine), "crashed" or "quarantined"
	QString preset;
};

//! The hosting problems oop.get_state reports, by "<target>|<device>": "crashed" or "quarantined". One read for a panel.
LMMS_EXPORT QHash<QString, QString> hostingProblems();
//! The key hostingProblems() uses for @a plugin (fx-<n> or inst) on @a target.
LMMS_EXPORT QString hostingKey(const QString& target, const QString& plugin);

LMMS_EXPORT State ofEffect(Effect* effect, const QString& hosting);
LMMS_EXPORT State ofInstrument(const InstrumentTrack* track, const QString& hosting);

//! "512 samples (10.7 ms)" at the engine's rate.
LMMS_EXPORT QString latencyText(int frames);
//! The card's state line, its parts in a fixed order: "Bypassed", "Hearing B (A and B stored)", "Latency ...",
//! "Crashed", "Preset: Warm" - "On" for an effect with nothing to report, empty for an instrument with none.
LMMS_EXPORT QStringList parts(const State& state);
LMMS_EXPORT QString text(const State& state);
//! window.get_state's per-card `state`: {bypassed, ab, latency_frames, hosting, preset}.
LMMS_EXPORT QJsonObject json(const State& state);

} // namespace lmms::gui::devicestate

#endif // LMMS_GUI_DEVICE_CARD_STATE_H
