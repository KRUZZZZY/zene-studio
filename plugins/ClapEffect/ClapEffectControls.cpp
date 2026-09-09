/*
 * ClapEffectControls.cpp - controls and parameter models for the CLAP effect host
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

#include "ClapEffectControls.h"

#include <cmath>

#include <QTimer>

#include "ClapEffect.h"
#include "ClapEffectControlDialog.h"

namespace lmms
{

ClapEffectControls::ClapEffectControls(ClapEffect* effect) :
	EffectControls(effect),
	m_effect(effect)
{
	auto* plugin = m_effect->plugin();
	for (const auto& descriptor : plugin->parameters())
	{
		auto* model = new ClapParamModel(descriptor, this);
		m_paramModels.push_back(model);
		// host -> plug-in: mirror the model value into the lock free snapshot
		// the audio thread reads. Safe from any thread that changes a model.
		QObject::connect(model, &Model::dataChanged, model,
			[this, plugin, id = descriptor.id, model] {
				// poll() writes plug-in values back into the models; do not
				// echo those back to the plug-in.
				if (m_syncing) { return; }
				plugin->setParamPlain(id, static_cast<double>(model->value()));
			});
		// plug-in -> host: render the value with the plug-in's own formatter
		model->setDisplayFormatter([plugin, id = descriptor.id](double value) {
			return plugin->paramDisplayValue(id, value);
		});
	}

	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(100);
	connect(m_pollTimer, &QTimer::timeout, this, &ClapEffectControls::poll);
	m_pollTimer->start();
}

auto ClapEffectControls::modelForParam(std::uint32_t id) -> ClapParamModel*
{
	for (auto* model : m_paramModels)
	{
		if (model->paramId() == id) { return model; }
	}
	return nullptr;
}

void ClapEffectControls::poll()
{
	auto* plugin = m_effect->plugin();
	if (!plugin->isLoaded()) { return; }

	if (m_effect->needsReprepare() || plugin->needsReprepare())
	{
		plugin->clearNeedsReprepare();
		m_effect->reprepare();
	}
	plugin->takeCallbackRequest();

	if (m_syncing) { return; }
	m_syncing = true;
	for (auto* model : m_paramModels)
	{
		const auto id = model->paramId();
		const auto& descriptor = model->descriptor();
		const auto range = std::max(1e-12, descriptor.maxValue - descriptor.minValue);
		const auto value = static_cast<float>(plugin->paramPlain(id));
		if (std::abs(model->value() - value) > range * 1e-4f)
		{
			model->setValue(value);
		}
	}
	m_syncing = false;
}

void ClapEffectControls::saveSettings(QDomDocument& doc, QDomElement& element)
{
	QByteArray state;
	m_effect->plugin()->saveState(&state);
	if (state.isEmpty()) { return; }

	auto stateElement = doc.createElement(QStringLiteral("state"));
	stateElement.appendChild(doc.createTextNode(QString::fromLatin1(state.toBase64())));
	element.appendChild(stateElement);
}

void ClapEffectControls::loadSettings(const QDomElement& element)
{
	const auto stateElement = element.firstChildElement(QStringLiteral("state"));
	if (stateElement.isNull()) { return; }

	m_effect->plugin()->loadState(QByteArray::fromBase64(stateElement.text().toLatin1()));

	// reflect the restored state in the models
	m_syncing = true;
	for (auto* model : m_paramModels)
	{
		model->setValue(static_cast<float>(m_effect->plugin()->paramPlain(model->paramId())));
	}
	m_syncing = false;
}

auto ClapEffectControls::createView() -> gui::EffectControlDialog*
{
	return new gui::ClapEffectControlDialog(this);
}

} // namespace lmms
