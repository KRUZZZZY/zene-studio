/*
 * ControlCommandsShortcuts.cpp - shortcuts.*: rebind, reset, export and import keyboard shortcuts
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

/*! WF-110/111 (0.5.0 L13): every menu action's key is the user's to choose. The bindings are
 *  read from the live menus (ShortcutMap), the choices are overrides in ui/shortcuts written as
 *  one undo step (UiConfigStore), and a set is a file that can be exported and imported. */

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMenuBar>
#include <QSaveFile>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "MainWindow.h"
#include "ShortcutMap.h"
#include "UiConfigStore.h"

namespace lmms
{

using namespace control;
namespace sm = gui::ShortcutMap;

namespace
{

constexpr int ExportVersion = 1;
const QString ExportFormat = QStringLiteral("zene-shortcuts");

QMenuBar* bar()
{
	gui::MainWindow* window = controlMainWindow();
	return window != nullptr ? window->menuBar() : nullptr;
}

QJsonObject bindingJson(const sm::Binding& binding)
{
	return {{QStringLiteral("id"), binding.id}, {QStringLiteral("command"), binding.command},
		{QStringLiteral("keys"), binding.keys}, {QStringLiteral("default"), binding.defaults},
		{QStringLiteral("overridden"), binding.overridden}};
}

//! Write @a overrides as the whole store, re-applying the keys after the write and its undo.
ControlResult storeOverrides(QMenuBar* menus, const QJsonObject& overrides, QJsonObject result, const gui::uistore::Inverse& inverse)
{
	QJsonObject store = gui::uistore::read(QLatin1String(sm::StoreKey));
	store.insert(QStringLiteral("overrides"), overrides);
	QPointer<QMenuBar> guarded(menus);
	return gui::uistore::write(QLatin1String(sm::StoreKey), store, std::move(result), inverse,
		[guarded] { if (!guarded.isNull()) { sm::apply(guarded); } });
}

ControlResult listShortcuts(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.list")); }
	const QString query = args.value(QStringLiteral("query")).toString();
	QJsonArray out;
	int overridden = 0;
	for (const sm::Binding& binding : sm::bindings(menus))
	{
		const QString haystack = binding.id + QLatin1Char(' ') + binding.keys + QLatin1Char(' ') + binding.command;
		if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) { continue; }
		overridden += binding.overridden ? 1 : 0;
		out.append(bindingJson(binding));
	}
	return ControlResult::success({{QStringLiteral("bindings"), out}, {QStringLiteral("count"), out.size()},
		{QStringLiteral("overridden_count"), overridden}});
}

ControlResult setShortcut(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.set")); }
	const QString id = args.value(QStringLiteral("id")).toString();
	bool ok = false;
	const QString keys = sm::portable(args.value(QStringLiteral("keys")).toString(), &ok);
	if (!ok) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is not a key sequence").arg(args.value(QStringLiteral("keys")).toString())); }
	const QList<sm::Binding> all = sm::bindings(menus);
	const auto found = std::find_if(all.begin(), all.end(), [&id](const sm::Binding& b) { return b.id == id; });
	if (found == all.end()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no menu action '%1' (shortcuts.list names them)").arg(id)); }
	const QStringList clash = sm::conflicts(all, keys, id);
	if (!clash.isEmpty() && !args.value(QStringLiteral("force")).toBool())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is already %2's key; pass force: true to move it here").arg(keys, clash.join(QStringLiteral(", "))));
	}
	QJsonObject overrides = sm::overrides();
	if (keys == found->defaults) { overrides.remove(id); } else { overrides.insert(id, keys); }
	for (const QString& other : clash) { overrides.insert(other, QString()); }
	QJsonObject result{{QStringLiteral("id"), id}, {QStringLiteral("keys"), keys}, {QStringLiteral("previous"), found->keys},
		{QStringLiteral("cleared"), QJsonArray::fromStringList(clash)}};
	// A forced move also cleared other actions' keys; only the whole previous set names all of it.
	const gui::uistore::Inverse inverse = clash.isEmpty()
		? gui::uistore::Inverse{QStringLiteral("shortcuts.set"), {{QStringLiteral("id"), id}, {QStringLiteral("keys"), found->keys}, {QStringLiteral("force"), true}}}
		: gui::uistore::Inverse{QStringLiteral("shortcuts.import"), {{QStringLiteral("overrides"), sm::overrides()}, {QStringLiteral("replace"), true}}};
	return storeOverrides(menus, overrides, result, inverse);
}

ControlResult resetShortcuts(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.reset")); }
	QJsonObject overrides = sm::overrides();
	const QString id = args.value(QStringLiteral("id")).toString();
	// An empty id resets every action; a named one has to exist, or the caller hears "reset" for nothing.
	if (!id.isEmpty())
	{
		const QList<sm::Binding> all = sm::bindings(menus);
		const bool known = std::any_of(all.cbegin(), all.cend(), [&id](const sm::Binding& b) { return b.id == id; });
		if (!known)
		{
			return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no menu action '%1' (shortcuts.list names them)").arg(id));
		}
	}
	const int before = overrides.size();
	if (id.isEmpty()) { overrides = QJsonObject(); } else { overrides.remove(id); }
	return storeOverrides(menus, overrides, {{QStringLiteral("reset"), before - overrides.size()}},
		{QStringLiteral("shortcuts.import"), {{QStringLiteral("overrides"), sm::overrides()}, {QStringLiteral("replace"), true}}});
}

ControlResult exportShortcuts(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.export")); }
	const QString path = args.value(QStringLiteral("file")).toString();
	QJsonArray all;
	for (const sm::Binding& binding : sm::bindings(menus)) { all.append(bindingJson(binding)); }
	const QJsonObject doc{{QStringLiteral("format"), ExportFormat}, {QStringLiteral("version"), ExportVersion},
		{QStringLiteral("overrides"), sm::overrides()}, {QStringLiteral("bindings"), all}};
	// Written whole or not at all: a short write must not leave a truncated set reported as exported.
	const QByteArray bytes = QJsonDocument(doc).toJson();
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot write '%1': %2").arg(path, file.errorString()));
	}
	return ControlResult::success({{QStringLiteral("file"), path}, {QStringLiteral("overrides"), sm::overrides().size()},
		{QStringLiteral("bindings"), all.size()}});
}

//! The overrides an import brings: the `overrides` argument, or the file's.
bool importedOverrides(const QJsonObject& args, QJsonObject* out, ControlResult* error)
{
	if (args.contains(QStringLiteral("overrides"))) { *out = args.value(QStringLiteral("overrides")).toObject(); return true; }
	const QString path = args.value(QStringLiteral("file")).toString();
	QFile file(path);
	const QJsonObject doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
	if (doc.value(QStringLiteral("format")).toString() != ExportFormat)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is not a shortcut set (shortcuts.export writes one)").arg(path));
		return false;
	}
	if (doc.value(QStringLiteral("version")).toInt(ExportVersion) > ExportVersion)  // a newer build's set may mean other things
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is a version %2 shortcut set; this build reads "
			"version %3").arg(path).arg(doc.value(QStringLiteral("version")).toInt()).arg(ExportVersion));
		return false;
	}
	*out = doc.value(QStringLiteral("overrides")).toObject();
	return true;
}

//! The keys more than one action holds once @a overrides apply over @a all's defaults: [{keys, ids}]. Qt fires
//! neither action of an ambiguous shortcut, so an import that makes one says so.
QJsonArray clashesUnder(const QList<sm::Binding>& all, const QJsonObject& overrides)
{
	QMap<QString, QStringList> holders;
	for (const sm::Binding& binding : all)
	{
		const QString keys = overrides.contains(binding.id) ? overrides.value(binding.id).toString() : binding.defaults;
		if (!keys.isEmpty()) { holders[keys].append(binding.id); }
	}
	QJsonArray out;
	for (auto it = holders.cbegin(); it != holders.cend(); ++it)
	{
		if (it.value().size() > 1) { out.append(QJsonObject{{QStringLiteral("keys"), it.key()}, {QStringLiteral("ids"), QJsonArray::fromStringList(it.value())}}); }
	}
	return out;
}

ControlResult importShortcuts(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.import")); }
	QJsonObject incoming;
	ControlResult error;
	if (!importedOverrides(args, &incoming, &error)) { return error; }
	QJsonObject overrides = args.value(QStringLiteral("replace")).toBool() ? QJsonObject() : sm::overrides();
	const QList<sm::Binding> all = sm::bindings(menus);
	QStringList known;
	for (const sm::Binding& binding : all) { known.append(binding.id); }
	QJsonArray unknown;
	QJsonArray invalid;  // not a string key sequence: left out (read as empty it would unbind the action)
	for (auto it = incoming.begin(); it != incoming.end(); ++it)
	{
		if (!it.value().isString()) { invalid.append(it.key()); continue; }
		bool ok = false;
		const QString keys = sm::portable(it.value().toString(), &ok);
		if (!ok) { invalid.append(it.key()); continue; }
		overrides.insert(it.key(), keys);
		if (!known.contains(it.key())) { unknown.append(it.key()); }
	}
	return storeOverrides(menus, overrides, {{QStringLiteral("imported"), incoming.size() - invalid.size()}, {QStringLiteral("unknown"), unknown},
		{QStringLiteral("invalid"), invalid}, {QStringLiteral("conflicts"), clashesUnder(all, overrides)}},
		{QStringLiteral("shortcuts.import"), {{QStringLiteral("overrides"), sm::overrides()}, {QStringLiteral("replace"), true}}});
}

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
	add(registry, "list", QStringLiteral("Every menu action that can carry a keyboard shortcut: its id (\"File ▸ Save\" - "
		"where it lives and what it says), the registry command it declares, its current keys, the code's own "
		"default and whether the user overrode it. 'query' filters by id, keys or command."),
		objectSchema({{QStringLiteral("query"), stringProperty()}}),
		objectSchema({{QStringLiteral("bindings"), arrayProperty()}, {QStringLiteral("count"), integerProperty()},
			{QStringLiteral("overridden_count"), integerProperty()}}), false, listShortcuts);
	add(registry, "set", QStringLiteral("Bind a menu action to 'keys' (portable text such as \"Ctrl+Shift+K\"; empty "
		"unbinds it). Refused, typed, when another action already has those keys - unless 'force', which moves "
		"them here and unbinds the other. Kept in the config file (ui/shortcuts), applied at once, one undo step."),
		objectSchema({{QStringLiteral("id"), stringProperty()}, {QStringLiteral("keys"), keys},
			{QStringLiteral("force"), booleanProperty()}}, {QStringLiteral("id"), QStringLiteral("keys")}),
		objectSchema({{QStringLiteral("id"), stringProperty()}, {QStringLiteral("keys"), stringProperty()},
			{QStringLiteral("previous"), stringProperty()}, {QStringLiteral("cleared"), arrayProperty()}}), true, setShortcut);
	add(registry, "reset", QStringLiteral("Give one action ('id') - or, without it, every action - its own default "
		"shortcut back. One undo step."), objectSchema({{QStringLiteral("id"), stringProperty()}}),
		objectSchema({{QStringLiteral("reset"), integerProperty()}}), true, resetShortcuts);
	add(registry, "export", QStringLiteral("Write the shortcut set - the overrides and every binding - to 'file' as "
		"JSON, for another machine or a backup. Writes only that file."),
		objectSchema({{QStringLiteral("file"), stringProperty()}}, {QStringLiteral("file")}),
		objectSchema({{QStringLiteral("file"), stringProperty()}, {QStringLiteral("overrides"), integerProperty()},
			{QStringLiteral("bindings"), integerProperty()}}), false, exportShortcuts);
	add(registry, "import", QStringLiteral("Apply a shortcut set: the overrides in 'file' (written by shortcuts.export) or "
		"given as 'overrides'; merged over the current ones, or replacing them with 'replace'. Ids this build's "
		"menus do not have are kept and listed in 'unknown'; a value that is no key sequence is left out and listed in "
		"'invalid'; 'conflicts' lists the keys more than one action holds afterwards ({keys, ids}). One undo step."),
		objectSchema({{QStringLiteral("file"), stringProperty()}, {QStringLiteral("overrides"), objectProperty()},
			{QStringLiteral("replace"), booleanProperty()}}),
		objectSchema({{QStringLiteral("imported"), integerProperty()}, {QStringLiteral("unknown"), arrayProperty()},
			{QStringLiteral("invalid"), arrayProperty()}, {QStringLiteral("conflicts"), arrayProperty()}}), true,
		importShortcuts);
}

} // namespace lmms
