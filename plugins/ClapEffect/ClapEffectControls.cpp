/*
 * ClapEffectControls.cpp - controls and parameter models for the CLAP effect host
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

#include "ClapEffectControls.h"
#include "Song.h"
#include "Engine.h"

#include <QTimer>

#include "ClapEffect.h"
#include "ClapEffectControlDialog.h"
#include "EffectChain.h"

namespace lmms
{

ClapEffectControls::ClapEffectControls(ClapEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_params(effect->plugin(), this)
{
	m_params.sync();

	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(100);
	connect(m_pollTimer, &QTimer::timeout, this, &ClapEffectControls::poll);
	m_pollTimer->start();
}

auto ClapEffectControls::modelForParam(std::uint32_t id) -> ClapParamModel*
{
	return m_params.modelFor(id);
}

void ClapEffectControls::poll()
{
	auto* plugin = m_effect->plugin();
	if (!plugin->isLoaded()) { return; }

	// The plug-in's requests first: one that asks for a restart (a full parameter rescan, W6) is answered by the
	// re-prepare right after it, in this same poll.
	if (plugin->serviceMainThread() && Engine::getSong() != nullptr) { Engine::getSong()->setModified(); }
	if (m_effect->needsReprepare() || plugin->needsReprepare())
	{
		plugin->clearNeedsReprepare();
		m_effect->reprepare();
	}
	if (m_params.sync())
	{
		emit parametersChanged();
		// The Device panel redraws a card on its chain's dataChanged.
		if (EffectChain* chain = m_effect->effectChain()) { emit chain->dataChanged(); }
	}
	m_params.pullValues(1e-4f);
}

void ClapEffectControls::saveSettings(QDomDocument& doc, QDomElement& element)
{
	QByteArray state;
	m_effect->plugin()->saveState(&state);
	if (!state.isEmpty())
	{
		auto stateElement = doc.createElement(QStringLiteral("state"));
		stateElement.appendChild(doc.createTextNode(QString::fromLatin1(state.toBase64())));
		element.appendChild(stateElement);
	}
	m_params.saveValues(doc, element);
}

void ClapEffectControls::loadSettings(const QDomElement& element)
{
	const auto stateElement = element.firstChildElement(QStringLiteral("state"));
	if (!stateElement.isNull())
	{
		m_effect->plugin()->loadState(QByteArray::fromBase64(stateElement.text().toLatin1()));
	}
	// stress-4: the host's saved values of what the plug-in's state did not bring back (none in a document
	// written before they were saved).
	const bool restored = m_params.restoreValues(element);
	if (stateElement.isNull() && !restored) { return; }

	// reflect the restored state in the models
	m_params.pullValues(0.f);
}

auto ClapEffectControls::createView() -> gui::EffectControlDialog*
{
	return new gui::ClapEffectControlDialog(this);
}

} // namespace lmms
