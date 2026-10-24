/*
 * ControlDeviceTools.h - what the 0.5.0 W3 device command files share
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

#ifndef LMMS_CONTROL_DEVICE_TOOLS_H
#define LMMS_CONTROL_DEVICE_TOOLS_H

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlRegistry.h"

namespace lmms
{
class AutomatableModel;
class Effect;
}

namespace lmms::control
{

struct AbSlotsSnapshot;

//! Registers one command of the 0.5.0 W3 device files (L10): group and verb from the dotted @a id,
//! the args schema built from @a args / @a required (additionalProperties false), a free-form
//! object result. Defined in ControlCommandsDeviceTools.cpp - the one copy for these files.
void addDeviceCommand(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args,
	const QStringList& required, bool mutating, ControlResult (*handler)(const QJsonObject&));

//! Write each (model, value) of @a writes as ONE undo step: a journal checkpoint of every model
//! first, then the values (each clamped to its model's range). A write that leaves its model at the
//! value it already has is no change: it is neither recorded nor written. Returns how many models
//! changed (0 means no undo step was taken). Defined in ControlCommandsDeviceTools.cpp.
int writeModelsRecorded(const std::vector<std::pair<AutomatableModel*, float>>& writes);

//! 0.5.0 L10 (MX-093): the parameter values plugin.ab stored in slots A and B of @a effect, in
//! plugin.param_get order; false unless both slots are stored for this device. Defined in
//! ControlCommandsDeviceCopy.cpp, where the slots live.
bool abSlotValues(Effect* effect, std::vector<float>* a, std::vector<float>* b);

//! Number of retained A/B slot records (one per device id that holds or held slots); DeviceCopyTest uses it to
//! check that a destroyed device's record is released. Defined in ControlCommandsDeviceCopy.cpp.
std::size_t abSlotEntryCountForTesting();

//! Keep a device's session A/B state across a recorded removal and restore it on the rebuilt effect.
std::shared_ptr<AbSlotsSnapshot> captureAbSlotsForRemoval(Effect* effect);
void restoreAbSlotsAfterRemoval(Effect* effect, const std::shared_ptr<AbSlotsSnapshot>& snapshot);
std::size_t abSlotsSnapshotSize(const std::shared_ptr<AbSlotsSnapshot>& snapshot);

} // namespace lmms::control

#endif // LMMS_CONTROL_DEVICE_TOOLS_H
