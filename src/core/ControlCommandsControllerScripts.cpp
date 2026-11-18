/*
 * ControlCommandsControllerScripts.cpp - controller.script_*: Lua control-surface scripts (Q050-081)
 *                                       reversibility contract (0.5.0 F6)
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

/*! Start, stop and read the Lua control-surface scripts (ControllerScript.h). A running script is device
 *  state, like a connected Mackie surface: nothing here is recorded, and what the script then does records as
 *  the commands it runs (zene.command), each with its own row. The rows are below, in this file's table. */

#include <QFileInfo>
#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "ControllerScript.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult scriptStart(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	if (!QFileInfo(path).isAbsolute())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'path' must be an absolute file path"));
	}
	const ControllerScript::Options options{args.value(QStringLiteral("name")).toString(), path,
		args.value(QStringLiteral("input")).toString(), args.value(QStringLiteral("output")).toString()};
	QString error;
	ControllerScript* script = ControllerScript::start(options, nullptr, &error);
	if (script == nullptr) { return ControlResult::failure(ControlErrorKind::Refused, error); }
	return ControlResult::success(script->state());
}

ControlResult scriptStop(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	if (!ControllerScript::stop(name))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no controller script '%1' is running").arg(name));
	}
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("stopped"), true}});
}

ControlResult scriptState(const QJsonObject& args)
{
	const QString only = args.value(QStringLiteral("name")).toString();
	QJsonArray scripts;
	for (const QString& name : ControllerScript::names())
	{
		if (only.isEmpty() || only == name) { scripts.append(ControllerScript::find(name)->state()); }
	}
	if (!only.isEmpty() && scripts.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no controller script '%1' is running").arg(only));
	}
	return ControlResult::success({{QStringLiteral("scripts"), scripts}, {QStringLiteral("count"), scripts.size()}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&))
{
	defineCommandWithSchema(registry, QStringLiteral("controller.") + QLatin1String(verb), description, args, false, handler);
}

using RC = ReversibilityClass;

#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kScriptRows[] = {
	R("controller.script_start", RC::NotMutating, false, "it starts a Lua control-surface script on MIDI ports: device "
		"state, not the project; each command the script runs records as itself", "no project write (controller.script_stop)", ""),
	R("controller.script_stop", RC::NotMutating, false, "it stops a control-surface script and releases its ports: device "
		"state", "no project write (controller.script_start)", ""),
	R("controller.script_state", RC::NotMutating, false, "it reads the running control-surface scripts and their counters. "
		"Nothing is written", "no write", ""),
};

#undef R

} // namespace

void registerControllerScriptCommands(ControlRegistry& registry)
{
	add(registry, "script_start", QStringLiteral("Start a Lua control-surface script ('path', an absolute .lua file) under "
		"'name', bound to the MIDI client's 'input' and 'output' ports (midi.device_list / surface.mcu_ports names). "
		"Each event from the input calls the script's on_midi(event) - event.type (note_on, note_off, cc, pitch_bend, "
		"...), channel 1..16, key/velocity or controller/value - and the script runs registered commands with "
		"zene.command(id, args) and sends feedback to the output with surface.send_cc(channel, cc, value) and "
		"surface.send_note(channel, key, velocity). Sandboxed (no files, no OS), 1,000,000 Lua instructions per call, "
		"8 MiB of Lua memory; 8 errors in a row stop it, and a lost input or output port is reported and re-bound when it "
		"returns. Replaces a running script of that name. Device state: not recorded, not saved."),
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("path"), stringProperty()},
			{QStringLiteral("input"), stringProperty()}, {QStringLiteral("output"), stringProperty()}},
			QJsonArray{QStringLiteral("name"), QStringLiteral("path")}), scriptStart);
	add(registry, "script_stop", QStringLiteral("Stop the control-surface script 'name' and release its MIDI ports."),
		objectSchema({{QStringLiteral("name"), stringProperty()}}, QJsonArray{QStringLiteral("name")}), scriptStop);
	add(registry, "script_state", QStringLiteral("The running control-surface scripts (or the one 'name'): their ports, "
		"whether they are faulted or have lost a bound MIDI port, how many events they received, ran and dropped, the "
		"commands and feedback they sent, their errors and recent log lines."),
		objectSchema({{QStringLiteral("name"), stringProperty()}}), scriptState);
}

namespace control
{

const ReversibilityRow* reversibilityControllerScriptRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = static_cast<int>(sizeof(kScriptRows) / sizeof(kScriptRows[0])); }
	return kScriptRows;
}

} // namespace control

} // namespace lmms
