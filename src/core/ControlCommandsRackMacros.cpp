/*
 * ControlCommandsRackMacros.cpp - the macro half of the rack.* command group
 *                                 (SPEC A11-A16).
 *
 * A macro is a named, persisted scalar (0..1) that drives a set of existing
 * model parameters, each through its own range window: the window is stated as
 * a FRACTION of the parameter's own min..max (see RackMacroTarget), so the
 * assignment survives a parameter whose range the engine defines rather than
 * pinning it to the numbers one build happened to report.
 *
 * What a macro writes is a real AutomatableModel - the same object
 * plugin.param_set writes - so a macro edit is a project edit, it is
 * journalled, and control.undo takes it back in one step.
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

#include <functional>
#include <utility>
#include <vector>

#include <QJsonObject>

#include "AutomatableModel.h"
#include "ControlEdit.h"
#include "ControlRackSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Mixer.h"
#include "Rack.h"
#include "RackMacros.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! A 0..1 number property. ControlVocabulary.h has no bounded number primitive
//! and mixer.set_volume states this shape inline, so it is stated here once,
//! for the three arguments that need it, rather than three times.
QJsonObject unitProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
		{QStringLiteral("minimum"), 0.0},
		{QStringLiteral("maximum"), 1.0}};
}

//! The target described by a macro_target_add call.
RackMacroTarget targetFromArgs(const QJsonObject& args)
{
	RackMacroTarget target;
	target.chain = static_cast<int>(args.value(QStringLiteral("chain")).toDouble());
	target.effect = static_cast<int>(args.value(QStringLiteral("effect")).toDouble());
	target.parameter = args.value(QStringLiteral("parameter")).toString();
	target.low = static_cast<float>(args.value(QStringLiteral("low")).toDouble(0.0));
	target.high = static_cast<float>(args.value(QStringLiteral("high")).toDouble(1.0));
	return target;
}

void registerMacroAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.macro_add");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("macro_add");
	cmd.description = QStringLiteral("Add a macro to a channel's rack: a named scalar (0..1) that "
		"drives parameters bound with rack.macro_target_add. Returns its macro-<n> id. Reversible "
		"through the ProjectJournal (an action checkpoint removes the macro this command created).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("value"), unitProperty()},
	}, {QStringLiteral("channel"), QStringLiteral("name")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("macro"), stringProperty()},
		{QStringLiteral("macro_count"), integerProperty()},
		{QStringLiteral("macro_state"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		const QString name = args.value(QStringLiteral("name")).toString();
		if (name.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'name' must not be empty: a macro with no name cannot be "
					"identified in a project file"));
		}
		const float value = static_cast<float>(args.value(QStringLiteral("value")).toDouble(0.0));
		Rack& rack = channel->m_rack;
		const QString channelIdText = channelIdOf(channel);
		const int before = rack.macros().macroCount();
		const int index = rack.macros().addMacro(name, value);

		addUndoStep(
			[channelIdText, index]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->macros().removeMacro(index); }
			},
			[channelIdText, name, value]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->macros().addMacro(name, value); }
			});

		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("macro"), macroId(index));
		result.insert(QStringLiteral("macro_count"), rack.macros().macroCount());
		result.insert(QStringLiteral("macro_state"), macroJson(*rack.macros().macro(index), index));
		QJsonObject beforeState;
		beforeState.insert(QStringLiteral("macro_count"), before);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(beforeState, QStringLiteral("rack.macro_remove"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("macro"), macroId(index)}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step removes the macro this "
					"command created, through the same RackMacros::removeMacro rack.macro_remove "
					"uses; a new macro carries only its name, its value and no targets")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerMacroRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.macro_remove");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("macro_remove");
	cmd.description = QStringLiteral("Drop a macro from a channel's rack, targets included. "
		"Reversible through the ProjectJournal (the recorded undo step re-inserts the captured "
		"macro at its index).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("macro"), stringProperty()},
	}, {QStringLiteral("channel"), QStringLiteral("macro")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("removed"), stringProperty()},
		{QStringLiteral("macro_count"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const int index = resolveMacroIndex(rack, args.value(QStringLiteral("macro")).toString(),
			&error);
		if (index < 0) { return error; }

		const RackMacro captured = *rack.macros().macro(index);
		const QString channelIdText = channelIdOf(channel);
		rack.macros().removeMacro(index);
		// A macro is pure data (a name, a scalar and a target list), so
		// re-inserting the captured one at its own index restores the list -
		// and the macro-<n> ids - exactly.
		addUndoStep(
			[channelIdText, index, captured]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->macros().insertMacro(index, captured); }
			},
			[channelIdText, index]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->macros().removeMacro(index); }
			});

		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("removed"), macroId(index));
		result.insert(QStringLiteral("macro_count"), rack.macros().macroCount());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(macroJson(captured, index), QStringLiteral("rack.macro_add"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("macro"), macroId(index)}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step re-inserts the captured "
					"macro - name, value and every target - at its own index, so the list and "
					"the macro-<n> ids come back exactly")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerMacroTargetAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.macro_target_add");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("macro_target_add");
	cmd.description = QStringLiteral("Bind an existing parameter to a macro: 'chain' and 'effect' "
		"address a device in the rack's chains (fx-<n> order), 'parameter' is its display name as "
		"plugin.param_get reports it, and 'low'/'high' are the window as a fraction (0..1) of the "
		"parameter's own range. Refused when the parameter does not resolve, so a macro never "
		"carries a target that can only fail. Reversible through the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("macro"), stringProperty()},
		{QStringLiteral("chain"), integerProperty()},
		{QStringLiteral("effect"), integerProperty()},
		{QStringLiteral("parameter"), stringProperty()},
		{QStringLiteral("low"), unitProperty()},
		{QStringLiteral("high"), unitProperty()},
	}, {QStringLiteral("channel"), QStringLiteral("macro"), QStringLiteral("chain"),
		QStringLiteral("effect"), QStringLiteral("parameter")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("macro"), stringProperty()},
		{QStringLiteral("target_index"), integerProperty()},
		{QStringLiteral("target_count"), integerProperty()},
		{QStringLiteral("macro_state"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const int macro = resolveMacroIndex(rack, args.value(QStringLiteral("macro")).toString(),
			&error);
		if (macro < 0) { return error; }

		const RackMacroTarget target = targetFromArgs(args);
		if (!isValidMacroTarget(target))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("the window %1..%2 is not a fraction pair inside 0..1")
					.arg(static_cast<double>(target.low))
					.arg(static_cast<double>(target.high)));
		}
		QString why;
		// Refused at BIND time: a stored target that names nothing (or names an
		// ambiguous parameter) would only ever fail at apply time, silently.
		if (rackMacroTargetModel(rack, target, &why) == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' does not resolve to a parameter: %2")
					.arg(target.parameter, why));
		}

		const QString channelIdText = channelIdOf(channel);
		const int index = rack.macros().addTarget(macro, target);

		addUndoStep(
			[channelIdText, macro, index]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->macros().removeTarget(macro, index); }
			},
			[channelIdText, macro, index, target]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->macros().insertTarget(macro, index, target); }
			});

		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("macro"), macroId(macro));
		result.insert(QStringLiteral("target_index"), index);
		result.insert(QStringLiteral("target_count"),
			static_cast<int>(rack.macros().macro(macro)->targets.size()));
		result.insert(QStringLiteral("macro_state"), macroJson(*rack.macros().macro(macro), macro));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(macroTargetJson(target, index),
				QStringLiteral("rack.macro_target_remove"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("macro"), macroId(macro)},
					{QStringLiteral("target"), index}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step removes the target this "
					"command appended (RackMacros::removeTarget at the same index)")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerMacroTargetRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.macro_target_remove");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("macro_target_remove");
	cmd.description = QStringLiteral("Unbind one parameter from a macro, by its target index as "
		"rack.get_state reports it. Reversible through the ProjectJournal (the recorded undo step "
		"re-inserts the captured target at its index).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("macro"), stringProperty()},
		{QStringLiteral("target"), integerProperty()},
	}, {QStringLiteral("channel"), QStringLiteral("macro"), QStringLiteral("target")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("macro"), stringProperty()},
		{QStringLiteral("removed"), objectProperty()},
		{QStringLiteral("target_count"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const int macro = resolveMacroIndex(rack, args.value(QStringLiteral("macro")).toString(),
			&error);
		if (macro < 0) { return error; }

		const RackMacro* entry = rack.macros().macro(macro);
		const int index = static_cast<int>(args.value(QStringLiteral("target")).toDouble());
		if (index < 0 || index >= static_cast<int>(entry->targets.size()))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("macro %1 has no target %2 (it has %3)")
					.arg(macroId(macro))
					.arg(index)
					.arg(static_cast<int>(entry->targets.size())));
		}

		const RackMacroTarget captured = entry->targets[static_cast<std::size_t>(index)];
		const QString channelIdText = channelIdOf(channel);
		rack.macros().removeTarget(macro, index);

		addUndoStep(
			[channelIdText, macro, index, captured]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->macros().insertTarget(macro, index, captured); }
			},
			[channelIdText, macro, index]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->macros().removeTarget(macro, index); }
			});

		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("macro"), macroId(macro));
		result.insert(QStringLiteral("removed"), macroTargetJson(captured, index));
		result.insert(QStringLiteral("target_count"),
			static_cast<int>(rack.macros().macro(macro)->targets.size()));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(macroTargetJson(captured, index),
				QStringLiteral("rack.macro_target_add"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("macro"), macroId(macro)},
					{QStringLiteral("chain"), captured.chain},
					{QStringLiteral("effect"), captured.effect},
					{QStringLiteral("parameter"), captured.parameter},
					{QStringLiteral("low"), static_cast<double>(captured.low)},
					{QStringLiteral("high"), static_cast<double>(captured.high)}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step re-inserts the captured "
					"target at its own index, so the bind order - which is the order the targets "
					"are applied in - comes back exactly")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerMacroSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.macro_set");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("macro_set");
	cmd.description = QStringLiteral("Set a macro's value (0..1) and drive every bound parameter "
		"through its own window in one step. Parameters whose device or name no longer exists are "
		"skipped and counted ('skipped'), never guessed at. Reversible through the ProjectJournal "
		"(one action checkpoint restores the macro and every parameter it wrote).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("macro"), stringProperty()},
		{QStringLiteral("value"), unitProperty()},
	}, {QStringLiteral("channel"), QStringLiteral("macro"), QStringLiteral("value")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("macro"), stringProperty()},
		{QStringLiteral("value"), numberProperty()},
		{QStringLiteral("applied"), integerProperty()},
		{QStringLiteral("skipped"), integerProperty()},
		{QStringLiteral("writes"), arrayProperty()},
		{QStringLiteral("macro_state"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const int macro = resolveMacroIndex(rack, args.value(QStringLiteral("macro")).toString(),
			&error);
		if (macro < 0) { return error; }

		const float previousValue = rack.macros().macro(macro)->value;
		const float value = static_cast<float>(args.value(QStringLiteral("value")).toDouble());
		const std::size_t targetCount = rack.macros().macro(macro)->targets.size();
		const QString channelIdText = channelIdOf(channel);

		rack.macros().setValue(macro, value);
		const std::vector<RackMacroWrite> writes = rack.macros().apply(macro, rack);

		MacroRestore undo;
		undo.channelId = channelIdText;
		undo.macro = macro;
		undo.value = previousValue;
		MacroRestore redo;
		redo.channelId = channelIdText;
		redo.macro = macro;
		redo.value = rack.macros().macro(macro)->value;
		QJsonArray written;
		for (const RackMacroWrite& write : writes)
		{
			undo.parameters.emplace_back(write.target, write.previous);
			redo.parameters.emplace_back(write.target, write.written);
			QJsonObject entry = macroTargetJson(write.target, 0);
			entry.insert(QStringLiteral("previous"), static_cast<double>(write.previous));
			entry.insert(QStringLiteral("written"), static_cast<double>(write.written));
			written.append(entry);
		}
		// ONE undo step for the macro's own scalar AND every parameter it
		// wrote: the step re-resolves each target at undo time, so it carries no
		// raw model pointer.
		addUndoStep([undo]() { writeMacroAssignments(undo); },
			[redo]() { writeMacroAssignments(redo); });

		const int applied = static_cast<int>(writes.size());
		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("macro"), macroId(macro));
		result.insert(QStringLiteral("value"), static_cast<double>(rack.macros().macro(macro)->value));
		result.insert(QStringLiteral("applied"), applied);
		result.insert(QStringLiteral("skipped"),
			static_cast<int>(targetCount) - applied);
		result.insert(QStringLiteral("writes"), written);
		result.insert(QStringLiteral("macro_state"), macroJson(*rack.macros().macro(macro), macro));
		QJsonObject beforeState = macroJson(*rack.macros().macro(macro), macro);
		beforeState.insert(QStringLiteral("value"), static_cast<double>(previousValue));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(beforeState, QStringLiteral("rack.macro_set"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("macro"), macroId(macro)},
					{QStringLiteral("value"), static_cast<double>(previousValue)}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step puts the macro's own "
					"value back and writes every parameter the call changed back to the value it "
					"had, re-resolved by chain/effect/parameter name at undo time")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerRackMacroCommands(ControlRegistry& registry)
{
	registerMacroAdd(registry);
	registerMacroRemove(registry);
	registerMacroTargetAdd(registry);
	registerMacroTargetRemove(registry);
	registerMacroSet(registry);
}

} // namespace lmms
