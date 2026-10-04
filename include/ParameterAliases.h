/*
 * ParameterAliases.h - the names a user gives a device's parameters, keyed by each parameter's stable key
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

#ifndef LMMS_PARAMETER_ALIASES_H
#define LMMS_PARAMETER_ALIASES_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;
class DeviceOptions;
class Effect;

/*! 0.5.0 W6: the aliases of a device's parameters (DeviceOptions, MX-097) are keyed by each parameter's STABLE
 *  KEY, not by its display name - a display name is translated, so an alias keyed by it was lost when the project
 *  was opened in another language.
 *
 *  A parameter's key is "id:" and then:
 *   - the name the device gives the model itself (its QObject name: an LV2 or LADSPA port's symbol, a CLAP
 *     parameter's id);
 *   - else the key a ParameterControls device saves it under;
 *   - else "#" and its position in the device's parameter list (controlEffectParameters) - the device's own
 *     construction order, the same in every language and every session of a build;
 *   and "#2", "#3", ... after it when an earlier parameter of the device has the same key (a LADSPA plug-in run
 *   once per channel has a "gain" in each run).
 *
 *  Migration: a project saved before W6 keyed an alias by the display name. That entry is still read by the name,
 *  as before, and adoptLegacyAliases() moves it to the parameter's key; an entry that names no parameter of the
 *  device (a project saved in another language) is kept as it is, so loading it in that language still finds it.
 *  Control and UI thread: everything here allocates. */
namespace parameteraliases
{

//! The keys of @a models, parameters of @a effect (empty for a model that is not one).
LMMS_EXPORT QStringList keysOf(Effect* effect, const QList<AutomatableModel*>& models);

//! The alias of each of @a models in @a options - the device's own options, or a copy being edited - or empty.
LMMS_EXPORT QStringList aliasesOf(Effect* effect, const QList<AutomatableModel*>& models, const DeviceOptions& options);

//! What a card shows for each of @a models: the alias the device's options give it, else its own name.
LMMS_EXPORT QStringList shownNamesOf(Effect* effect, const QList<AutomatableModel*>& models);

//! Gives @a model the alias @a alias in @a options, under its key; an empty alias, or its own name, removes it.
//! The device's legacy entries are adopted first, so a rename never drops another parameter's alias.
LMMS_EXPORT void setAlias(Effect* effect, AutomatableModel* model, const QString& alias, DeviceOptions* options);

//! Moves the entries of @a options keyed by a display name of one of @a effect's parameters to that parameter's
//! key (to each, when several share the name, as the name lookup gave it to each). True when any moved.
LMMS_EXPORT bool adoptLegacyAliases(Effect* effect, DeviceOptions* options);

//! The aliases as the control surface reports them: each parameter's display name and its alias, and an entry
//! that names no current parameter under the key or name it is stored by.
LMMS_EXPORT QJsonObject toJson(Effect* effect, const DeviceOptions& options);

} // namespace parameteraliases

} // namespace lmms

#endif // LMMS_PARAMETER_ALIASES_H
