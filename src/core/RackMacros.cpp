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
#include <cmath>
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

//! The change listeners of every RackMacros in the process, keyed by ChangeListenerId (see
//! RackMacros::addChangeListener): PROCESS-lifetime, like ControlRegistry's shutdown hooks, so
//! an id-based removal stays safe after the list or the listener's owner was destroyed. Control
//! thread only - the audio thread never touches macros - so no lock guards it.
struct ListenerEntry
{
	int id = 0;
	const RackMacros* source = nullptr;
	RackMacros::ChangeListener listener;
};

struct ListenerStore
{
	int nextId = 0;
	std::vector<ListenerEntry> entries;
};

ListenerStore& listeners()
{
	static ListenerStore store;
	return store;
}

/**
 * True when @a value is a number a macro can hold.
 *
 * A macro's value is a finite 0..1 scalar. std::clamp does NOT make a
 * nonfinite value one: every comparison against NaN is false, so clamp hands
 * NaN straight back - and an infinite value clamps to an end, which is a
 * different number than the caller meant. So the finiteness is checked here,
 * before the value is stored or applied.
 */
bool isFiniteMacroValue(float value)
{
	return std::isfinite(value);
}

/**
 * True when @a value is a FRACTION: a finite number inside 0..1.
 *
 * A window bound is stated as a fraction of the parameter's own range, and a
 * nonfinite one is not a fraction. The finiteness has to be checked on its
 * own: every comparison against NaN is false, so a range check alone lets a
 * NaN through.
 */
bool isMacroFraction(float value)
{
	return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

//! True when @a value is a curve: a finite number inside -1..1 (0 is linear).
bool isMacroCurve(float value)
{
	return std::isfinite(value) && value >= -1.0f && value <= 1.0f;
}

//! True when every number @a target carries is a number - the window bounds and
//! the curve. A nonfinite one makes the applied fraction a NaN, and a NaN is
//! what would be written into the parameter model.
bool isFiniteMacroTarget(const RackMacroTarget& target)
{
	return std::isfinite(target.low) && std::isfinite(target.high) && std::isfinite(target.curve);
}

} // namespace

float shapeMacroValue(float value, float curve)
{
	if (curve == 0.0f) { return value; }
	return std::pow(std::clamp(value, 0.0f, 1.0f), std::pow(4.0f, -curve));
}

bool isValidMacroTarget(const RackMacroTarget& target)
{
	if (target.chain < 0) { return false; }
	if (target.effect < 0) { return false; }
	if (target.parameter.isEmpty()) { return false; }
	// The window is a fraction pair: a driver that could leave its own 0..1
	// span would be unrepresentable in the persisted form. A nonfinite bound
	// is not a fraction either - and the range check cannot say so, because
	// every comparison against NaN is false.
	if (!isMacroFraction(target.low)) { return false; }
	if (!isMacroFraction(target.high)) { return false; }
	if (!isMacroCurve(target.curve)) { return false; }
	return true;
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

RackMacros::~RackMacros()
{
	// A dying list drops its own listeners, so a watcher that outlives it never hears a
	// pointer compare against a stale source again - and a later id-based removal of that
	// watcher is a harmless miss.
	auto& store = listeners();
	std::erase_if(store.entries, [this](const ListenerEntry& entry) { return entry.source == this; });
}

auto RackMacros::addChangeListener(ChangeListener listener) -> ChangeListenerId
{
	auto& store = listeners();
	store.entries.push_back({++store.nextId, this, std::move(listener)});
	return store.nextId;
}

bool RackMacros::removeChangeListener(ChangeListenerId id)
{
	if (id == 0) { return false; }
	auto& store = listeners();
	const auto found = std::find_if(store.entries.begin(), store.entries.end(),
		[id](const ListenerEntry& entry) { return entry.id == id; });
	if (found == store.entries.end()) { return false; }
	store.entries.erase(found);
	return true;
}

void RackMacros::notifyListChanged(int index, int shift) const
{
	// A snapshot first: a listener may add or remove listeners (open or close a watcher
	// while being told), and mutating the store mid-walk would invalidate it.
	std::vector<ChangeListener> due;
	for (const ListenerEntry& entry : listeners().entries)
	{
		if (entry.source == this) { due.push_back(entry.listener); }
	}
	for (const ChangeListener& listener : due) { listener(index, shift); }
}

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
	// A value that is not a number is refused, not clamped: clamp would keep
	// NaN as NaN (every comparison against it is false).
	if (!isFiniteMacroValue(value)) { return -1; }
	RackMacro entry;
	entry.name = name;
	entry.value = std::clamp(value, 0.0f, 1.0f);
	m_macros.push_back(entry);
	notifyListChanged();
	return macroCount() - 1;
}

auto RackMacros::insertMacro(int index, const RackMacro& macro) -> bool
{
	if (index < 0) { return false; }
	if (index > macroCount()) { return false; }
	if (!isFiniteMacroValue(macro.value)) { return false; }
	m_macros.insert(m_macros.begin() + index, macro);
	notifyListChanged(index, 1);
	return true;
}

auto RackMacros::removeMacro(int index) -> bool
{
	if (macro(index) == nullptr) { return false; }
	m_macros.erase(m_macros.begin() + index);
	notifyListChanged(index, -1);
	return true;
}

auto RackMacros::setValue(int index, float value) -> bool
{
	RackMacro* entry = macro(index);
	if (entry == nullptr) { return false; }
	// Same contract as addMacro: a value that is not a number is refused, and
	// the macro keeps the value it had.
	if (!isFiniteMacroValue(value)) { return false; }
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

auto RackMacros::apply(int index, Rack& rack, bool journalWrites) -> std::vector<RackMacroWrite>
{
	std::vector<RackMacroWrite> writes;
	const RackMacro* entry = macro(index);
	if (entry == nullptr) { return writes; }
	// The last line of defence before a parameter model: a macro whose own
	// value is not a number would write that number into every model it
	// drives. The storage doors (addMacro, insertMacro, setValue) refuse such
	// a value already; a macro list that carries one anyway writes nothing.
	if (!isFiniteMacroValue(entry->value)) { return writes; }

	for (const RackMacroTarget& target : entry->targets)
	{
		// A window that carries a number that is not a number would write one
		// into the model: the fraction below is NaN, and so is the value. The
		// target is skipped, like one that no longer resolves.
		if (!isFiniteMacroTarget(target)) { continue; }
		QString why;
		AutomatableModel* model = rackMacroTargetModel(rack, target, &why);
		// A target whose chain, effect or parameter is gone is skipped rather
		// than guessed at: the applied count is the returned size, so a caller
		// can see the difference between "wrote 3" and "wrote 2, skipped 1".
		if (model == nullptr) { continue; }

		const float minimum = model->minValue<float>();
		const float maximum = model->maxValue<float>();
		const float fraction = std::clamp(target.low + shapeMacroValue(entry->value, target.curve) * (target.high - target.low),
			0.0f, 1.0f);
		const float written = minimum + fraction * (maximum - minimum);

		RackMacroWrite write;
		write.target = target;
		write.previous = model->value<float>();
		write.written = written;

		// Pair commands record their net inverse themselves, so they suppress
		// this model's journal checkpoint while keeping the normal non-automated
		// write behavior (including lastTouched). Equal writes are not edits.
		if (write.previous != write.written)
		{
			if (journalWrites) { model->addJournalCheckPoint(); }
			model->setValue(written, false, journalWrites);
		}
		writes.push_back(write);
	}
	return writes;
}

void RackMacros::clear()
{
	if (m_macros.empty()) { return; }
	m_macros.clear();
	notifyListChanged();
}

void RackMacros::reindexChains(int removedIndex)
{
	if (removedIndex < 1) { return; }
	for (RackMacro& macro : m_macros)
	{
		std::vector<RackMacroTarget> kept;
		kept.reserve(macro.targets.size());
		for (const RackMacroTarget& target : macro.targets)
		{
			// A target on the chain that is gone would, after the shift, name
			// whichever chain took that index: the wrong parameter, silently.
			if (target.chain == removedIndex) { continue; }
			RackMacroTarget moved = target;
			if (moved.chain > removedIndex) { --moved.chain; }
			kept.push_back(moved);
		}
		macro.targets.swap(kept);
	}
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
			if (target.curve != 0.0f) { targetElement.setAttribute(QStringLiteral("curve"), static_cast<double>(target.curve)); }
			macroElement.appendChild(targetElement);
		}

		rackElement.appendChild(macroElement);
	}
}

void RackMacros::loadSettings(const QDomElement& rackElement)
{
	// Reset directly rather than through clear(): ONE notification for the whole reload, not
	// one for the transient empty list in the middle of it.
	const bool hadMacros = !m_macros.empty();
	m_macros.clear();

	for (QDomElement macroElement = rackElement.firstChildElement(QStringLiteral("macro"));
		!macroElement.isNull();
		macroElement = macroElement.nextSiblingElement(QStringLiteral("macro")))
	{
		RackMacro entry;
		entry.name = macroElement.attribute(QStringLiteral("name"));
		const float rawValue =
			macroElement.attribute(QStringLiteral("value"), QStringLiteral("0")).toFloat();
		// A macro's value is a finite number. QString reads a "nan" (or an
		// "inf") attribute as exactly that, and a NaN would be written into
		// every parameter the macro drives - so a value that is not a number is
		// stored as 0, and the macro still works.
		entry.value = isFiniteMacroValue(rawValue) ? rawValue : 0.0f;

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
			target.curve = targetElement.attribute(QStringLiteral("curve"), QStringLiteral("0")).toFloat();
			// A persisted window that carries a number that is not a number is
			// not loaded: QString reads a "nan" attribute as a NaN, and a NaN
			// window would be applied as a NaN parameter value.
			if (!isFiniteMacroTarget(target)) { continue; }
			entry.targets.push_back(target);
		}

		m_macros.push_back(entry);
	}
	if (hadMacros || !m_macros.empty()) { notifyListChanged(); }
}

} // namespace lmms
