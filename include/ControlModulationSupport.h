/*
 * ControlModulationSupport.h - the helpers the modulator.* command group and the
 *                              note.expression.* group share (SPEC A11-A16).
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
 */

#ifndef LMMS_CONTROL_MODULATION_SUPPORT_H
#define LMMS_CONTROL_MODULATION_SUPPORT_H

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ModulationLayer.h"
#include "lmms_export.h"

namespace lmms
{

//! The publisher every modulator.* handler edits: the Song owns exactly one.
LMMS_EXPORT ModulationLayerPublisher& songModulationLayer();

//! The model a route names, or nullptr with @a why set. Reuses the rack lane's
//! own resolver, so a modulator route and a rack macro target cannot disagree
//! about what a parameter name means. Control thread only.
LMMS_EXPORT AutomatableModel* modulationTargetModel(const ModulationRoute& route, QString* why);

//! "modulator-<n>" / the index a "modulator-<n>" id names; -1 with @a error set.
LMMS_EXPORT QString modulatorId(int index);
LMMS_EXPORT int resolveModulatorIndex(const ModulationLayer& layer, const QString& id,
	ControlResult* error);

//! One source as get_state and the rate_set result report it.
LMMS_EXPORT QJsonObject modulationSourceJson(const ModulatorSource& source);
//! One route: its address, its depth, and whether it still resolves - so a
//! dead route is visible through get_state instead of silently skipped.
LMMS_EXPORT QJsonObject modulationRouteJson(const ModulationRoute& route, int index);
//! One modulator with every route.
LMMS_EXPORT QJsonObject modulatorJson(const Modulator& modulator, int index);
//! The whole layer, as modulator.get_state reports it.
LMMS_EXPORT QJsonObject modulationLayerJson(const ModulationLayer& layer);

//! The source a rate_set call describes, starting from @a current so an
//! argument the call omits keeps the value it already had.
LMMS_EXPORT ModulatorSource modulationSourceFromArgs(const QJsonObject& args,
	const ModulatorSource& current, bool* shapeGiven, bool* valid);
//! The route a target_set call describes.
LMMS_EXPORT ModulationRoute modulationRouteFromArgs(const QJsonObject& args);

} // namespace lmms

#endif // LMMS_CONTROL_MODULATION_SUPPORT_H
