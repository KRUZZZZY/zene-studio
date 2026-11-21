/*
 * ParameterControls.cpp - effect controls whose parameters are listed once: saved, loaded and drawn from the list
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

#include "ParameterControls.h"

#include <algorithm>

#include <QDebug>

#include "AutomatableModel.h"
#include "ParameterGridDialog.h"

namespace lmms
{

ParameterControls::ParameterControls(Effect* effect, const QString& nodeName) :
	EffectControls(effect),
	m_nodeName(nodeName)
{
}

void ParameterControls::addParameter(AutomatableModel* model, const QString& key)
{
	// A key is the model's name in the save file: a second model under it would overwrite the first one's
	// saved value, and a load would give both the same value. The first registration keeps the key.
	const bool taken = std::any_of(m_parameters.begin(), m_parameters.end(),
		[&key](const auto& parameter) { return parameter.second == key; });
	if (taken)
	{
		qWarning("ParameterControls: key '%s' is already registered; the second parameter is neither saved nor loaded",
			qUtf8Printable(key));
		return;
	}
	m_parameters.emplace_back(model, key);
}

void ParameterControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	for (const auto& [model, key] : m_parameters) { model->saveSettings(doc, parent, key); }
}

void ParameterControls::loadSettings(const QDomElement& parent)
{
	for (const auto& [model, key] : m_parameters) { model->loadSettings(parent, key); }
}

gui::EffectControlDialog* ParameterControls::createView()
{
	return new gui::ParameterGridDialog(this);
}

} // namespace lmms
