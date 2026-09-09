/*
 * ClapParameter.cpp - an AutomatableModel mapped onto one CLAP parameter
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

#include "ClapParameter.h"

namespace lmms
{

ClapParamModel::ClapParamModel(const ClapParamDescriptor& descriptor, Model* parent) :
	FloatModel(static_cast<float>(descriptor.defaultValue),
		static_cast<float>(descriptor.minValue), static_cast<float>(descriptor.maxValue),
		0.f, parent, descriptor.title),
	m_descriptor(descriptor)
{
	// CLAP has no step count, only a stepped flag, so discrete parameters get a
	// step of 1 when the range is integral and 1 % of the range otherwise.
	const auto range = m_descriptor.maxValue - m_descriptor.minValue;
	setStep(m_descriptor.stepped ? (range >= 1.0 ? 1.f : static_cast<float>(range) / 100.f)
								: static_cast<float>(range) * 0.01f);
}

QString ClapParamModel::displayValue(const float value) const
{
	if (m_formatter)
	{
		return m_formatter(static_cast<double>(value));
	}
	return FloatModel::displayValue(value);
}

} // namespace lmms
