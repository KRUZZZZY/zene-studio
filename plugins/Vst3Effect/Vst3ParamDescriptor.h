/*
 * Vst3ParamDescriptor.h - SDK independent description of a VST3 parameter
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

#ifndef LMMS_VST3_PARAM_DESCRIPTOR_H
#define LMMS_VST3_PARAM_DESCRIPTOR_H

#include <cstdint>

#include <QString>

namespace lmms
{

//! SDK independent description of a VST3 parameter.
//!
//! Deliberately free of any LMMS model headers: the host itself (Vst3Host.h)
//! and its unit test need the description but must not pull in the GUI model
//! stack.
struct Vst3ParamDescriptor
{
	//! VST3 parameter ID (ParamID), not an array index
	std::uint32_t id = 0;
	QString title;
	QString shortTitle;
	QString units;
	//! Number of steps of a discrete parameter, 0 for continuous parameters
	std::int32_t stepCount = 0;
	//! Default value in the normalized [0, 1] range VST3 uses for parameters
	float defaultNormalized = 0.f;
	bool readOnly = false;
	bool hidden = false;
	bool stepped = false;
	bool bypass = false;
};

} // namespace lmms

#endif // LMMS_VST3_PARAM_DESCRIPTOR_H
