/*
 * ClapEffectControlDialog.h - parameter view for the CLAP effect host
 *
 * Copyright (c) 2026 LMMS contributors
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

#ifndef LMMS_CLAP_EFFECT_CONTROL_DIALOG_H
#define LMMS_CLAP_EFFECT_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

namespace lmms
{

class ClapEffectControls;

namespace gui
{

//! One knob per visible CLAP parameter. Values are rendered with the plug-in's
//! own value-to-string conversion.
class ClapEffectControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	explicit ClapEffectControlDialog(ClapEffectControls* controls);
};

} // namespace gui

} // namespace lmms

#endif // LMMS_CLAP_EFFECT_CONTROL_DIALOG_H
