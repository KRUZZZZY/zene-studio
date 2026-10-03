/*
 * MixerOptions.cpp - the project's pan law
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
#include "MixerOptions.h"

#include <algorithm>
#include <cmath>

#include <QDomElement>

namespace lmms
{

MixerOptions& MixerOptions::instance()
{
	static MixerOptions* options = [] {
		auto* made = new MixerOptions();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *options;
}

const QStringList& MixerOptions::panLawNames()
{
	static const QStringList names{QStringLiteral("balance"), QStringLiteral("constant_power"), QStringLiteral("compromise"),
		QStringLiteral("linear")};
	return names;
}

void MixerOptions::gains(PanLaw law, float pan, float* left, float* right)
{
	const float p = std::clamp(pan, -1.f, 1.f);
	const float linearLeft = 0.5f * (1.f - p);
	const float linearRight = 0.5f * (1.f + p);
	const float angle = (p + 1.f) * 0.25f * 3.14159265f;
	const float powerLeft = std::cos(angle);
	const float powerRight = std::sin(angle);
	switch (law)
	{
	case PanLaw::Linear: *left = linearLeft; *right = linearRight; return;
	case PanLaw::Compromise: *left = std::sqrt(linearLeft * powerLeft); *right = std::sqrt(linearRight * powerRight); return;
	case PanLaw::ConstantPower: *left = powerLeft; *right = powerRight; return;
	case PanLaw::Balance: break;
	}
	*left = std::min(1.f, 1.f - p);
	*right = std::min(1.f, 1.f + p);
}

void MixerOptions::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	if (panLaw() != PanLaw::Balance) { element.setAttribute(QStringLiteral("panlaw"), panLawNames().value(static_cast<int>(panLaw()))); }
	if (crossfader() != 0.f) { element.setAttribute(QStringLiteral("crossfader"), static_cast<double>(crossfader())); }
	parent.appendChild(element);
}

void MixerOptions::load(const QDomElement& element)
{
	setPanLaw(static_cast<PanLaw>(std::max(0, static_cast<int>(panLawNames().indexOf(element.attribute(QStringLiteral("panlaw")))))));
	setCrossfader(element.attribute(QStringLiteral("crossfader"), QStringLiteral("0")).toFloat());
}

} // namespace lmms
