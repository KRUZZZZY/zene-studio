/*
 * ModulationControls.cpp - the Modulation device's parameters
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
#include "ModulationControls.h"

#include "Modulation.h"

namespace lmms
{

ModulationControls::ModulationControls(ModulationEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_modeModel(this, tr("Mode")),
	m_rateModel(0.5f, 0.05f, 10.f, 0.01f, this, tr("Rate")),
	m_depthModel(0.5f, 0.f, 1.f, 0.01f, this, tr("Depth")),
	m_feedbackModel(0.f, 0.f, 0.9f, 0.01f, this, tr("Feedback")),
	m_spreadModel(90.f, 0.f, 180.f, 1.f, this, tr("Stereo spread"))
{
	m_modeModel.addItem(tr("Chorus"));
	m_modeModel.addItem(tr("Phaser"));
	m_modeModel.addItem(tr("Tremolo"));
}

void ModulationControls::loadSettings(const QDomElement& parent)
{
	m_modeModel.loadSettings(parent, "mode");
	m_rateModel.loadSettings(parent, "rate");
	m_depthModel.loadSettings(parent, "depth");
	m_feedbackModel.loadSettings(parent, "feedback");
	m_spreadModel.loadSettings(parent, "spread");
}

void ModulationControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_modeModel.saveSettings(doc, parent, "mode");
	m_rateModel.saveSettings(doc, parent, "rate");
	m_depthModel.saveSettings(doc, parent, "depth");
	m_feedbackModel.saveSettings(doc, parent, "feedback");
	m_spreadModel.saveSettings(doc, parent, "spread");
}

} // namespace lmms
