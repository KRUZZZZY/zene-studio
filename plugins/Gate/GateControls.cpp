/*
 * GateControls.cpp - the Gate's parameters
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
#include "GateControls.h"

#include "Gate.h"

namespace lmms
{

GateControls::GateControls(GateEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_modeModel(this, tr("Mode")),
	m_thresholdModel(-40.f, -80.f, 0.f, 0.1f, this, tr("Threshold")),
	m_rangeModel(-60.f, -80.f, 0.f, 0.1f, this, tr("Range")),
	m_ratioModel(2.f, 1.f, 8.f, 0.01f, this, tr("Ratio")),
	m_attackModel(1.f, 0.1f, 50.f, 0.1f, this, tr("Attack")),
	m_holdModel(50.f, 0.f, 500.f, 1.f, this, tr("Hold")),
	m_releaseModel(150.f, 5.f, 2000.f, 1.f, this, tr("Release"))
{
	m_modeModel.addItem(tr("Gate"));
	m_modeModel.addItem(tr("Expander"));
	m_modeModel.addItem(tr("Duck"));
}

void GateControls::loadSettings(const QDomElement& parent)
{
	m_modeModel.loadSettings(parent, "mode");
	m_thresholdModel.loadSettings(parent, "threshold");
	m_rangeModel.loadSettings(parent, "range");
	m_ratioModel.loadSettings(parent, "ratio");
	m_attackModel.loadSettings(parent, "attack");
	m_holdModel.loadSettings(parent, "hold");
	m_releaseModel.loadSettings(parent, "release");
}

void GateControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_modeModel.saveSettings(doc, parent, "mode");
	m_thresholdModel.saveSettings(doc, parent, "threshold");
	m_rangeModel.saveSettings(doc, parent, "range");
	m_ratioModel.saveSettings(doc, parent, "ratio");
	m_attackModel.saveSettings(doc, parent, "attack");
	m_holdModel.saveSettings(doc, parent, "hold");
	m_releaseModel.saveSettings(doc, parent, "release");
}

} // namespace lmms
