/*
 * Vst3EffectControls.cpp - controls and parameter models for the VST3 effect host
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

#include "Vst3EffectControls.h"

#include <cmath>

#include <QTimer>

#include "Vst3Effect.h"
#include "Vst3EffectControlDialog.h"

namespace lmms
{

Vst3EffectControls::Vst3EffectControls(Vst3Effect* effect) :
	EffectControls(effect),
	m_effect(effect)
{
	auto* plugin = m_effect->plugin();
	for (const auto& descriptor : plugin->parameters())
	{
		auto* model = new Vst3ParamModel(descriptor, plugin, this);
		m_paramModels.push_back(model);
		// host -> plug-in: mirror the model value into the lock free snapshot
		// the audio thread reads. Safe from any thread that changes a model.
		QObject::connect(model, &Model::dataChanged, model,
			[plugin, id = descriptor.id, model] {
				plugin->setParamNormalized(id, model->value());
			});
		// plug-in -> host: render the value with the plug-in's own formatter
		model->setFormatter([plugin, id = descriptor.id](float value) {
			return plugin->paramDisplayValue(id, value);
		});
	}

	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(100);
	connect(m_pollTimer, &QTimer::timeout, this, &Vst3EffectControls::poll);
	m_pollTimer->start();
}

auto Vst3EffectControls::modelForParam(std::uint32_t id) -> Vst3ParamModel*
{
	for (auto* model : m_paramModels)
	{
		if (model->paramId() == id) { return model; }
	}
	return nullptr;
}

void Vst3EffectControls::poll()
{
	auto* plugin = m_effect->plugin();
	if (!plugin->isLoaded()) { return; }

	if (m_effect->needsReprepare())
	{
		m_effect->reprepare();
	}

	if (m_syncing) { return; }
	m_syncing = true;
	for (auto* model : m_paramModels)
	{
		const auto id = model->paramId();
		const auto value = plugin->paramNormalized(id);
		if (std::abs(model->value() - value) > 1e-4f)
		{
			model->setValue(value);
		}
		// keep the edit controller's GUI-side mirror in sync (GUI thread only)
		plugin->notifyController(id, model->value());
	}
	m_syncing = false;
}

void Vst3EffectControls::saveSettings(QDomDocument& doc, QDomElement& element)
{
	QByteArray componentState;
	QByteArray controllerState;
	m_effect->plugin()->saveState(&componentState, &controllerState);

	auto appendState = [&doc, &element](const QString& name, const QByteArray& data) {
		if (data.isEmpty()) { return; }
		auto state = doc.createElement(name);
		state.appendChild(doc.createTextNode(QString::fromLatin1(data.toBase64())));
		element.appendChild(state);
	};
	appendState(QStringLiteral("componentstate"), componentState);
	appendState(QStringLiteral("controllerstate"), controllerState);
}

void Vst3EffectControls::loadSettings(const QDomElement& element)
{
	auto readState = [&element](const QString& name) -> QByteArray {
		const auto node = element.firstChildElement(name);
		if (node.isNull()) { return {}; }
		return QByteArray::fromBase64(node.text().toLatin1());
	};

	const auto componentState = readState(QStringLiteral("componentstate"));
	const auto controllerState = readState(QStringLiteral("controllerstate"));
	m_effect->plugin()->loadState(componentState, controllerState);

	// reflect the restored state in the models
	m_syncing = true;
	for (auto* model : m_paramModels)
	{
		model->setValue(m_effect->plugin()->paramNormalized(model->paramId()));
	}
	m_syncing = false;
}

auto Vst3EffectControls::createView() -> gui::EffectControlDialog*
{
	return new gui::Vst3EffectControlDialog(this);
}

} // namespace lmms
