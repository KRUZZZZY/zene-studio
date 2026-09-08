/*
 * ClapParamDescriptor.h - SDK independent description of a CLAP parameter
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

#ifndef LMMS_CLAP_PARAM_DESCRIPTOR_H
#define LMMS_CLAP_PARAM_DESCRIPTOR_H

#include <cstdint>

#include <QString>

namespace lmms
{

//! SDK independent description of a CLAP parameter.
//!
//! Unlike VST3, CLAP parameters are plain doubles over a plug-in defined
//! [minValue, maxValue] range, so the model below maps that range directly.
struct ClapParamDescriptor
{
	//! CLAP parameter id, not an array index
	std::uint32_t id = 0;
	QString title;
	QString module;
	double minValue = 0.0;
	double maxValue = 1.0;
	double defaultValue = 0.0;
	bool stepped = false;
	bool periodic = false;
	bool readOnly = false;
	bool bypass = false;
	bool automatable = false;
	bool hidden = false;
	//! The plug-in only applies the value inside process(), so the host must
	//! deliver the change as an event instead of assuming get_value reflects it.
	bool requiresProcess = false;
	//! defaultValue mapped onto [0, 1] for display convenience.
	double defaultNormalized = 0.0;
};

} // namespace lmms

#endif // LMMS_CLAP_PARAM_DESCRIPTOR_H
