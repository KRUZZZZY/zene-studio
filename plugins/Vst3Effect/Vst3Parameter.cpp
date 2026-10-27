/*
 * Vst3Parameter.cpp - an AutomatableModel mapped onto one normalized VST3 parameter
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

#include "Vst3Parameter.h"

#include <cmath>

#include <QDomElement>

#include "ParameterReadOnly.h"
#include "Vst3Host.h"

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
	// stress-4: a kIsReadOnly parameter is the plug-in's to set; the control surface reports and refuses it.
	markParameterReadOnly(this, m_descriptor.readOnly);
}

QString Vst3ParamModel::displayValue(const float value) const
{
	if (m_formatter)
	{
		return m_formatter(value);
	}
	return FloatModel::displayValue(value);
}

void restoreSavedParameters(vst3::HostedPlugin& plugin, const QDomElement& element)
{
	const auto parameters = element.firstChildElement(QStringLiteral("parameters"));
	for (auto parameter = parameters.firstChildElement(QStringLiteral("parameter"));
		!parameter.isNull(); parameter = parameter.nextSiblingElement(QStringLiteral("parameter")))
	{
		bool idOk = false;
		bool valueOk = false;
		const auto id = parameter.attribute(QStringLiteral("id")).toUInt(&idOk);
		const float value = parameter.attribute(QStringLiteral("value")).toFloat(&valueOk);
		const int index = idOk ? plugin.paramIndex(id) : -1;
		if (!valueOk || !std::isfinite(value) || index < 0
			|| plugin.parameters()[static_cast<std::size_t>(index)].readOnly)
		{
			continue;
		}
		// The value is written with 9 significant digits, so a float that came back reads back equal.
		if (std::abs(plugin.paramNormalized(id) - value) <= 1e-6f) { continue; }
		plugin.notifyController(id, value);
	}
}

} // namespace lmms
