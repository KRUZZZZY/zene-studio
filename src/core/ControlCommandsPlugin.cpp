/*
 * ControlCommandsPlugin.cpp - the plugin.list / load / unload / bypass commands
 *                             (SPEC A11-A16).
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

#include <memory>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"

#include "ControlVocabulary.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Instrument.h"
#include "InstrumentTrack.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

// Bounds the state XML a transaction keeps. SPEC A16's fallback is a *bounded*
// snapshot, not an unbounded one.
constexpr int MaxSnapshotChars = 65536;

//! Which catalogue entries plugin.list returns for the given filters. The id
//! reported is the entry's index in the *whole* catalogue, never in the
//! filtered subset, so a dev-<n> means one device whatever the filter was.
bool matchesDeviceFilter(const ControlDeviceEntry& entry, const QString& format,
	const QString& kind, bool loadableOnly)
{
	if (!format.isEmpty() && entry.format != format) { return false; }
	if (!kind.isEmpty() && entry.kind != kind) { return false; }
	if (loadableOnly && !entry.loadable) { return false; }
	return true;
}

//! The state XML of \a effect, capped at MaxSnapshotChars.
QJsonObject stateSnapshot(Effect* effect)
{
	QJsonObject snapshot;
	QString xml = controlEffectStateXml(effect);
	const bool truncated = xml.size() > MaxSnapshotChars;
	if (truncated) { xml.truncate(MaxSnapshotChars); }
	snapshot.insert(QStringLiteral("state_xml"), xml);
	snapshot.insert(QStringLiteral("state_truncated"), truncated);
	return snapshot;
}

void registerPluginList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.list");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("list");
	cmd.description = QStringLiteral("Every device this build can load: built-in effect and "
		"instrument modules plus the devices of the shipped hosting formats (LADSPA and LV2), "
		"each with a dev-<n> id that is deterministic for the binary. dev-<n> is a catalogue "
		"index, not a persisted project id, and 'loadable' says whether plugin.load accepts the "
		"entry. Format order is built-in, then LADSPA, then LV2, so adding a host does not "
		"renumber the ids of the formats that were already there.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
			{QStringLiteral("enum"), QJsonArray{QStringLiteral("builtin"), QStringLiteral("ladspa"),
				QStringLiteral("lv2")}}}},
		{QStringLiteral("kind"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
			{QStringLiteral("enum"), QJsonArray{QStringLiteral("effect"),
				QStringLiteral("instrument"), QStringLiteral("tool"), QStringLiteral("other")}}}},
		{QStringLiteral("loadable_only"), booleanProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("devices"), arrayProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("total"), integerProperty()},
		{QStringLiteral("loadable_count"), integerProperty()},
		{QStringLiteral("counts_by_format"), objectSchema({})},
		{QStringLiteral("counts_by_kind"), objectSchema({})},
	});
	cmd.handler = [](const QJsonObject& args) {
		const QString format = args.value(QStringLiteral("format")).toString();
		const QString kind = args.value(QStringLiteral("kind")).toString();
		const bool loadableOnly = args.value(QStringLiteral("loadable_only")).toBool(false);
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();

		QJsonArray list;
		QJsonObject byFormat;
		QJsonObject byKind;
		int loadable = 0;
		for (int i = 0; i < catalogue.size(); ++i)
		{
			const ControlDeviceEntry& entry = catalogue.at(i);
			if (!matchesDeviceFilter(entry, format, kind, loadableOnly)) { continue; }
			list.append(controlDeviceJson(entry, i));
			byFormat.insert(entry.format, byFormat.value(entry.format).toInt() + 1);
			byKind.insert(entry.kind, byKind.value(entry.kind).toInt() + 1);
			if (entry.loadable) { ++loadable; }
		}

		QJsonObject result;
		result.insert(QStringLiteral("devices"), list);
		result.insert(QStringLiteral("count"), list.size());
		result.insert(QStringLiteral("total"), catalogue.size());
		result.insert(QStringLiteral("loadable_count"), loadable);
		result.insert(QStringLiteral("counts_by_format"), byFormat);
		result.insert(QStringLiteral("counts_by_kind"), byKind);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! Appends \a entry to the target's chain and describes the new instance.
ControlResult loadEffect(const ControlTarget& target, const ControlDeviceEntry& entry,
	int deviceIndex)
{
	ControlResult error;
	Effect* effect = controlInstantiateDevice(entry, target.chain, &error);
	if (effect == nullptr) { return error; }
	const int index = static_cast<int>(target.chain->effects().size()) - 1;

	// SPEC A16: an APPENDED effect carries only defaults, so the inverse is the
	// OPERATION and it is exact - ONE action step unloads the instance it
	// created, and its redo instantiates the same catalogue entry again. (An
	// instrument REPLACEMENT has no such inverse: see loadInstrument.)
	auto holder = std::make_shared<Effect*>(effect);
	EffectChain* chain = target.chain;
	control::addUndoStep(
		[holder, chain]() {
			if (*holder != nullptr)
			{
				chain->removeEffect(*holder);
				(*holder)->deleteLater();
				*holder = nullptr;
			}
		},
		[holder, chain, entry]() {
			ControlResult ignored;
			*holder = controlInstantiateDevice(entry, chain, &ignored);
		});

	QJsonObject result;
	result.insert(QStringLiteral("target"), target.id);
	result.insert(QStringLiteral("kind"), QStringLiteral("effect"));
	result.insert(QStringLiteral("id"), control::effectId(index));
	result.insert(QStringLiteral("index"), index);
	result.insert(QStringLiteral("device"), control::deviceId(deviceIndex));
	result.insert(QStringLiteral("plugin"), QString::fromUtf8(effect->descriptor()->name));
	result.insert(QStringLiteral("display_name"),
		QString::fromUtf8(effect->descriptor()->displayName));

	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"),
		QJsonObject{{QStringLiteral("target"), target.id},
			{QStringLiteral("devices_on_target"), index}});
	transaction.insert(QStringLiteral("inverse"),
		QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.unload")},
			{QStringLiteral("args"),
				QJsonObject{{QStringLiteral("target"), target.id},
					{QStringLiteral("plugin"), control::effectId(index)}}}});
	transaction.insert(QStringLiteral("reversible"), true);
	transaction.insert(QStringLiteral("mechanism"),
		QStringLiteral("action checkpoint: the recorded undo step unloads the device this command "
			"appended, through the same EffectChain::removeEffect + deleteLater path "
			"plugin.unload uses; an appended effect carries only defaults, so unloading it "
			"restores the chain exactly"));
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

//! Replaces the instrument of an instrument track.
ControlResult loadInstrument(const ControlTarget& target, const ControlDeviceEntry& entry,
	int deviceIndex)
{
	if (target.instrumentTrack == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' is a %2 target: an instrument loads onto an instrument track")
				.arg(target.id, target.typeName));
	}
	const Instrument* previousInstrument = target.instrumentTrack->instrument();
	const QString previous = previousInstrument != nullptr
		? QString::fromUtf8(previousInstrument->descriptor()->name)
		: QString();

	ControlResult error;
	QString pluginName;
	Plugin::Descriptor::SubPluginFeatures::Key key;
	bool useKey = false;
	if (!controlDeviceModule(entry, &pluginName, &key, &useKey, &error)) { return error; }
	if (!controlPluginIsInstantiable(pluginName, &error)) { return error; }
	Instrument* instrument = target.instrumentTrack->loadInstrument(pluginName,
		useKey ? &key : nullptr);
	if (instrument == nullptr ||
		QString::fromUtf8(instrument->descriptor()->name) != pluginName)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the engine could not load instrument '%1'").arg(pluginName));
	}

	QJsonObject result;
	result.insert(QStringLiteral("target"), target.id);
	result.insert(QStringLiteral("kind"), QStringLiteral("instrument"));
	result.insert(QStringLiteral("id"), QStringLiteral("inst"));
	result.insert(QStringLiteral("device"), control::deviceId(deviceIndex));
	result.insert(QStringLiteral("plugin"), pluginName);
	result.insert(QStringLiteral("display_name"),
		entry.displayName.isEmpty() ? entry.name : entry.displayName);
	result.insert(QStringLiteral("previous_plugin"), previous);

	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"),
		QJsonObject{{QStringLiteral("target"), target.id},
			{QStringLiteral("plugin"), previous}});
	transaction.insert(QStringLiteral("inverse"),
		QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.load")},
			{QStringLiteral("args"),
				QJsonObject{{QStringLiteral("target"), target.id},
					{QStringLiteral("note"), QStringLiteral("reload the previous instrument by "
						"device id; its parameter values are not restored")}}}});
	transaction.insert(QStringLiteral("reversible"), false);
	transaction.insert(QStringLiteral("mechanism"),
		QStringLiteral("snapshot only: replacing an instrument has no ProjectJournal checkpoint "
			"and the replaced instrument's state is not preserved"));
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

void registerPluginLoad(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.load");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("load");
	cmd.description = QStringLiteral("Load a device by its dev-<n> id: an effect onto a track's "
		"device chain or a mixer channel's insert chain, an instrument onto an instrument track. "
		"Returns the new instance id (fx-<n>, or inst for an instrument). Headless-safe: it "
		"declares no 'requires' at all, because no editor is created here - neither a built-in "
		"plugin view nor an LV2 UI. An LV2 device whose bundle ships a GUI loads and is fully "
		"parametrisable through plugin.param_get / plugin.param_set on a display-less instance.");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("device"), stringProperty()}},
		{QStringLiteral("target"), QStringLiteral("device")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("kind"), stringProperty()},
		{QStringLiteral("id"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
		{
			return error;
		}
		ControlDeviceEntry entry;
		int deviceIndex = -1;
		if (!controlDeviceById(args.value(QStringLiteral("device")).toString(), &entry,
				&deviceIndex, &error))
		{
			return error;
		}
		if (entry.kind == QLatin1String("instrument"))
		{
			return loadInstrument(target, entry, deviceIndex);
		}
		return loadEffect(target, entry, deviceIndex);
	};
	registry.registerCommand(cmd);
}

void registerPluginUnload(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.unload");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("unload");
	cmd.description = QStringLiteral("Remove a device instance (fx-<n>) from a track's chain or a "
		"mixer channel. REVERSIBLE: the device's own state document is captured before the "
		"removal and one control.undo re-instantiates the same plugin at the same index in the "
		"chain with its settings restored. The transaction's 'before' snapshot still carries the "
		"state XML, so a client that prefers to rebuild by hand can; a device whose state exceeds "
		"the capture cap reports itself as snapshot-only instead.");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("plugin"), stringProperty()}},
		{QStringLiteral("target"), QStringLiteral("plugin")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("removed"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("index"), integerProperty()},
		{QStringLiteral("reversible"), booleanProperty()},
		{QStringLiteral("count"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
		{
			return error;
		}
		const QString pluginId = args.value(QStringLiteral("plugin")).toString();
		Effect* effect = resolveControlEffect(target, pluginId, &error);
		if (effect == nullptr) { return error; }
		const QString pluginName = QString::fromUtf8(effect->descriptor()->name);
		const int index = control::idToIndex(pluginId, QStringLiteral("fx-"));
		QJsonObject snapshot = stateSnapshot(effect);
		snapshot.insert(QStringLiteral("target"), target.id);
		snapshot.insert(QStringLiteral("index"), index);
		snapshot.insert(QStringLiteral("plugin"), pluginName);

		// SPEC A16 deliverable 5 / task #664: the removal is RECORDED before it
		// happens, and by the one implementation both this command and the
		// product's own rack share (control::journalEffectRemoval). The
		// difference it makes is the point: a removed device used to be
		// `reversible: false` - "snapshot only, rebuild it by hand from
		// before.state_xml" - because a deleted Effect has no live object a
		// checkpoint could restore. The recorded structural step RE-INSTANTIATES
		// it, at the index it was removed from, with its settings.
		const bool reversible = control::journalEffectRemoval(target.chain, effect, index);
		target.chain->removeEffect(effect);
		// Same lifetime as the rack's own delete: the audio engine may still
		// hold the pointer for the period in flight.
		effect->deleteLater();

		QJsonObject result;
		result.insert(QStringLiteral("target"), target.id);
		result.insert(QStringLiteral("removed"), pluginId);
		result.insert(QStringLiteral("plugin"), pluginName);
		result.insert(QStringLiteral("index"), index);
		result.insert(QStringLiteral("reversible"), reversible);
		result.insert(QStringLiteral("count"), static_cast<int>(target.chain->effects().size()));

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"), snapshot);
		transaction.insert(QStringLiteral("inverse"),
			reversible
				? QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.load")},
					{QStringLiteral("args"),
						QJsonObject{{QStringLiteral("target"), target.id},
							{QStringLiteral("note"), QStringLiteral("the recorded undo step "
								"re-instantiates the same dev-<n> at index %1 with its "
								"captured settings, so no reload is needed by hand")
								.arg(index)}}}}
				: QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.load")},
					{QStringLiteral("args"),
						QJsonObject{{QStringLiteral("target"), target.id},
							{QStringLiteral("note"), QStringLiteral("reload the same dev-<n>, then "
								"plugin.state_load from 'before.state_xml' written to a file")}}}});
		transaction.insert(QStringLiteral("reversible"), reversible);
		transaction.insert(QStringLiteral("mechanism"),
			reversible
				? QStringLiteral("action checkpoint (structural step): the device's own state "
					"document (controlEffectStateXml - including the sub-plugin key that "
					"identifies a hosted plugin) is captured before the removal, and the "
					"recorded undo step re-instantiates the device through the chain's own "
					"instantiate path and puts the settings back, at the index it was removed "
					"from. The document's measured size is charged to the undo stack's byte "
					"budget, so a long run of removals is evicted like any other step")
				: QStringLiteral("snapshot only: this device's state document is over the "
					"%1-character cap (or the engine's undo stack is not addressable), so no "
					"inverse was recorded; 'before.state_xml' is the bounded record that allows "
					"a rebuild by hand").arg(control::StructuralSnapshotLimit));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerPluginBypass(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.bypass");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("bypass");
	cmd.description = QStringLiteral("Switch a device instance off or on. This drives the same "
		"enabled control the rack's On/Off LED drives, so a bypassed device reports "
		"processing=false in dsp.get_state. Reversible through the ProjectJournal.");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("plugin"), stringProperty()},
			{QStringLiteral("bypass"), booleanProperty()}},
		{QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("bypass")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("processing"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
		{
			return error;
		}
		const QString pluginId = args.value(QStringLiteral("plugin")).toString();
		Effect* effect = resolveControlEffect(target, pluginId, &error);
		if (effect == nullptr) { return error; }

		const bool previouslyEnabled = effect->isEnabled();
		effect->setEnabled(!args.value(QStringLiteral("bypass")).toBool());

		QJsonObject result;
		result.insert(QStringLiteral("target"), target.id);
		result.insert(QStringLiteral("plugin"), pluginId);
		result.insert(QStringLiteral("enabled"), effect->isEnabled());
		result.insert(QStringLiteral("processing"), effect->isProcessingAudio());

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("enabled"), previouslyEnabled}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.bypass")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("target"), target.id},
						{QStringLiteral("plugin"), pluginId},
						{QStringLiteral("bypass"), previouslyEnabled}}}});
		transaction.insert(QStringLiteral("reversible"), true);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("ProjectJournal (Effect enabled-model checkpoint)"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerPluginDeviceCommands(ControlRegistry& registry)
{
	registerPluginList(registry);
	registerPluginLoad(registry);
	registerPluginUnload(registry);
	registerPluginBypass(registry);
}

void registerPluginCommands(ControlRegistry& registry)
{
	registerPluginDeviceCommands(registry);
	registerPluginParameterCommands(registry);
	registerPluginStateCommands(registry);
}

} // namespace lmms
