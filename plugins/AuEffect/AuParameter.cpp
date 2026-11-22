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

#include "ParameterReadOnly.h"

namespace lmms
{

AuParamModel::AuParamModel(const au::ParamInfo& info, int index, Model* parent) :
	FloatModel(info.defaultValue, info.minimum, info.maximum, (info.maximum - info.minimum) * 0.001f, parent, info.name),
	m_info(info),
	m_index(index)
{
	setObjectName(QStringLiteral("au-%1").arg(info.id));
	// A parameter the unit does not let a host write is reported and refused by the control surface.
	markParameterReadOnly(this, !info.writable);
}

AuParamModelList::AuParamModelList(au::ParamTarget* unit, Model* parent) :
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
		// An automation tick sets the model on the render thread: its signal waits for flushDeferred().
		model->setDeferChangeSignalOnRenderThread(true);
		m_models.push_back(model);
	}
	m_pushed = std::make_unique<std::atomic<float>[]>(m_models.size());
	for (std::size_t i = 0; i < m_models.size(); ++i)
	{
		const float value = m_unit->paramValue(static_cast<int>(i));
		m_models[i]->setValue(value);
		m_pushed[i].store(m_models[i]->value(), std::memory_order_relaxed);
	}
}

void AuParamModelList::pushChangedValues()
{
	for (std::size_t i = 0; i < m_models.size(); ++i)
	{
		const AuParamModel* model = m_models[i];
		const float value = model->value();
		if (!model->info().writable || value == m_pushed[i].load(std::memory_order_relaxed)) { continue; }
		m_unit->setParam(model->index(), value);
		m_pushed[i].store(value, std::memory_order_release);
	}
}

void AuParamModelList::flushDeferred()
{
	for (AuParamModel* model : m_models) { model->flushDeferredChange(); }
}

void AuParamModelList::pullValues(float tolerance)
{
	for (std::size_t i = 0; i < m_models.size(); ++i)
	{
		AuParamModel* model = m_models[i];
		// A host-side change the audio path has not handed over yet: the unit still has the old value.
		if (model->value() != m_pushed[i].load(std::memory_order_acquire)) { continue; }
		const float value = m_unit->paramValue(model->index());
		const float range = model->maxValue() - model->minValue();
		// The model follows the unit; the next block hands the same value back, which changes nothing.
		if (std::fabs(value - model->value()) > tolerance * range) { model->setValue(value); }
	}
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
