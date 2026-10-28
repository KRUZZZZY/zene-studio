/*
 * AuParameter.cpp - an Audio Unit's parameters as LMMS models (Q050-491, macOS only)
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

#include "AuParameter.h"

#include <cmath>

#include <QDebug>

#include "AudioEngine.h"
#include "Engine.h"
#include "ParameterReadOnly.h"

namespace lmms
{

void saveAuState(au::HostedUnit* unit, QDomDocument& doc, QDomElement& element)
{
	QByteArray state;
	if (!unit->saveState(&state)) { return; }
	auto stateElement = doc.createElement(QStringLiteral("state"));
	stateElement.appendChild(doc.createTextNode(QString::fromLatin1(state.toBase64())));
	element.appendChild(stateElement);
}

bool loadAuState(au::HostedUnit* unit, const QDomElement& element)
{
	const auto stateElement = element.firstChildElement(QStringLiteral("state"));
	if (stateElement.isNull() || !unit->isLoaded()) { return false; }
	AudioEngine* engine = Engine::audioEngine();
	// Not while the unit renders: the engine is held for the property write.
	if (engine != nullptr) { engine->requestChangeInModel(); }
	QString error;
	const bool loaded = unit->loadState(QByteArray::fromBase64(stateElement.text().toLatin1()), &error);
	if (engine != nullptr) { engine->doneChangeInModel(); }
	if (!loaded) { qWarning() << "AU:" << error; }
	return loaded;
}

AuParamModel::AuParamModel(const au::ParamInfo& info, int index, Model* parent) :
	FloatModel(info.defaultValue, info.minimum, info.maximum, (info.maximum - info.minimum) * 0.001f, parent, info.name),
	m_info(info),
	m_index(index)
{
	setObjectName(QStringLiteral("au-%1").arg(info.id));
	// A parameter the unit does not let a host write is reported and refused by the control surface.
	markParameterReadOnly(this, !info.writable);
}

AuParamModelList::AuParamModelList(au::HostedUnit* unit, Model* parent) :
	m_unit(unit),
	m_parent(parent)
{
}

void AuParamModelList::build()
{
	if (!m_models.empty() || !m_unit->isLoaded()) { return; }
	const auto& params = m_unit->parameters();
	for (int i = 0; i < static_cast<int>(params.size()); ++i)
	{
		auto* model = new AuParamModel(params[static_cast<std::size_t>(i)], i, m_parent);
		QObject::connect(model, &Model::dataChanged, model, [this, model] {
			if (!m_syncing) { m_unit->setParam(model->index(), model->value()); }
		}, Qt::DirectConnection);
		m_models.push_back(model);
	}
	pullValues(0.f);
}

void AuParamModelList::pullValues(float tolerance)
{
	m_syncing = true;
	for (AuParamModel* model : m_models)
	{
		const float value = m_unit->paramValue(model->index());
		const float range = model->maxValue() - model->minValue();
		if (std::fabs(value - model->value()) > tolerance * range) { model->setValue(value); }
	}
	m_syncing = false;
}

void AuParamModelList::saveValues(QDomDocument& doc, QDomElement& element)
{
	auto parameters = doc.createElement(QStringLiteral("parameters"));
	for (AuParamModel* model : m_models) { model->saveSettings(doc, parameters, model->objectName()); }
	element.appendChild(parameters);
}

void AuParamModelList::restoreValues(const QDomElement& element)
{
	const auto parameters = element.firstChildElement(QStringLiteral("parameters"));
	if (parameters.isNull()) { return; }
	// Through the models, so the automation links come back with them; each value that differs is sent on.
	// A parameter the document does not carry (a newer version of the unit) keeps the unit's restored value:
	// loadSettings() would reset it to its default.
	for (AuParamModel* model : m_models)
	{
		const QString name = model->objectName();
		if (parameters.hasAttribute(name) || !parameters.firstChildElement(name).isNull())
		{
			model->loadSettings(parameters, name);
		}
	}
}

} // namespace lmms
