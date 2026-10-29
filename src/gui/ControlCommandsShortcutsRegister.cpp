/*
 * ControlCommandsShortcutsRegister.cpp - shortcuts.*: the registration table (ids, schemas, descriptions)
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


/*! The shortcuts.* registration table, split from ControlCommandsShortcuts.cpp (the handlers) so
 *  neither file outgrows the file-length gate once the panel scopes (Q050-055.1) and the shipped
 *  presets (Q050-202) both landed. Only the table is here: every handler is in the other file. */

#include <QJsonArray>

#include "ControlCommandsShortcutsShared.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "PanelShortcuts.h"

namespace lmms
{

using namespace control;
namespace ps = gui::panelshortcuts;

namespace
{

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	const QJsonObject& result, bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd = guiCommand("shortcuts", verb);
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = result;
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerShortcutCommands(ControlRegistry& registry)
{
	QJsonObject keys = stringProperty();
	keys.insert(QStringLiteral("format"), QStringLiteral("keys"));  // the command form captures a key press
	// Listing filters only panel scopes; set/reset also accept the empty menu scope because shortcut
	// page rows pass it explicitly as a fixed argument.
	QJsonObject panelScope = stringProperty();
	panelScope.insert(QStringLiteral("enum"), QJsonArray::fromStringList(ps::scopes()));
	QStringList bindingScopes = ps::scopes();
	bindingScopes.prepend(QString());
	QJsonObject bindingScope = stringProperty();
	bindingScope.insert(QStringLiteral("enum"), QJsonArray::fromStringList(bindingScopes));
	add(registry, "list", QStringLiteral("Every action that can carry a keyboard shortcut: the menu actions "
		"('scope' empty) and the arranger's, note editor's, automation editor's and Perform's own actions "
		"('scope' names which - the same key may differ in each). A binding is its id, its scope, the registry "
		"command it declares, its current keys, the code's own default and whether the user overrode it; "
		"'conflict' names the actions of the SAME scope already on those keys (a clash to resolve, while the "
		"same key in another scope is not one). 'query' filters by id, keys, command or scope; 'scope' keeps "
		"one context. 'conflict_count' is how many rows clash."),
		objectSchema({{QStringLiteral("query"), stringProperty()}, {QStringLiteral("scope"), panelScope}}),
		objectSchema({{QStringLiteral("bindings"), arrayProperty()}, {QStringLiteral("count"), integerProperty()},
			{QStringLiteral("overridden_count"), integerProperty()}, {QStringLiteral("conflict_count"), integerProperty()},
			{QStringLiteral("scopes"), arrayProperty()}}), false, shortcutcommands::list);
	add(registry, "set", QStringLiteral("Bind an action to 'keys' (portable text such as \"Ctrl+Shift+K\"; empty "
		"unbinds it) - a menu action, or one of a panel scope's actions ('scope' arranger, note_editor, "
		"automation_editor or perform). Refused, typed, when another action of the SAME scope already has "
		"those keys - unless 'force', which moves them here and unbinds the other; a binding in another scope "
		"is never touched. Kept in the config file (ui/shortcuts), applied at once, one undo step."),
		objectSchema({{QStringLiteral("id"), stringProperty()}, {QStringLiteral("scope"), bindingScope},
			{QStringLiteral("keys"), keys}, {QStringLiteral("force"), booleanProperty()}},
			{QStringLiteral("id"), QStringLiteral("keys")}),
		objectSchema({{QStringLiteral("id"), stringProperty()}, {QStringLiteral("scope"), stringProperty()},
			{QStringLiteral("keys"), stringProperty()}, {QStringLiteral("previous"), stringProperty()},
			{QStringLiteral("cleared"), arrayProperty()}}), true, shortcutcommands::set);
	add(registry, "reset", QStringLiteral("Give one action ('id') - or, without it, every action of one "
		"context ('scope': that panel's bindings; the menu map when 'scope' is empty) - its own default "
		"shortcut back; with no 'id' and no 'scope' argument at all, every menu AND panel binding. One undo step."),
		objectSchema({{QStringLiteral("id"), stringProperty()}, {QStringLiteral("scope"), stringProperty()}}),
		objectSchema({{QStringLiteral("reset"), integerProperty()}}), true, shortcutcommands::reset);
	add(registry, "export", QStringLiteral("Write the shortcut set - the menu overrides, the panel overrides and "
		"every binding - to 'file' as JSON, for another machine or a backup. Writes only that file."),
		objectSchema({{QStringLiteral("file"), stringProperty()}}, {QStringLiteral("file")}),
		objectSchema({{QStringLiteral("file"), stringProperty()}, {QStringLiteral("overrides"), integerProperty()},
			{QStringLiteral("bindings"), integerProperty()}, {QStringLiteral("panels"), integerProperty()}}), false, shortcutcommands::exportSet);
	add(registry, "import", QStringLiteral("Apply a shortcut set: the overrides in 'file' (written by shortcuts.export) or "
		"given as 'overrides', plus a stage-1 'panels' object; merged over the current ones, or replacing them "
		"with 'replace'. Ids this build's actions do not have are kept and listed in 'unknown'; a value that is "
		"no key sequence is left out and listed in 'invalid'; 'conflicts' lists the keys more than one action "
		"of one scope holds afterwards ({keys, ids, scope}). One undo step."),
		objectSchema({{QStringLiteral("file"), stringProperty()}, {QStringLiteral("overrides"), objectProperty()},
			{QStringLiteral("panels"), objectProperty()}, {QStringLiteral("replace"), booleanProperty()}}),
		objectSchema({{QStringLiteral("imported"), integerProperty()}, {QStringLiteral("unknown"), arrayProperty()},
			{QStringLiteral("invalid"), arrayProperty()}, {QStringLiteral("conflicts"), arrayProperty()},
			{QStringLiteral("panels"), integerProperty()}}), true,
		shortcutcommands::importSet);
	add(registry, "preset_list", QStringLiteral("The shipped keyboard maps: the accessibility one and the Ableton Live, "
		"FL Studio and Logic Pro migration maps, each by id, name, description and mapping count. They map Zene's "
		"corresponding menu actions only, never another DAW's project or settings."),
		objectSchema(), objectSchema({{QStringLiteral("presets"), arrayProperty()}, {QStringLiteral("count"), integerProperty()}}),
		false, shortcutcommands::presetList);
	add(registry, "preset_apply", QStringLiteral("Apply a shipped keyboard map ('preset': accessibility, ableton-live, "
		"fl-studio or logic-pro) to the matching Zene menu actions. Before the map is replaced, the current set is written "
		"to a timestamped shortcuts-backup JSON file in the workspace and its path is reported; shortcuts.import can restore "
		"it. One undo step also restores the previous overrides. The backup file remains after that undo, deliberately."),
		objectSchema({{QStringLiteral("preset"), stringProperty()}}, {QStringLiteral("preset")}),
		objectSchema({{QStringLiteral("preset"), stringProperty()}, {QStringLiteral("name"), stringProperty()},
			{QStringLiteral("backup"), stringProperty()}, {QStringLiteral("imported"), integerProperty()},
			{QStringLiteral("mapped"), arrayProperty()}, {QStringLiteral("unknown"), arrayProperty()},
			{QStringLiteral("invalid"), arrayProperty()}, {QStringLiteral("conflicts"), arrayProperty()}}), true,
		shortcutcommands::presetApply);
}

} // namespace lmms
