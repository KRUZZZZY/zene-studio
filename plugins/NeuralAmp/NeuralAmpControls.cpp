/*
 * NeuralAmpControls.cpp - controls for the neural amp effect
 *
 * Copyright (c) 2026 AI-KOS Team
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

#include "NeuralAmpControls.h"
#include "NeuralAmpEffect.h"

namespace lmms
{

NeuralAmpControls::NeuralAmpControls(NeuralAmpEffect* effect) :
	EffectControls(effect),
	m_effect(effect)
{
}


void NeuralAmpControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	parent.setAttribute("model", m_modelPath);
}


void NeuralAmpControls::loadSettings(const QDomElement& parent)
{
	m_modelPath = parent.attribute("model", QString());
	if (!m_modelPath.isEmpty())
	{
		m_effect->setModelPath(m_modelPath);
	}
}


QString NeuralAmpControls::modelPath() const
{
	return m_modelPath;
}


void NeuralAmpControls::setModelPath(const QString& path)
{
	m_modelPath = path;
	m_effect->setModelPath(path);
}

}  // namespace lmms
