/*
 * Vst3Parameter.cpp - an AutomatableModel mapped onto one normalized VST3 parameter
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

#include "Vst3Parameter.h"

namespace lmms
{

Vst3ParamModel::Vst3ParamModel(const Vst3ParamDescriptor& descriptor, Model* parent)
	: FloatModel(descriptor.defaultNormalized, 0.f, 1.f, 0.f, parent, descriptor.title)
	, m_descriptor(descriptor)
{
	// Discrete parameters get a step size so that wheel and keyboard changes
	// land on the plug-in's own value list; continuous parameters get a
	// conventional 1 % step for the same interactions.
	setStep(m_descriptor.stepped && m_descriptor.stepCount > 0
			? 1.f / static_cast<float>(m_descriptor.stepCount)
			: 0.01f);
}

QString Vst3ParamModel::displayValue(const float value) const
{
	if (m_formatter)
	{
		return m_formatter(value);
	}
	return FloatModel::displayValue(value);
}

} // namespace lmms
