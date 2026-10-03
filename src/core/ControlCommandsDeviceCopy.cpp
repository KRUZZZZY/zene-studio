/*
 * ControlCommandsDeviceCopy.cpp - plugin.copy, chain.copy and plugin.ab: copy devices to another chain, compare two settings
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
/*! 0.5.0 L10: devices moved between chains with their settings, and an A/B comparison.
 *  - plugin.copy (MX-076): one device to another track's or channel's chain - or a rack chain - at a
 *    position, with its settings (the device's own state document, recreated through the path an undo
 *    of plugin.unload takes);
 *  - chain.copy (MX-076): every device of a chain, appended or replacing the destination's;
 *  - plugin.ab (MX-092): two stored settings per device - store the current one in A or B, recall
 *    one, toggle between them. The slots live with the session (not the project); a recall is one
 *    undo step. */

#include <functional>
#include <map>
#include <vector>

#include <QJsonArray>
#include <QPointer>

#include "ControlDeviceMoves.h"
#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlDeviceTools.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "AutomatableModel.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

//! The destination of a copy: 'to' (trk-<n> / ch-<n>) and 'to_chain' (a rack chain), as resolveChainTarget reads them.
bool destination(const QJsonObject& args, ControlTarget* target, ControlResult* error)
{
	QJsonObject named{{QStringLiteral("target"), args.value(QStringLiteral("to"))}};
	if (args.contains(QStringLiteral("to_chain"))) { named.insert(QStringLiteral("chain"), args.value(QStringLiteral("to_chain"))); }
	return resolveChainTarget(named, target, error);
}

//! The destination chain, found again by its ids whenever a recorded step runs (an undo can delete
//! a rack chain and a redo create it again, as a new object).
std::function<EffectChain*()> destinationResolver(const QJsonObject& args)
{
	return [args]() -> EffectChain* {
		ControlTarget target;
		ControlResult ignored;
		return destination(args, &target, &ignored) ? target.chain : nullptr;
	};
}

QJsonObject copied(const ControlTarget& to, const QJsonArray& ids, const QString& mechanism)
{
	QJsonObject result{{QStringLiteral("to"), to.id}, {QStringLiteral("plugins"), ids}, {QStringLiteral("count"), ids.size()}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("to"), to.id}}, QStringLiteral("control.undo"), {},
		true, mechanism));
	return result;
}

ControlResult pluginCopy(const QJsonObject& args)
{
	ControlTarget from;
	ControlTarget to;
	ControlResult error;
	if (!resolveChainTarget(args, &from, &error) || !destination(args, &to, &error)) { return error; }
	Effect* effect = resolveControlEffect(from, args.value(QStringLiteral("plugin")).toString(), &error);
	if (effect == nullptr) { return error; }
	const QString state = controlEffectStateXml(effect);
	if (state.size() > StructuralSnapshotLimit)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the device's settings are too large to record for an undo"));
	}
	const int position = args.contains(QStringLiteral("position")) ? args.value(QStringLiteral("position")).toInt() : -1;
	const QJsonArray ids = recreateDevices(destinationResolver(args), {state}, position, &error);
	if (ids.isEmpty()) { return error.errorMessage.isEmpty() ? ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the device could not be copied")) : error; }
	return ControlResult::success(copied(to, ids, QStringLiteral("action checkpoint: the recorded undo step takes the copy out "
		"again; its redo recreates it from the same settings document")));
}

ControlResult chainCopy(const QJsonObject& args)
{
	ControlTarget from;
	ControlTarget to;
	ControlResult error;
	if (!resolveChainTarget(args, &from, &error) || !destination(args, &to, &error)) { return error; }
	if (from.chain == to.chain) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the chain cannot be copied onto itself")); }
	// As plugin.copy: every copy's settings must fit an undo step, checked before 'replace' takes anything out.
	if (!devicesRecordable(from.chain->effects()))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a device on %1 has settings too large to record for "
			"an undo, so the chain is not copied").arg(from.id));
	}
	QStringList states;
	for (Effect* effect : from.chain->effects()) { states.append(controlEffectStateXml(effect)); }
	const bool replace = args.value(QStringLiteral("replace")).toBool();
	if (replace && !devicesRecordable(to.chain->effects()))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a device on %1 has settings too large to record for "
			"an undo, so its chain is not replaced").arg(to.id));
	}
	if (replace)
	{
		const std::vector<Effect*> existing = to.chain->effects();  // a copy: the removal changes the chain's list
		removeDevicesRecorded(destinationResolver(args), existing);
	}
	const QJsonArray ids = recreateDevices(destinationResolver(args), states, -1, &error);
	QJsonObject result = copied(to, ids, QStringLiteral("action checkpoint: the recorded undo step takes the copies out and "
		"(with 'replace') recreates the devices that were there, at their slots"));
	// A device that could not be recreated stops the copy there; what was copied stays, and says so.
	if (ids.size() != states.size()) { result.insert(QStringLiteral("stopped"), error.errorMessage); }
	return ControlResult::success(result);
}

//! A device's two stored settings and which one it holds now; by the device's stable id.
struct AbSlots
{
	QString a;
	QString b;
	bool onB = false;
	//! The device the slots belong to: ids restart with each document, so a slot found under the
	//! id of a device it was not stored for is not its (review L10-L03-L02-1 #3).
	QPointer<Effect> owner;
	//! 0.5.0 L10 (MX-093): each slot's parameter values, in plugin.param_get order, for plugin.blend.
	std::vector<float> aValues;
	std::vector<float> bValues;
};

std::vector<float> parameterValues(Effect* effect)
{
	std::vector<float> values;
	for (const AutomatableModel* model : controlEffectParameters(effect)) { values.push_back(model->value<float>()); }
	return values;
}

//! Keep the settings heard now in slot B (or A), with their parameter values.
void storeSlot(Effect* effect, AbSlots& pair, bool toB)
{
	(toB ? pair.b : pair.a) = controlEffectStateXml(effect);
	(toB ? pair.bValues : pair.aValues) = parameterValues(effect);
}

std::map<int, AbSlots>& abSlots()
{
	static std::map<int, AbSlots> pairs;
	return pairs;
}

//! Put a recorded A/B state back on @a effect: its settings and its slots (which one is heard, what each holds).
void restoreAb(const QPointer<Effect>& effect, const QString& state, const AbSlots& held)
{
	if (!effect) { return; }
	controlRestoreEffectState(effect.data(), state.toUtf8());
	abSlots()[effect->id()] = held;
}

//! Put @a state on @a effect as one undo step: the previous settings and slots (@a slotsBefore) come
//! back on undo, @a slotsAfter with the settings on redo.
ControlResult recallState(Effect* effect, const QString& state, const AbSlots& slotsBefore, const AbSlots& slotsAfter)
{
	const QString before = controlEffectStateXml(effect);
	const ControlResult restored = controlRestoreEffectState(effect, state.toUtf8());
	if (!restored.ok) { return restored; }
	const QPointer<Effect> guarded(effect);
	addUndoStep([guarded, before, slotsBefore]() { restoreAb(guarded, before, slotsBefore); },
		[guarded, state, slotsAfter]() { restoreAb(guarded, state, slotsAfter); });
	Engine::getSong()->setModified();
	return restored;
}

QJsonObject abJson(const AbSlots& pair)
{
	return {{QStringLiteral("current"), pair.onB ? QStringLiteral("b") : QStringLiteral("a")},
		{QStringLiteral("has_a"), !pair.a.isEmpty()}, {QStringLiteral("has_b"), !pair.b.isEmpty()}};
}

//! Recall slot B (or A); @a before is the slots as they were before the command touched them.
ControlResult abRecall(Effect* effect, AbSlots& pair, bool toB, AbSlots before)
{
	const QString state = toB ? pair.b : pair.a;
	if (state.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("nothing is stored in %1").arg(toB ? QLatin1Char('B') : QLatin1Char('A')));
	}
	AbSlots after = pair;
	after.onB = toB;
	const ControlResult recalled = recallState(effect, state, before, after);
	pair = recalled.ok ? after : before;
	if (!recalled.ok) { return recalled; }
	return ControlResult::success(abJson(pair));
}

//! The settings heard now become the current slot's; the other slot - a copy on the first toggle - is recalled.
ControlResult abToggle(Effect* effect, AbSlots& pair)
{
	const AbSlots before = pair;
	storeSlot(effect, pair, pair.onB);
	if ((pair.onB ? pair.a : pair.b).isEmpty()) { storeSlot(effect, pair, !pair.onB); }
	return abRecall(effect, pair, !pair.onB, before);
}

ControlResult pluginAb(const QJsonObject& args)
{
	ControlTarget target;
	ControlResult error;
	if (!resolveChainTarget(args, &target, &error)) { return error; }
	Effect* effect = resolveControlEffect(target, args.value(QStringLiteral("plugin")).toString(), &error);
	if (effect == nullptr) { return error; }
	AbSlots& pair = abSlots()[effect->id()];
	if (pair.owner != effect) { pair = AbSlots{}; pair.owner = effect; }
	const QString action = args.value(QStringLiteral("action")).toString();
	const bool toB = args.value(QStringLiteral("slot")).toString() == QLatin1String("b");
	if (action == QLatin1String("recall")) { return abRecall(effect, pair, toB, pair); }
	if (action == QLatin1String("toggle")) { return abToggle(effect, pair); }
	if (action == QLatin1String("store"))
	{
		storeSlot(effect, pair, toB);
		pair.onB = toB;
	}
	return ControlResult::success(abJson(pair));
}

} // namespace

bool control::abSlotValues(Effect* effect, std::vector<float>* a, std::vector<float>* b)
{
	const auto found = abSlots().find(effect->id());
	if (found == abSlots().end() || found->second.owner != effect || found->second.a.isEmpty() || found->second.b.isEmpty())
	{
		return false;
	}
	*a = found->second.aValues;
	*b = found->second.bValues;
	return true;
}

namespace
{

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), true, handler);
}

} // namespace

void registerDeviceCopyCommands(ControlRegistry& registry)
{
	const QJsonObject source{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("chain"), integerProperty(0, 64)}};
	QJsonObject one = source;
	one.insert(QStringLiteral("plugin"), stringProperty());
	one.insert(QStringLiteral("to"), stringProperty());
	one.insert(QStringLiteral("to_chain"), integerProperty(0, 64));
	one.insert(QStringLiteral("position"), integerProperty(0, 1024));
	add(registry, "plugin.copy", QStringLiteral("Copy one device ('plugin', fx-<n> on 'target' - with 'chain' for a rack chain) "
		"to another chain: 'to' (trk-<n> / ch-<n>, 'to_chain' for its rack chain), at 'position' (0 = first; appended when "
		"left out), with its settings. 'plugins' answers the copy's fx-<n>. One undo step."),
		one, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("to")}, pluginCopy);
	QJsonObject whole = source;
	whole.insert(QStringLiteral("to"), stringProperty());
	whole.insert(QStringLiteral("to_chain"), integerProperty(0, 64));
	whole.insert(QStringLiteral("replace"), booleanProperty());
	add(registry, "chain.copy", QStringLiteral("Copy every device of a chain ('target', 'chain') to another ('to', 'to_chain'), "
		"in order, with their settings: appended, or with 'replace' in place of the devices there. One undo step."),
		whole, {QStringLiteral("target"), QStringLiteral("to")}, chainCopy);
	QJsonObject ab = source;
	ab.insert(QStringLiteral("plugin"), stringProperty());
	ab.insert(QStringLiteral("action"), enumProperty({QStringLiteral("get"), QStringLiteral("store"), QStringLiteral("recall"), QStringLiteral("toggle")}));
	ab.insert(QStringLiteral("slot"), enumProperty({QStringLiteral("a"), QStringLiteral("b")}));
	add(registry, "plugin.ab", QStringLiteral("Compare two settings of one device: 'store' puts the current settings in 'slot' "
		"(a or b), 'recall' puts a stored slot's settings on the device, 'toggle' keeps the current settings in the slot heard "
		"now and recalls the other (the first toggle copies them, so A and B start equal), 'get' answers which slot is heard "
		"and which are stored. The slots last the session, not the project; a recall or toggle is one undo step."),
		ab, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("action")}, pluginAb);
}

} // namespace lmms
