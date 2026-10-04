/*
 * DeviceAbState.h - which of plugin.ab's two slots a device holds, read without running a command
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

#ifndef LMMS_DEVICE_AB_STATE_H
#define LMMS_DEVICE_AB_STATE_H

#include <QString>

#include "lmms_export.h"

namespace lmms
{
class Effect;
}

namespace lmms::control
{

//! 0.5.0 theme 12: plugin.ab's slots of one device, as its card shows them.
struct AbState
{
	bool onB = false;  //!< the slot heard now is B (else A)
	bool hasA = false;
	bool hasB = false;
};

/*! The A/B slots plugin.ab keeps for @a effect, read in place - no command runs, so nothing is recorded
 *  and a collaboration peer forwards nothing (the device card polls this). False, @a state untouched, when
 *  no slot is stored for this device. Defined in ControlCommandsDeviceCopy.cpp, where the slots live. */
LMMS_EXPORT bool abSlotState(const Effect* effect, AbState* state);

//! The dynamic property plugin.preset_load / plugin.preset_save set on a device: the preset's name, for its
//! card. Interface state only - never saved with the project.
inline const char* presetNameProperty() { return "zenePresetName"; }

} // namespace lmms::control

#endif // LMMS_DEVICE_AB_STATE_H
