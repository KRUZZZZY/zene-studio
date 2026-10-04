/*
 * ControlCommandsRackMacroPair.cpp - rack.macro_set_pair: two macros as one step (the XY pad), and the shared macro write
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
/*! W1 (0.5.0 catch-up): the XY pad drives two macros at once. Two rack.macro_set calls per mouse move alternated between
 *  the macros, so the coalescing rule (same command, same target) opened a step for every one of them and a drag cost
 *  dozens of undos; neither call's result was read either. rack.macro_set_pair sets both macros in one call - both are
 *  resolved before either is written, so the pair is set whole or refused whole - and records ONE undo step, which the
 *  coalescing rule (target "channel,x_macro,y_macro") extends for the length of a drag. */

#include <QJsonObject>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRackMacroWrites.h"
#include "ControlRackSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Mixer.h"
#include "Rack.h"
#include "RackMacros.h"

namespace lmms
{

using namespace control;

MacroApplied applyMacroValue(Rack& rack, const QString& channelId, int macro, float value)
{
	MacroApplied out;
	out.undo.channelId = channelId;
	out.undo.macro = macro;
	out.undo.value = rack.macros().macro(macro)->value;
	const auto targets = static_cast<int>(rack.macros().macro(macro)->targets.size());
	rack.macros().setValue(macro, value);
	const std::vector<RackMacroWrite> writes = rack.macros().apply(macro, rack);
	out.redo.channelId = channelId;
	out.redo.macro = macro;
	out.redo.value = rack.macros().macro(macro)->value;
	for (const RackMacroWrite& write : writes)
	{
		out.undo.parameters.emplace_back(write.target, write.previous);
		out.redo.parameters.emplace_back(write.target, write.written);
		QJsonObject entry = macroTargetJson(write.target, 0);
		entry.insert(QStringLiteral("previous"), static_cast<double>(write.previous));
		entry.insert(QStringLiteral("written"), static_cast<double>(write.written));
		out.writes.append(entry);
	}
	out.skipped = targets - static_cast<int>(writes.size());
	return out;
}

namespace
{

//! One axis of the reply: the macro, the value it holds now, and what it wrote.
QJsonObject axisJson(const Rack& rack, int macro, const MacroApplied& applied)
{
	return {{QStringLiteral("macro"), macroId(macro)}, {QStringLiteral("value"), static_cast<double>(rack.macros().macro(macro)->value)},
		{QStringLiteral("applied"), static_cast<int>(applied.writes.size())}, {QStringLiteral("skipped"), applied.skipped},
		{QStringLiteral("writes"), applied.writes}};
}

//! The two macros a call names, both resolved before anything is written; false with @a error set otherwise.
bool resolvePair(const Rack& rack, const QJsonObject& args, int* x, int* y, ControlResult* error)
{
	*x = resolveMacroIndex(rack, args.value(QStringLiteral("x_macro")).toString(), error);
	if (*x < 0) { return false; }
	*y = resolveMacroIndex(rack, args.value(QStringLiteral("y_macro")).toString(), error);
	if (*y < 0) { return false; }
	if (*x == *y)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'x_macro' and 'y_macro' name the same "
			"macro; set one macro with rack.macro_set"));
		return false;
	}
	return true;
}

ControlResult setPair(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	Rack& rack = channel->m_rack;
	int x = -1;
	int y = -1;
	if (!resolvePair(rack, args, &x, &y, &error)) { return error; }
	const QString id = channelIdOf(channel);
	const QJsonObject before{{QStringLiteral("channel"), id}, {QStringLiteral("x_macro"), macroId(x)},
		{QStringLiteral("x_value"), static_cast<double>(rack.macros().macro(x)->value)}, {QStringLiteral("y_macro"), macroId(y)},
		{QStringLiteral("y_value"), static_cast<double>(rack.macros().macro(y)->value)}};
	const MacroApplied ax = applyMacroValue(rack, id, x, static_cast<float>(args.value(QStringLiteral("x_value")).toDouble()));
	const MacroApplied ay = applyMacroValue(rack, id, y, static_cast<float>(args.value(QStringLiteral("y_value")).toDouble()));
	// ONE step for both macros and every parameter they wrote. Undone in reverse, so a parameter both macros drive gets
	// back the value it had before X wrote it, not the one Y found.
	addUndoStep([ux = ax.undo, uy = ay.undo]() { writeMacroAssignments(uy); writeMacroAssignments(ux); },
		[rx = ax.redo, ry = ay.redo]() { writeMacroAssignments(rx); writeMacroAssignments(ry); });
	QJsonObject result{{QStringLiteral("channel"), id}, {QStringLiteral("x"), axisJson(rack, x, ax)}, {QStringLiteral("y"), axisJson(rack, y, ay)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload(before, QStringLiteral("rack.macro_set_pair"), before, true,
		QStringLiteral("action checkpoint: the recorded undo step puts both macros' own values back and writes every parameter "
			"the call changed back to the value it had, re-resolved by chain/effect/parameter name at undo time")));
	return ControlResult::success(result);
}

QJsonObject unitProperty()
{
	return {{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}};
}

} // namespace

void registerRackMacroPairCommand(ControlRegistry& registry)
{
	const QJsonObject args{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("x_macro"), stringProperty()},
		{QStringLiteral("x_value"), unitProperty()}, {QStringLiteral("y_macro"), stringProperty()},
		{QStringLiteral("y_value"), unitProperty()}};
	defineCommandWithSchema(registry, QStringLiteral("rack.macro_set_pair"), QStringLiteral("Set two macros of a channel's rack "
		"at once - the XY pad: 'x_macro' to 'x_value' and 'y_macro' to 'y_value' (0..1 each, two different macro-<n>), each "
		"driving its bound parameters as rack.macro_set does. Both macros are checked before either is written, so the pair "
		"is set whole or refused whole. ONE undo step; a run of calls on the same pair inside the coalescing window (a drag) "
		"stays one step. 'x' and 'y' answer each macro's value, applied/skipped counts and writes."),
		objectSchema(args, {QStringLiteral("channel"), QStringLiteral("x_macro"), QStringLiteral("x_value"),
			QStringLiteral("y_macro"), QStringLiteral("y_value")}), true, setPair);
}

} // namespace lmms
