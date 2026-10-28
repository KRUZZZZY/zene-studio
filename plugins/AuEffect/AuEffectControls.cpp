/*
 * AuEffectControls.cpp - an Audio Unit effect's parameters and saved state (Q050-491, macOS only)
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
 */

#include "AuEffectControls.h"

#include <QTimer>

#include "AuEffect.h"
#include "AuEffectControlDialog.h"

namespace lmms
{

AuEffectControls::AuEffectControls(AuEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_params(effect->unit(), this)
{
	m_params.build();
	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(100);
	connect(m_pollTimer, &QTimer::timeout, this, &AuEffectControls::poll);
	m_pollTimer->start();
}

void AuEffectControls::poll()
{
	if (!m_effect->unit()->isLoaded()) { return; }
	if (m_effect->needsReprepare()) { m_effect->reprepare(); }
	m_params.pullValues(1e-4f);
}

void AuEffectControls::saveSettings(QDomDocument& doc, QDomElement& element)
{
	saveAuState(m_effect->unit(), doc, element);
	m_params.saveValues(doc, element);
}

void AuEffectControls::loadSettings(const QDomElement& element)
{
	loadAuState(m_effect->unit(), element);
	m_params.pullValues(0.f);         // the restored state into the models
	m_params.restoreValues(element);  // then the host's saved values and automation links
}

auto AuEffectControls::createView() -> gui::EffectControlDialog*
{
	return new gui::AuEffectControlDialog(this);
}

} // namespace lmms
