/*
 * ClapParameter.cpp - an AutomatableModel mapped onto one CLAP parameter, and the list of them
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

#include "ClapParameter.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "AudioEngine.h"
#include "ClapHost.h"
#include "Engine.h"
#include "ParameterReadOnly.h"

namespace lmms
{

namespace
{

//! CLAP has no step count, only a stepped flag, so discrete parameters get a step of 1 when the range
//! is integral and 1 % of the range otherwise.
auto stepOf(const ClapParamDescriptor& descriptor) -> float
{
	const auto range = descriptor.maxValue - descriptor.minValue;
	return descriptor.stepped ? (range >= 1.0 ? 1.f : static_cast<float>(range) / 100.f) : static_cast<float>(range) * 0.01f;
}

} // namespace

ClapParamModel::ClapParamModel(const ClapParamDescriptor& descriptor, Model* parent) :
	FloatModel(static_cast<float>(descriptor.defaultValue),
		static_cast<float>(descriptor.minValue), static_cast<float>(descriptor.maxValue),
		0.f, parent, descriptor.title),
	m_descriptor(descriptor)
{
	setStep(stepOf(m_descriptor));
	// W6: the parameter's own id as the model's name - the stable key its alias is kept by (ParameterAliases.h),
	// which a rename or a translation of its title does not move.
	setObjectName(QStringLiteral("clap-%1").arg(descriptor.id));
	// stress-4: a CLAP_PARAM_IS_READONLY parameter is the plug-in's to set; the control surface reports and refuses it.
	markParameterReadOnly(this, m_descriptor.readOnly);
}

void ClapParamModel::setDescriptor(const ClapParamDescriptor& descriptor)
{
	const bool sameRange = descriptor.minValue == m_descriptor.minValue && descriptor.maxValue == m_descriptor.maxValue
		&& descriptor.stepped == m_descriptor.stepped;
	m_descriptor = descriptor;
	setDisplayName(descriptor.title);
	markParameterReadOnly(this, descriptor.readOnly);
	// An info or text rescan leaves the range as it is: the model's, which the audio thread may be reading while it
	// automates the parameter, is then not written at all.
	if (!sameRange)
	{
		setRange(static_cast<float>(descriptor.minValue), static_cast<float>(descriptor.maxValue), stepOf(descriptor));
	}
}

QString ClapParamModel::displayValue(const float value) const
{
	if (m_formatter)
	{
		return m_formatter(static_cast<double>(value));
	}
	return FloatModel::displayValue(value);
}

ClapParamModelList::ClapParamModelList(clap::HostedPlugin* plugin, Model* parent) :
	m_plugin(plugin),
	m_parent(parent)
{
}

auto ClapParamModelList::modelFor(std::uint32_t id) const -> ClapParamModel*
{
	const auto found = std::find_if(m_models.begin(), m_models.end(), [id](const ClapParamModel* model) { return model->paramId() == id; });
	return found != m_models.end() ? *found : nullptr;
}

auto ClapParamModelList::create(const ClapParamDescriptor& descriptor) -> ClapParamModel*
{
	auto* model = new ClapParamModel(descriptor, m_parent);
	// host -> plug-in: the model's value into the lock-free snapshot the audio thread reads, from any thread
	// that changes the model - not when the host itself wrote it (pullValues), which would echo it back.
	QObject::connect(model, &Model::dataChanged, model, [this, model] {
		if (!m_syncing) { m_plugin->setParamPlain(model->paramId(), static_cast<double>(model->value())); }
	});
	// plug-in -> host: the value as the plug-in's own formatter writes it.
	model->setDisplayFormatter([plugin = m_plugin, id = descriptor.id](double value) { return plugin->paramDisplayValue(id, value); });
	return model;
}

auto ClapParamModelList::sync() -> bool
{
	const auto revision = m_plugin->paramsRevision();
	if (m_built && revision == m_revision) { return false; }
	m_built = true;
	m_revision = revision;
	std::vector<ClapParamModel*> next;
	m_syncing = true;  // a new range may move a value: that is not the user's change
	for (const auto& descriptor : m_plugin->parameters())
	{
		ClapParamModel* model = modelFor(descriptor.id);
		if (model != nullptr) { model->setDescriptor(descriptor); }
		else { model = create(descriptor); }
		next.push_back(model);
	}
	m_syncing = false;
	removeAllBut(next);
	m_models = std::move(next);
	return true;
}

void ClapParamModelList::removeAllBut(const std::vector<ClapParamModel*>& kept)
{
	std::vector<ClapParamModel*> gone;
	std::copy_if(m_models.begin(), m_models.end(), std::back_inserter(gone),
		[&kept](ClapParamModel* model) { return std::find(kept.begin(), kept.end(), model) == kept.end(); });
	if (gone.empty()) { return; }
	auto* engine = Engine::audioEngine();
	if (engine != nullptr) { engine->requestChangeInModel(); }
	for (ClapParamModel* model : gone) { delete model; }
	if (engine != nullptr) { engine->doneChangeInModel(); }
}

void ClapParamModelList::pullValues(float tolerance)
{
	if (m_syncing) { return; }
	m_syncing = true;
	for (auto* model : m_models)
	{
		const auto& descriptor = model->descriptor();
		const auto range = std::max(1e-12, descriptor.maxValue - descriptor.minValue);
		const auto value = static_cast<float>(m_plugin->paramPlain(model->paramId()));
		if (std::abs(model->value() - value) > range * tolerance) { model->setValue(value); }
	}
	m_syncing = false;
}

} // namespace lmms
