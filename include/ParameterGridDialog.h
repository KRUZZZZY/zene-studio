/*
 * ParameterGridDialog.h - a device's control dialog drawn from its parameter list
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

#ifndef LMMS_GUI_PARAMETER_GRID_DIALOG_H
#define LMMS_GUI_PARAMETER_GRID_DIALOG_H

#include "EffectControlDialog.h"
#include "lmms_export.h"

namespace lmms
{

class ParameterControls;

namespace gui
{

//! 0.5.0 L10 (W3): the dialog of a ParameterControls device - its named choices across the top, then a
//! knob per number, four to a row, then its switches; each control bound to the device's own model.
class LMMS_EXPORT ParameterGridDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	explicit ParameterGridDialog(ParameterControls* controls);
	~ParameterGridDialog() override = default;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_PARAMETER_GRID_DIALOG_H
