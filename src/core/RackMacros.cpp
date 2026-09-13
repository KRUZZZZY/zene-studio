/*
 * RackMacros.cpp - a rack's macros: named, persisted scalars that drive a set
 *                  of existing model parameters, each within a range window.
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

#include "RackMacros.h"

#include <algorithm>
#include <cstddef>

#include <QDomDocument>

#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Rack.h"

namespace lmms
{

namespace
{

//! The effect a target names inside @a rack, or nullptr with @a why set. The
//! chain and effect indexes are checked here because this is the one place that
//! knows the rack's shape.
Effect* effectOfTarget(Rack& rack, const RackMacroTarget& target, QString* why)
{
	if (target.chain < 0)
	{
		*why = QStringLiteral("chain %1 is not a chain index").arg(target.chain);
		return nullptr;
	}
	if (target.chain >= rack.chainCount())
	{
		*why = QStringLiteral("the rack has no chain %1 (it has %2)")
			.arg(target.chain)
			.arg(rack.chainCount());
		return nullptr;
	}
	EffectChain* chain = rack.chain(target.chain);
	if (chain == nullptr)
	{
		*why = QStringLiteral("chain %1 is empty").arg(target.chain);
		return nullptr;
	}
	const std::vector<Effect*>& effects = chain->effects();
	if (target.effect < 0)
	{
		*why = QStringLiteral("effect %1 is not an effect index").arg(target.effect);
		return nullptr;
	}
	if (target.effect >= static_cast<int>(effects.size()))
	{
		*why = QStringLiteral("chain %1 has no effect %2 (it has %3)")
			.arg(target.chain)
			.arg(target.effect)
			.arg(static_cast<int>(effects.size()));
		return nullptr;
	}
	return effects[static_cast<std::size_t>(target.effect)];
}

} // namespace

bool isValidMacroTarget(const RackMacroTarget& target)
{
	if (target.chain < 0) { return false; }
	if (target.effect < 0) { return false; }
	if (target.parameter.isEmpty()) { return false; }
	// The window is a fraction pair: a driver that could leave its own 0..1
	// span would be unrepresentable in the persisted form.
	if (target.low < 0.0f || target.low > 1.0f) { return false; }
	return target.high >= 0.0f && target.high <= 1.0f;
}

AutomatableModel* rackMacroTargetModel(Rack& rack, const RackMacroTarget& target, QString* why)
{
	QString localReason;
	QString* const reason = why != nullptr ? why : &localReason;
	Effect* effect = effectOfTarget(rack, target, reason);
	if (effect == nullptr) { return nullptr; }

	// Resolve by NAME through the effect's own parameter list - the list, in
	// the order, plugin.param_get reports - so the target survives a reload
	// (where no model pointer does) and means the same parameter to a human
	// and to an agent.
	const QList<AutomatableModel*> models = controlEffectParameters(effect);
	ControlResult error;
	AutomatableModel* model = controlResolveParameterIn(models, target.parameter, 0, false, &error);
	if (model == nullptr) { *reason = error.errorMessage; }
	return model;
}

// ---------------------------------------------------------------------------
// RackMacros
// ---------------------------------------------------------------------------

auto RackMacros::macroCount() const -> int
{
	return static_cast<int>(m_macros.size());
}

auto RackMacros::macro(int index) -> RackMacro*
{
	if (index < 0 || index >= macroCount()) { return nullptr; }
	return &m_macros[static_cast<std::size_t>(index)];
}

auto RackMacros::macro(int index) const -> const RackMacro*
{
	if (index < 0 || index >= macroCount()) { return nullptr; }
	return &m_macros[static_cast<std::size_t>(index)];
}

auto RackMacros::addMacro(const QString& name, float value) -> int
{
	RackMacro entry;
	entry.name = name;
	entry.value = std::clamp(value, 0.0f, 1.0f);
	m_macros.push_back(entry);
	return macroCount() - 1;
}

auto RackMacros::insertMacro(int index, const RackMacro& macro) -> bool
{
	if (index < 0) { return false; }
	if (index > macroCount()) { return false; }
	m_macros.insert(m_macros.begin() + index, macro);
	return true;
}

auto RackMacros::removeMacro(int index) -> bool
{
	if (macro(index) == nullptr) { return false; }
	m_macros.erase(m_macros.begin() + index);
	return true;
}

auto RackMacros::setValue(int index, float value) -> bool
{
	RackMacro* entry = macro(index);
	if (entry == nullptr) { return false; }
	entry->value = std::clamp(value, 0.0f, 1.0f);
	return true;
}

auto RackMacros::addTarget(int index, const RackMacroTarget& target) -> int
{
	RackMacro* entry = macro(index);
	if (entry == nullptr) { return -1; }
	entry->targets.push_back(target);
	return static_cast<int>(entry->targets.size()) - 1;
}

auto RackMacros::insertTarget(int index, int targetIndex, const RackMacroTarget& target) -> bool
{
	RackMacro* entry = macro(index);
	if (entry == nullptr) { return false; }
	if (targetIndex < 0) { return false; }
	if (targetIndex > static_cast<int>(entry->targets.size())) { return false; }
	entry->targets.insert(entry->targets.begin() + targetIndex, target);
	return true;
}

auto RackMacros::removeTarget(int index, int targetIndex) -> bool
{
	RackMacro* entry = macro(index);
	if (entry == nullptr) { return false; }
	if (targetIndex < 0) { return false; }
	if (targetIndex >= static_cast<int>(entry->targets.size())) { return false; }
	entry->targets.erase(entry->targets.begin() + targetIndex);
	return true;
}

auto RackMacros::apply(int index, Rack& rack) -> std::vector<RackMacroWrite>
{
	std::vector<RackMacroWrite> writes;
	const RackMacro* entry = macro(index);
	if (entry == nullptr) { return writes; }

	for (const RackMacroTarget& target : entry->targets)
	{
		QString why;
		AutomatableModel* model = rackMacroTargetModel(rack, target, &why);
		// A target whose chain, effect or parameter is gone is skipped rather
		// than guessed at: the applied count is the returned size, so a caller
		// can see the difference between "wrote 3" and "wrote 2, skipped 1".
		if (model == nullptr) { continue; }

		const float minimum = model->minValue<float>();
		const float maximum = model->maxValue<float>();
		const float fraction = std::clamp(target.low + entry->value * (target.high - target.low),
			0.0f, 1.0f);
		const float written = minimum + fraction * (maximum - minimum);

		RackMacroWrite write;
		write.target = target;
		write.previous = model->value<float>();
		write.written = written;

		// The model is a JournallingObject, so this checkpoint is the
		// parameter half of the inverse for the engine's own undo stack
		// (SPEC A16) - the same mechanism plugin.param_set uses.
		model->addJournalCheckPoint();
		model->setValue(written);
		writes.push_back(write);
	}
	return writes;
}

void RackMacros::clear()
{
	m_macros.clear();
}

void RackMacros::saveSettings(QDomDocument& doc, QDomElement& rackElement) const
{
	for (const RackMacro& entry : m_macros)
	{
		QDomElement macroElement = doc.createElement(QStringLiteral("macro"));
		macroElement.setAttribute(QStringLiteral("name"), entry.name);
		macroElement.setAttribute(QStringLiteral("value"), static_cast<double>(entry.value));

		for (const RackMacroTarget& target : entry.targets)
		{
			QDomElement targetElement = doc.createElement(QStringLiteral("target"));
			targetElement.setAttribute(QStringLiteral("chain"), target.chain);
			targetElement.setAttribute(QStringLiteral("effect"), target.effect);
			targetElement.setAttribute(QStringLiteral("parameter"), target.parameter);
			targetElement.setAttribute(QStringLiteral("low"), static_cast<double>(target.low));
			targetElement.setAttribute(QStringLiteral("high"), static_cast<double>(target.high));
			macroElement.appendChild(targetElement);
		}

		rackElement.appendChild(macroElement);
	}
}

void RackMacros::loadSettings(const QDomElement& rackElement)
{
	clear();

	for (QDomElement macroElement = rackElement.firstChildElement(QStringLiteral("macro"));
		!macroElement.isNull();
		macroElement = macroElement.nextSiblingElement(QStringLiteral("macro")))
	{
		RackMacro entry;
		entry.name = macroElement.attribute(QStringLiteral("name"));
		entry.value = macroElement.attribute(QStringLiteral("value"), QStringLiteral("0")).toFloat();

		for (QDomElement targetElement = macroElement.firstChildElement(QStringLiteral("target"));
			!targetElement.isNull();
			targetElement = targetElement.nextSiblingElement(QStringLiteral("target")))
		{
			RackMacroTarget target;
			target.chain =
				targetElement.attribute(QStringLiteral("chain"), QStringLiteral("0")).toInt();
			target.effect =
				targetElement.attribute(QStringLiteral("effect"), QStringLiteral("0")).toInt();
			target.parameter = targetElement.attribute(QStringLiteral("parameter"));
			target.low =
				targetElement.attribute(QStringLiteral("low"), QStringLiteral("0")).toFloat();
			target.high =
				targetElement.attribute(QStringLiteral("high"), QStringLiteral("1")).toFloat();
			entry.targets.push_back(target);
		}

		m_macros.push_back(entry);
	}
}

} // namespace lmms
