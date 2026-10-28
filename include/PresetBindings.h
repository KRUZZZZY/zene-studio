/*
 * PresetBindings.h - the controller bindings a track or chain preset carries (Q050-331)
 *                                       reversibility contract (0.5.0 F6)
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

#ifndef LMMS_PRESET_BINDINGS_H
#define LMMS_PRESET_BINDINGS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "lmms_export.h"

class QDomDocument;
class QDomElement;
class QObject;

namespace lmms
{

class Model;
class Track;

/*! A preset's MIDI controller bindings (Q050-331).
 *
 *  The binding itself already travels inside the preset: every parameter writes its own MIDI controller
 *  connection next to its value (AutomatableModel::saveSettings, MidiController::saveSettings - channel, CC,
 *  soft takeover, feedback), and the load reads it back onto THAT parameter of the device the preset just
 *  created. So a binding can only ever land on its own parameter of the loaded device; nothing here re-binds.
 *
 *  What this adds is the MANIFEST: at save, a <zenebindings> list of every binding, named relative to the
 *  device (or track) it belongs to; at load, a reconciliation of that list against what the loaded device
 *  actually carries - restored, or skipped (the parameter no longer exists, so its connection had nowhere to
 *  go and none was made). An older preset has no manifest: it loads as before and reports nothing. */
namespace presetbindings
{

struct Binding
{
	QString target;     //!< the parameter's display path below the root ("Osc 1 volume", "Amplifier>Volume")
	int channel = 0;    //!< MIDI input channel, 1..16
	int controller = 0; //!< MIDI CC number, 0..127
	bool softTakeover = false;
	bool feedback = false;
};

//! Every MIDI-controller binding on a parameter in @a root's object tree, named relative to @a root and
//! prefixed with @a prefix.
LMMS_EXPORT QList<Binding> collect(const Model* root, const QString& prefix = QString());

/*! Every binding a track preset carries: the track's own parameters, its instrument's ("instrument>..."), and
 *  each effect of its chain by position ("effect 2>..."). The instrument and the chain are not object children of
 *  the track, so they are named from their own roots - which is also what makes a name point at THIS track's
 *  device rather than a same-named parameter anywhere else. */
LMMS_EXPORT QList<Binding> collectTrack(Track* track);

//! Writes @a bindings as a <zenebindings> child of @a parent (nothing when the list is empty).
LMMS_EXPORT void write(QDomDocument& doc, QDomElement& parent, const QList<Binding>& bindings);

//! The <zenebindings> list under @a parent; @a present false when it has none (a preset older than Q050-331).
LMMS_EXPORT QList<Binding> read(const QDomElement& parent, bool* present = nullptr);

/*! Reconciles the saved @a bindings with what @a root carries after the load: each saved binding that a
 *  parameter of the loaded device carries now (@a live: same name, channel and CC) goes to @a restored, every other one to
 *  @a skipped with the reason. @a device (an index or id) is copied into each entry when not empty. */
LMMS_EXPORT void reconcile(const QList<Binding>& bindings, const QList<Binding>& live, const QJsonValue& device,
	QJsonArray* restored, QJsonArray* skipped);

//! The report a load command returns: {saved, restored: [...], skipped: [...]}, and a one-line summary.
LMMS_EXPORT QJsonObject report(int saved, const QJsonArray& restored, const QJsonArray& skipped);
LMMS_EXPORT QString summary(const QJsonObject& report);

} // namespace presetbindings
} // namespace lmms

#endif // LMMS_PRESET_BINDINGS_H
