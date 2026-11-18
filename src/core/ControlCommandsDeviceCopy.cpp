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

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QJsonArray>
#include <QPointer>

#include "ControlDeviceMoves.h"
#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlDeviceTools.h"
#include "DeviceAbState.h"
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

//! @a args with 'to' naming the one destination @a id: the form destination() and destinationResolver() read.
QJsonObject withDestination(QJsonObject args, const QString& id)
{
	args.insert(QStringLiteral("to"), id);
	return args;
}

/*! W1: the destinations a call names - 'to' as one trk-<n> / ch-<n>, or a LIST of them (the mixer's Copy to Channels:
 *  every copy in one call, so one undo step takes them all back). Each is resolved before anything is copied, so a bad
 *  id refuses the whole call rather than leave half the copies made. */
bool destinations(const QJsonObject& args, std::vector<ControlTarget>* out, ControlResult* error)
{
	const QJsonValue to = args.value(QStringLiteral("to"));
	const QJsonArray ids = to.isArray() ? to.toArray() : QJsonArray{to};
	QStringList seen;
	for (const QJsonValue& id : ids)
	{
		ControlTarget target;
		if (!id.isString() || seen.contains(id.toString()))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'to' names each destination once, as a "
				"trk-<n> / ch-<n> id"));
			return false;
		}
		if (!destination(withDestination(args, id.toString()), &target, error)) { return false; }
		seen << id.toString();
		out->push_back(target);
	}
	if (out->empty()) { *error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'to' names no destination")); }
	return !out->empty();
}

//! What one destination's copy made: the fx-<n> ids, and why it stopped short (empty when it did not).
struct Copied
{
	QJsonArray ids;
	QString stopped;
};

using CopyOne = std::function<Copied(const QJsonObject& oneArgs, const ControlTarget& to)>;

/*! Runs @a copyOne for each destination in order, stopping at the first one that stops short: what was copied stays and
 *  the reply says where it stopped ('stopped'). 'to' answers as the call named it - one id, or the list - and 'copies'
 *  says what each destination got. Every step the copies record is merged by the registry into ONE undo step. */
QJsonObject copyToEach(const QJsonObject& args, const std::vector<ControlTarget>& targets, const CopyOne& copyOne,
	const QString& mechanism)
{
	QJsonArray all;
	QJsonArray copies;
	QString stopped;
	for (const ControlTarget& to : targets)
	{
		const Copied one = copyOne(withDestination(args, to.id), to);
		for (const QJsonValue& id : one.ids) { all.append(id); }
		copies.append(QJsonObject{{QStringLiteral("to"), to.id}, {QStringLiteral("plugins"), one.ids}});
		stopped = one.stopped;
		if (!stopped.isEmpty()) { break; }
	}
	const QJsonValue named = args.value(QStringLiteral("to")).isArray() ? args.value(QStringLiteral("to"))
		: QJsonValue(targets.front().id);
	QJsonObject result{{QStringLiteral("to"), named}, {QStringLiteral("plugins"), all}, {QStringLiteral("count"), all.size()},
		{QStringLiteral("copies"), copies}};
	if (!stopped.isEmpty()) { result.insert(QStringLiteral("stopped"), stopped); }
	// Nothing copied (an empty source chain): no transaction, so the registry records the call by the steps it left - none
	// for a plain copy (no record, nothing for control.undo to misattribute), a removal step for a 'replace'.
	if (all.isEmpty()) { return result; }
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("to"), named}}, QStringLiteral("control.undo"), {},
		true, mechanism));
	return result;
}

ControlResult pluginCopy(const QJsonObject& args)
{
	ControlTarget from;
	std::vector<ControlTarget> targets;
	ControlResult error;
	if (!resolveChainTarget(args, &from, &error) || !destinations(args, &targets, &error)) { return error; }
	Effect* effect = resolveControlEffect(from, args.value(QStringLiteral("plugin")).toString(), &error);
	if (effect == nullptr) { return error; }
	const QString state = controlEffectStateXml(effect);
	if (state.size() > StructuralSnapshotLimit)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the device's settings are too large to record for an undo"));
	}
	const int position = args.contains(QStringLiteral("position")) ? args.value(QStringLiteral("position")).toInt() : -1;
	auto why = std::make_shared<ControlResult>(ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the device could not be copied")));
	const QJsonObject result = copyToEach(args, targets, [state, position, why](const QJsonObject& oneArgs, const ControlTarget&) {
		ControlResult reason;
		const QJsonArray ids = recreateDevices(destinationResolver(oneArgs), {state}, position, &reason);
		if (ids.isEmpty() && !reason.errorMessage.isEmpty()) { *why = reason; }
		return Copied{ids, ids.isEmpty() ? why->errorMessage : QString()};
	}, QStringLiteral("action checkpoint: the recorded undo step takes every copy out again; its redo recreates them from the "
		"same settings document"));
	// Nothing copied at all is the refusal it always was for one destination, with the recreate's own reason.
	return result.value(QStringLiteral("count")).toInt() == 0 ? *why : ControlResult::success(result);
}

//! chain.copy's checks, before anything is taken out or copied: no destination is the source, and every device it would
//! copy - and, with 'replace', every device it would remove - fits an undo step.
ControlResult chainCopyRefusal(const ControlTarget& from, const std::vector<ControlTarget>& targets, bool replace)
{
	const auto self = [&from](const ControlTarget& to) { return to.chain == from.chain; };
	if (std::any_of(targets.begin(), targets.end(), self))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the chain cannot be copied onto itself"));
	}
	if (!devicesRecordable(from.chain->effects()))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a device on %1 has settings too large to record for "
			"an undo, so the chain is not copied").arg(from.id));
	}
	for (const ControlTarget& to : targets)
	{
		if (replace && !devicesRecordable(to.chain->effects()))
		{
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a device on %1 has settings too large to record "
				"for an undo, so its chain is not replaced").arg(to.id));
		}
	}
	return ControlResult::success({});
}

ControlResult chainCopy(const QJsonObject& args)
{
	ControlTarget from;
	std::vector<ControlTarget> targets;
	ControlResult error;
	if (!resolveChainTarget(args, &from, &error) || !destinations(args, &targets, &error)) { return error; }
	const bool replace = args.value(QStringLiteral("replace")).toBool();
	const ControlResult refused = chainCopyRefusal(from, targets, replace);
	if (!refused.ok) { return refused; }
	QStringList states;
	for (Effect* effect : from.chain->effects()) { states.append(controlEffectStateXml(effect)); }
	return ControlResult::success(copyToEach(args, targets, [states, replace](const QJsonObject& oneArgs, const ControlTarget& to) {
		if (replace)
		{
			const std::vector<Effect*> existing = to.chain->effects();  // a copy: the removal changes the chain's list
			removeDevicesRecorded(destinationResolver(oneArgs), existing);
		}
		ControlResult why;
		const QJsonArray ids = recreateDevices(destinationResolver(oneArgs), states, -1, &why);
		// A device that could not be recreated stops the copy there; what was copied stays, and says so.
		return Copied{ids, ids.size() != states.size() ? why.errorMessage : QString()};
	}, QStringLiteral("action checkpoint: the recorded undo step takes the copies out and (with 'replace') recreates the devices "
		"that were there, at their slots")));
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

bool sameAbSlots(const AbSlots& first, const AbSlots& second)
{
	return first.a == second.a && first.b == second.b && first.onB == second.onB &&
		first.aValues == second.aValues && first.bValues == second.bValues;
}

std::map<int, AbSlots>& abSlots()
{
	static std::map<int, AbSlots> pairs;
	return pairs;
}

//! Release the record of the device with @a id once the device @a gone is destroyed. A record another device has
//! claimed under the same id (ids restart with each document) is its own and stays.
void forgetDestroyedAbSlots(int id, const QObject* gone)
{
	auto& pairs = abSlots();
	const auto found = pairs.find(id);
	if (found != pairs.end() && (found->second.owner.isNull() || found->second.owner.data() == gone)) { pairs.erase(found); }
}

//! The record of @a effect's id, owned by @a effect: a record another device owned is replaced by an empty one. The
//! first claim connects the device's destruction to releasing the record, so a destroyed device keeps no settings.
AbSlots& claimAbSlots(Effect* effect)
{
	const int id = effect->id();
	AbSlots& pair = abSlots()[id];
	if (pair.owner != effect)
	{
		pair = AbSlots{};
		pair.owner = effect;
		QObject::connect(effect, &QObject::destroyed, [id](QObject* gone) { forgetDestroyedAbSlots(id, gone); });
	}
	return pair;
}

//! The device an A/B step acted on, recorded so the step finds it again when it runs.
struct AbRecorded
{
	//! The chain the device was on, found again by its owner's address (a plugin.unload and its undo rebuild
	//! the chain's devices as new objects).
	std::function<EffectChain*()> chainOf;
	//! The device itself, while it lives.
	QPointer<Effect> effect;
	//! Its slot then, for a chain that no longer holds it: a rebuilt device keeps the slot, its fx id renumbers.
	int slot = -1;
	//! The plugin the slot's device has to be for, so another device moved into the slot is not written to.
	QString plugin;
};

//! The device @a recorded names: itself while it is still on its chain, else the device at its slot when that is
//! the same plugin (ControlStructuralSupport.cpp's madeEffect). nullptr when neither is there.
Effect* findRecorded(const AbRecorded& recorded)
{
	EffectChain* chain = recorded.chainOf();
	if (chain == nullptr) { return nullptr; }
	const std::vector<Effect*>& effects = chain->effects();
	if (!recorded.effect.isNull() && std::find(effects.begin(), effects.end(), recorded.effect.data()) != effects.end())
	{
		return recorded.effect.data();
	}
	if (recorded.slot < 0 || recorded.slot >= static_cast<int>(effects.size())) { return nullptr; }
	Effect* atSlot = effects[static_cast<std::size_t>(recorded.slot)];
	return QString::fromUtf8(atSlot->descriptor()->name) == recorded.plugin ? atSlot : nullptr;
}

AbRecorded recordAb(const ControlTarget& target, Effect* effect)
{
	const std::vector<Effect*>& effects = target.chain->effects();
	return {recordedChainFinder(target.chain), QPointer<Effect>(effect),
		static_cast<int>(std::find(effects.begin(), effects.end(), effect) - effects.begin()),
		QString::fromUtf8(effect->descriptor()->name)};
}

//! Put the A/B slots @a kept back on the device @a recorded names, found again as restoreAb finds it, and rebind
//! the slots' owner to that device: a store's undo or redo after the device was recreated must not write the
//! slots under a stale id, or leave an owner plugin.ab would take for a different device.
void restoreSlots(const AbRecorded& recorded, const AbSlots& kept)
{
	Effect* effect = findRecorded(recorded);
	if (effect == nullptr) { return; }
	AbSlots& pair = claimAbSlots(effect);
	pair = kept;
	pair.owner = effect;
}

//! Put a recorded A/B state back on the device @a recorded names: its settings and its slots (which one is
//! heard, what each holds). Nothing when the device is gone: an undo cannot invent it.
void restoreAb(const AbRecorded& recorded, const QString& state, const AbSlots& held)
{
	Effect* effect = findRecorded(recorded);
	if (effect == nullptr) { return; }
	controlRestoreEffectState(effect, state.toUtf8());
	AbSlots& pair = claimAbSlots(effect);  // a rebuilt device is a new object: the slots now belong to the one found
	pair = held;
	pair.owner = effect;
}

//! Put @a state on @a effect as one undo step: the previous settings and slots (@a slotsBefore) come
//! back on undo, @a slotsAfter with the settings on redo.
ControlResult recallState(const AbRecorded& recorded, Effect* effect, const QString& state, const AbSlots& slotsBefore,
	const AbSlots& slotsAfter)
{
	const QString before = controlEffectStateXml(effect);
	const ControlResult restored = controlRestoreEffectState(effect, state.toUtf8());
	if (!restored.ok) { return restored; }
	addUndoStep([recorded, before, slotsBefore]() { restoreAb(recorded, before, slotsBefore); },
		[recorded, state, slotsAfter]() { restoreAb(recorded, state, slotsAfter); });
	Engine::getSong()->setModified();
	return restored;
}

QJsonObject abJson(const AbSlots& pair)
{
	return {{QStringLiteral("current"), pair.onB ? QStringLiteral("b") : QStringLiteral("a")},
		{QStringLiteral("has_a"), !pair.a.isEmpty()}, {QStringLiteral("has_b"), !pair.b.isEmpty()}};
}

//! Recall slot B (or A); @a before is the slots as they were before the command touched them.
ControlResult abRecall(const AbRecorded& recorded, Effect* effect, AbSlots& pair, bool toB, AbSlots before)
{
	const QString state = toB ? pair.b : pair.a;
	if (state.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("nothing is stored in %1").arg(toB ? QLatin1Char('B') : QLatin1Char('A')));
	}
	AbSlots after = pair;
	after.onB = toB;
	const ControlResult recalled = recallState(recorded, effect, state, before, after);
	pair = recalled.ok ? after : before;
	if (!recalled.ok) { return recalled; }
	return ControlResult::success(abJson(pair));
}

//! The settings heard now become the current slot's; the other slot - a copy on the first toggle - is recalled.
ControlResult abToggle(const AbRecorded& recorded, Effect* effect, AbSlots& pair)
{
	const AbSlots before = pair;
	storeSlot(effect, pair, pair.onB);
	if ((pair.onB ? pair.a : pair.b).isEmpty()) { storeSlot(effect, pair, !pair.onB); }
	return abRecall(recorded, effect, pair, !pair.onB, before);
}

ControlResult pluginAb(const QJsonObject& args)
{
	ControlTarget target;
	ControlResult error;
	if (!resolveChainTarget(args, &target, &error)) { return error; }
	Effect* effect = resolveControlEffect(target, args.value(QStringLiteral("plugin")).toString(), &error);
	if (effect == nullptr) { return error; }
	AbSlots& pair = claimAbSlots(effect);
	const QString action = args.value(QStringLiteral("action")).toString();
	const bool toB = args.value(QStringLiteral("slot")).toString() == QLatin1String("b");
	const AbRecorded recorded = recordAb(target, effect);
	if (action == QLatin1String("recall")) { return abRecall(recorded, effect, pair, toB, pair); }
	if (action == QLatin1String("toggle")) { return abToggle(recorded, effect, pair); }
	if (action == QLatin1String("store"))
	{
		const AbSlots before = pair;
		storeSlot(effect, pair, toB);
		pair.onB = toB;
		const bool changed = !sameAbSlots(before, pair);
		if (changed)
		{
			// The device is recorded like recall's, so undo and redo find it again after an unload and its undo rebuilt it.
			addUndoStep([recorded, before]() { restoreSlots(recorded, before); },
				[recorded, after = pair]() { restoreSlots(recorded, after); });
		}
	}
	return ControlResult::success(abJson(pair));
}

//! 0.5.0 theme 12: the device at slot @a from of the chain @a args address (found again on every run - an undo can
//! make a rack chain a new object) put at slot @a to; moveUp/moveDown signal nothing, so the views are told here.
bool reslot(const QJsonObject& args, int from, int to)
{
	ControlTarget target;
	ControlResult ignored;
	if (!resolveChainTarget(args, &target, &ignored)) { return false; }
	const int count = static_cast<int>(target.chain->effects().size());
	if (from < 0 || from >= count || to < 0 || to >= count) { return false; }
	Effect* effect = target.chain->effects()[from];
	for (int slot = from; slot > to; --slot) { target.chain->moveUp(effect); }
	for (int slot = from; slot < to; ++slot) { target.chain->moveDown(effect); }
	emit target.chain->dataChanged();
	return true;
}

//! plugin.move: one device to another slot of its own chain, one undo step (recorded by chain and slot, not by pointer).
ControlResult pluginMove(const QJsonObject& args)
{
	ControlTarget target;
	ControlResult error;
	if (!resolveChainTarget(args, &target, &error)) { return error; }
	Effect* effect = resolveControlEffect(target, args.value(QStringLiteral("plugin")).toString(), &error);
	if (effect == nullptr) { return error; }
	const std::vector<Effect*>& effects = target.chain->effects();
	const int from = static_cast<int>(std::find(effects.begin(), effects.end(), effect) - effects.begin());
	const int to = std::clamp(args.value(QStringLiteral("position")).toInt(), 0, static_cast<int>(effects.size()) - 1);
	const QJsonObject result{{QStringLiteral("plugin"), effectIdOf(effect)}, {QStringLiteral("from"), from}, {QStringLiteral("position"), to}};
	if (from == to) { return ControlResult::success(result); }  // already there: nothing changed, no step
	QJsonObject chain{{QStringLiteral("target"), target.id}};
	if (args.contains(QStringLiteral("chain"))) { chain.insert(QStringLiteral("chain"), args.value(QStringLiteral("chain"))); }
	reslot(chain, from, to);
	addUndoStep([chain, from, to]() { reslot(chain, to, from); }, [chain, from, to]() { reslot(chain, from, to); });
	Engine::getSong()->setModified();
	return ControlResult::success(result);
}

} // namespace

struct control::AbSlotsSnapshot
{
	QString a;
	QString b;
	bool onB = false;
	std::vector<float> aValues;
	std::vector<float> bValues;
};

bool control::abSlotState(const Effect* effect, AbState* state)
{
	const auto found = effect != nullptr ? abSlots().find(effect->id()) : abSlots().end();
	if (found == abSlots().end() || found->second.owner != effect || (found->second.a.isEmpty() && found->second.b.isEmpty()))
	{
		return false;
	}
	*state = AbState{found->second.onB, !found->second.a.isEmpty(), !found->second.b.isEmpty()};
	return true;
}

std::shared_ptr<control::AbSlotsSnapshot> control::captureAbSlotsForRemoval(Effect* effect)
{
	if (effect == nullptr) { return {}; }
	const auto found = abSlots().find(effect->id());
	if (found == abSlots().end() || found->second.owner != effect) { return {}; }
	auto snapshot = std::make_shared<AbSlotsSnapshot>();
	snapshot->a = found->second.a;
	snapshot->b = found->second.b;
	snapshot->onB = found->second.onB;
	snapshot->aValues = found->second.aValues;
	snapshot->bValues = found->second.bValues;
	return snapshot;
}

void control::restoreAbSlotsAfterRemoval(Effect* effect, const std::shared_ptr<AbSlotsSnapshot>& snapshot)
{
	if (effect == nullptr || !snapshot) { return; }
	AbSlots& pair = claimAbSlots(effect);
	pair.a = snapshot->a;
	pair.b = snapshot->b;
	pair.onB = snapshot->onB;
	pair.aValues = snapshot->aValues;
	pair.bValues = snapshot->bValues;
	// claimAbSlots binds the record to the rebuilt effect and releases it with that effect.
}

std::size_t control::abSlotsSnapshotSize(const std::shared_ptr<AbSlotsSnapshot>& snapshot)
{
	if (!snapshot) { return 0; }
	return sizeof(AbSlotsSnapshot) +
		(static_cast<std::size_t>(snapshot->a.size()) + static_cast<std::size_t>(snapshot->b.size())) * sizeof(QChar) +
		(snapshot->aValues.size() + snapshot->bValues.size()) * sizeof(float);
}

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

std::size_t control::abSlotEntryCountForTesting()
{
	return abSlots().size();
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
	// W1: 'to' is one destination or a list of them (Copy to Channels), all in one undo step.
	const QJsonObject toOneOrMany{{QStringLiteral("type"), QJsonArray{QStringLiteral("string"), QStringLiteral("array")}}};
	one.insert(QStringLiteral("to"), toOneOrMany);
	one.insert(QStringLiteral("to_chain"), integerProperty(0, 64));
	one.insert(QStringLiteral("position"), integerProperty(0, 1024));
	add(registry, "plugin.copy", QStringLiteral("Copy one device ('plugin', fx-<n> on 'target' - with 'chain' for a rack chain) "
		"to another chain: 'to' (trk-<n> / ch-<n>, or a list of them; 'to_chain' for a rack chain), at 'position' (0 = first; "
		"appended when left out), with its settings. 'plugins' answers the copies' fx-<n>, 'copies' each destination's; a copy "
		"that cannot be made stops the rest ('stopped'). One undo step, however many destinations."),
		one, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("to")}, pluginCopy);
	QJsonObject whole = source;
	whole.insert(QStringLiteral("to"), toOneOrMany);
	whole.insert(QStringLiteral("to_chain"), integerProperty(0, 64));
	whole.insert(QStringLiteral("replace"), booleanProperty());
	add(registry, "chain.copy", QStringLiteral("Copy every device of a chain ('target', 'chain') to another ('to', or a list of "
		"them; 'to_chain'), in order, with their settings: appended, or with 'replace' in place of the devices there. 'copies' "
		"says what each destination got; a device that cannot be made stops the rest ('stopped'). One undo step, however many "
		"destinations."),
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
	QJsonObject move = source;  // 0.5.0 theme 12: the device card's Move Left / Move Right
	move.insert(QStringLiteral("plugin"), stringProperty());
	move.insert(QStringLiteral("position"), integerProperty(0, 1024));
	add(registry, "plugin.move", QStringLiteral("Move one device ('plugin', fx-<n> on 'target' - with 'chain' for a rack chain) to "
		"slot 'position' of its own chain (0 = first; clamped to the last slot). 'from' answers where it was. One undo step; a "
		"device already at that slot changes nothing."),
		move, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("position")}, pluginMove);
}

} // namespace lmms
