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

/*! WF-110/111 (0.5.0 L13): every menu action's key is the user's to choose, and - stage 1 of the
 *  context-aware map - so is every action of the arranger, the note editor, the automation editor
 *  and Perform. The menu bindings are read from the live menus (ShortcutMap); a panel action is
 *  declared with its scope (PanelShortcuts). The choices are overrides in ui/shortcuts written as
 *  one undo step (UiConfigStore): `overrides` holds the menu map, `panels` the scoped one. A clash
 *  is two actions of the SAME scope on one key; the same key in two scopes is the point. */

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMenuBar>
#include <QSaveFile>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ConfigManager.h"
#include "ControlCommandsShortcutsShared.h"
#include "ControlWindowCommands.h"
#include "MainWindow.h"
#include "PanelShortcuts.h"
#include "ShortcutMap.h"
#include "ShortcutPresets.h"
#include "UiConfigStore.h"

namespace lmms
{

using namespace control;
namespace sm = gui::ShortcutMap;
namespace ps = gui::panelshortcuts;

namespace
{

constexpr int ExportVersion = 1;
const QString ExportFormat = QStringLiteral("zene-shortcuts");
const QString PanelsField = QStringLiteral("panels");
const QString OverridesField = QStringLiteral("overrides");

QMenuBar* bar()
{
	gui::MainWindow* window = controlMainWindow();
	return window != nullptr ? window->menuBar() : nullptr;
}

//! The actions of @a scope on @a keys, other than @a except: what the user has to resolve. Only the
//! SAME scope counts - the same key in another panel is the point of a scoped map, not a clash.
QStringList clashOn(const QList<sm::Binding>& all, const QString& keys, const QString& scope, const QString& except)
{
	QStringList clash;
	if (keys.isEmpty()) { return clash; }
	for (const sm::Binding& other : all)
	{
		if (other.id != except && other.scope == scope && other.keys == keys) { clash.append(other.id); }
	}
	return clash;
}

//! The actions of @a scope on @a binding's keys, other than itself.
QStringList sameScopeClash(const sm::Binding& binding, const QList<sm::Binding>& all)
{
	return clashOn(all, binding.keys, binding.scope, binding.id);
}

QJsonObject bindingJson(const sm::Binding& binding, const QList<sm::Binding>& all)
{
	return {{QStringLiteral("id"), binding.id}, {QStringLiteral("scope"), binding.scope},
		{QStringLiteral("command"), binding.command}, {QStringLiteral("keys"), binding.keys},
		{QStringLiteral("default"), binding.defaults}, {QStringLiteral("overridden"), binding.overridden},
		{QStringLiteral("conflict"), sameScopeClash(binding, all).join(QStringLiteral(", "))}};
}

//! The stored panel overrides (ui/shortcuts' `panels` object, scope -> id -> keys).
QJsonObject panelOverrides()
{
	return gui::uistore::read(QLatin1String(sm::StoreKey)).value(PanelsField).toObject();
}

//! The arguments that put the whole shortcut map - menu overrides AND panel overrides - back.
QJsonObject inverseArgs()
{
	return {{OverridesField, sm::overrides()}, {PanelsField, panelOverrides()}, {QStringLiteral("replace"), true}};
}

//! How many panel overrides there are across every scope (the store nests them by scope).
int panelCount(const QJsonObject& panels)
{
	int count = 0;
	for (auto scope = panels.begin(); scope != panels.end(); ++scope) { count += scope.value().toObject().size(); }
	return count;
}

//! Write @a overrides and @a panels as the whole store, re-applying the menu keys after the write
//! and its undo (the panel map is read live by the router, so it needs no re-apply).
ControlResult storeShortcuts(QMenuBar* menus, const QJsonObject& overrides, const QJsonObject& panels,
	QJsonObject result, const gui::uistore::Inverse& inverse)
{
	QJsonObject store = gui::uistore::read(QLatin1String(sm::StoreKey));
	store.insert(OverridesField, overrides);
	store.insert(PanelsField, panels);
	QPointer<QMenuBar> guarded(menus);
	return gui::uistore::write(QLatin1String(sm::StoreKey), store, std::move(result), inverse,
		[guarded] { if (!guarded.isNull()) { sm::apply(guarded); } });
}

//! The binding @a id has in @a scope, or nullptr.
const sm::Binding* findBinding(const QList<sm::Binding>& all, const QString& id, const QString& scope)
{
	for (const sm::Binding& binding : all)
	{
		if (binding.id == id && binding.scope == scope) { return &binding; }
	}
	return nullptr;
}

//! Every binding - menu and panel - in one list, for a scoped clash check and for the list command.
QList<sm::Binding> allBindings(QMenuBar* menus)
{
	QList<sm::Binding> all = sm::bindings(menus);
	for (const sm::Binding& binding : ps::bindings()) { all.append(binding); }
	return all;
}

ControlResult listShortcuts(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.list")); }
	const QString query = args.value(QStringLiteral("query")).toString();
	const QString wantedScope = args.value(QStringLiteral("scope")).toString();
	const QList<sm::Binding> all = allBindings(menus);
	QJsonArray out;
	int overridden = 0;
	int conflicts = 0;
	for (const sm::Binding& binding : all)
	{
		if (!wantedScope.isEmpty() && binding.scope != wantedScope) { continue; }
		const QString haystack = binding.id + QLatin1Char(' ') + binding.keys + QLatin1Char(' ') + binding.command
			+ QLatin1Char(' ') + binding.scope;
		if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) { continue; }
		overridden += binding.overridden ? 1 : 0;
		const QJsonObject row = bindingJson(binding, all);
		if (!row.value(QStringLiteral("conflict")).toString().isEmpty()) { ++conflicts; }
		out.append(row);
	}
	return ControlResult::success({{QStringLiteral("bindings"), out}, {QStringLiteral("count"), out.size()},
		{QStringLiteral("overridden_count"), overridden}, {QStringLiteral("conflict_count"), conflicts},
		{QStringLiteral("scopes"), QJsonArray::fromStringList(ps::scopes())}});
}

//! A scope argument's refusal: an unknown scope is a typed InvalidArgs, never a quiet "the menu map".
ControlResult checkScope(const QString& scope)
{
	if (scope.isEmpty() || ps::knownScope(scope)) { return ControlResult::success(); }
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' is not a panel scope (arranger, note_editor, automation_editor, perform)").arg(scope));
}

//! The override object a write goes to, and the one the inverse restores.
struct Stores
{
	QJsonObject menu;
	QJsonObject panels;
	QJsonObject previousMenu;
	QJsonObject previousPanels;
};

//! Put @a keys into the map @a scope writes to; Undefined removes the override entirely.
void putStored(QJsonObject* menu, QJsonObject* panels, const QString& scope, const QString& id, const QJsonValue& keys)
{
	if (scope.isEmpty())
	{
		if (keys.isUndefined()) { menu->remove(id); } else { menu->insert(id, keys); }
		return;
	}
	*panels = ps::withStored(*panels, scope, id, keys);
}

//! A set, resolved before anything is written: the action, its canonical key text, and the
//! same-scope clash to clear - or the typed refusal that stops the write.
struct Resolved
{
	const sm::Binding* binding = nullptr;
	QString keys;
	QStringList clash;
	ControlResult error = ControlResult::success();
};

Resolved resolveSet(const QList<sm::Binding>& all, const QString& scope, const QString& id, const QJsonObject& args)
{
	Resolved out;
	bool ok = false;
	out.keys = sm::portable(args.value(QStringLiteral("keys")).toString(), &ok);
	if (!ok)
	{
		out.error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a key sequence").arg(args.value(QStringLiteral("keys")).toString()));
		return out;
	}
	out.binding = findBinding(all, id, scope);
	if (out.binding == nullptr)
	{
		out.error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no %1 action '%2' (shortcuts.list names them)")
				.arg(scope.isEmpty() ? QStringLiteral("menu") : scope, id));
		return out;
	}
	out.clash = clashOn(all, out.keys, scope, id);
	if (!out.clash.isEmpty() && !args.value(QStringLiteral("force")).toBool())
	{
		out.error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is already %2's key in %3; pass force: true to move it here")
				.arg(out.keys, out.clash.join(QStringLiteral(", ")),
					scope.isEmpty() ? QStringLiteral("the menu map") : scope));
	}
	return out;
}

ControlResult setShortcut(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.set")); }
	const QString scope = args.value(QStringLiteral("scope")).toString();
	if (const ControlResult bad = checkScope(scope); !bad.ok) { return bad; }
	const QString id = args.value(QStringLiteral("id")).toString();
	const QList<sm::Binding> all = allBindings(menus);
	const Resolved resolved = resolveSet(all, scope, id, args);
	if (!resolved.error.ok) { return resolved.error; }
	Stores stores{sm::overrides(), panelOverrides(), {}, {}};
	stores.previousMenu = stores.menu;
	stores.previousPanels = stores.panels;
	// The new value: the action's own default removes the override (nothing to remember), an empty
	// key text is the deliberate unbind, anything else is the chosen key.
	const QJsonValue value = resolved.keys == resolved.binding->defaults
		? QJsonValue(QJsonValue::Undefined) : QJsonValue(resolved.keys);
	// Only actions of the SAME context are cleared: the same key in another panel stays put.
	const QJsonValue cleared = value.isUndefined() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(QString());
	putStored(&stores.menu, &stores.panels, scope, id, value);
	for (const QString& other : resolved.clash) { putStored(&stores.menu, &stores.panels, scope, other, cleared); }
	QJsonObject result{{QStringLiteral("id"), id}, {QStringLiteral("scope"), scope}, {QStringLiteral("keys"), resolved.keys},
		{QStringLiteral("previous"), resolved.binding->keys},
		{QStringLiteral("cleared"), QJsonArray::fromStringList(resolved.clash)}};
	// A forced move also cleared other actions' keys; only the whole previous set names all of it.
	const gui::uistore::Inverse inverse = resolved.clash.isEmpty()
		? gui::uistore::Inverse{QStringLiteral("shortcuts.set"), {{QStringLiteral("id"), id}, {QStringLiteral("scope"), scope},
			{QStringLiteral("keys"), resolved.binding->keys}, {QStringLiteral("force"), true}}}
		: gui::uistore::Inverse{QStringLiteral("shortcuts.import"),
			{{OverridesField, stores.previousMenu}, {PanelsField, stores.previousPanels}, {QStringLiteral("replace"), true}}};
	return storeShortcuts(menus, stores.menu, stores.panels, result, inverse);
}

ControlResult resetShortcuts(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.reset")); }
	const QString scope = args.value(QStringLiteral("scope")).toString();
	if (const ControlResult bad = checkScope(scope); !bad.ok) { return bad; }
	QJsonObject overrides = sm::overrides();
	QJsonObject panels = panelOverrides();
	const QString id = args.value(QStringLiteral("id")).toString();
	// An empty id resets every action OF THAT CONTEXT: the named scope's panel bindings, the menu
	// map when no scope is asked for, and - when 'scope' is absent altogether - every panel too
	// (the shortcuts page's Reset All).
	const bool everyScope = !args.contains(QStringLiteral("scope"));
	if (!id.isEmpty())
	{
		const QList<sm::Binding> all = allBindings(menus);
		if (findBinding(all, id, scope) == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no %1 action '%2' (shortcuts.list names them)")
				.arg(scope.isEmpty() ? QStringLiteral("menu") : scope, id));
		}
	}
	const int before = overrides.size() + panelCount(panels);
	if (!scope.isEmpty())
	{
		if (id.isEmpty()) { panels.remove(scope); }
		else { panels = ps::withStored(panels, scope, id, QJsonValue(QJsonValue::Undefined)); }
	}
	else
	{
		if (id.isEmpty())
		{
			overrides = QJsonObject();
			if (everyScope) { panels = QJsonObject(); }
		}
		else { overrides.remove(id); }
	}
	const int after = overrides.size() + panelCount(panels);
	return storeShortcuts(menus, overrides, panels, {{QStringLiteral("reset"), before - after}},
		{QStringLiteral("shortcuts.import"), inverseArgs()});
}

//! The whole shortcut set as shortcuts.export writes it (Q050-202 uses the same document for a backup).
//! Q050-055.1: it carries the panel overrides too, so a backup restores every context.
QJsonObject shortcutSetJson(QMenuBar* menus)
{
	const QList<sm::Binding> all = allBindings(menus);
	QJsonArray rows;
	for (const sm::Binding& binding : all) { rows.append(bindingJson(binding, all)); }
	return {{QStringLiteral("format"), ExportFormat}, {QStringLiteral("version"), ExportVersion},
		{OverridesField, sm::overrides()}, {PanelsField, panelOverrides()}, {QStringLiteral("bindings"), rows}};
}

bool writeShortcutSet(const QString& path, const QJsonObject& document, ControlResult* error)
{
	const QByteArray bytes = QJsonDocument(document).toJson();
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot write '%1': %2").arg(path, file.errorString()));
		return false;
	}
	return true;
}

ControlResult exportShortcuts(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.export")); }
	const QString path = args.value(QStringLiteral("file")).toString();
	const QJsonObject doc = shortcutSetJson(menus);
	// Written whole or not at all: a short write must not leave a truncated set reported as exported.
	ControlResult error;
	if (!writeShortcutSet(path, doc, &error)) { return error; }
	return ControlResult::success({{QStringLiteral("file"), path},
		{QStringLiteral("overrides"), sm::overrides().size() + panelCount(panelOverrides())},
		{QStringLiteral("bindings"), doc.value(QStringLiteral("bindings")).toArray().size()},
		{QStringLiteral("panels"), panelCount(panelOverrides())}});
}

//! @a path is not a shortcut set: one message, so every refusal reads the same.
ControlResult notASet(const QString& path)
{
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' is not a shortcut set (shortcuts.export writes one)").arg(path));
}

//! The version @a doc declares, or -1 when it declares none this build can read.
int declaredVersion(const QJsonObject& doc)
{
	const QJsonValue declared = doc.value(QStringLiteral("version"));
	const int version = declared.toInt(-1);
	return declared.isDouble() && version >= 1 && declared.toDouble() == version ? version : -1;
}

//! The panel map a shortcut set carries, when it carries one.
void takePanels(const QJsonObject& doc, QJsonObject* panels, bool* hasPanels)
{
	if (const QJsonValue given = doc.value(PanelsField); given.isObject())
	{
		*panels = given.toObject();
		*hasPanels = true;
	}
}

//! The overrides an import brings: the arguments, or the file's. A file written by this build also
//! carries the stage-1 panel map (`panels`), merged or replaced the same way.
bool importedOverrides(const QJsonObject& args, QJsonObject* out, QJsonObject* panels, bool* hasPanels, ControlResult* error)
{
	*hasPanels = false;
	if (args.contains(QStringLiteral("file")) && args.contains(OverridesField))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("give 'file' or 'overrides', not both"));
		return false;
	}
	if (args.contains(OverridesField))
	{
		*out = args.value(OverridesField).toObject();
		takePanels(args, panels, hasPanels);
		return true;
	}
	const QString path = args.value(QStringLiteral("file")).toString();
	QFile file(path);
	const QJsonObject doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
	if (doc.value(QStringLiteral("format")).toString() != ExportFormat) { *error = notASet(path); return false; }
	const int version = declaredVersion(doc);
	if (version < 0) { *error = notASet(path); return false; }
	if (version > ExportVersion)  // a newer build's set may mean other things
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is a version %2 shortcut set; this build reads "
			"version %3").arg(path).arg(version).arg(ExportVersion));
		return false;
	}
	const QJsonValue overrides = doc.value(OverridesField);
	if (!overrides.isObject()) { *error = notASet(path); return false; }
	*out = overrides.toObject();
	takePanels(doc, panels, hasPanels);
	return true;
}

//! The keys more than one action of one scope holds once the maps apply: [{scope, keys, ids}]. Qt
//! fires neither action of an ambiguous shortcut, so a set that makes one says so - and the same key
//! in two scopes is NOT listed.
QJsonArray clashesUnder(const QList<sm::Binding>& all, const QJsonObject& overrides, const QJsonObject& panels)
{
	QMap<QString, QStringList> holders;
	for (const sm::Binding& binding : all)
	{
		QString keys = binding.defaults;
		if (binding.scope.isEmpty()) { if (overrides.contains(binding.id)) { keys = overrides.value(binding.id).toString(); } }
		else if (const QJsonValue chosen = ps::stored(panels, binding.scope, binding.id); chosen.isString())
		{
			keys = chosen.toString();
		}
		if (!keys.isEmpty()) { holders[binding.scope + QChar(0x1f) + keys].append(binding.id); }
	}
	QJsonArray out;
	for (auto it = holders.cbegin(); it != holders.cend(); ++it)
	{
		if (it.value().size() < 2) { continue; }
		const int separator = it.key().indexOf(QChar(0x1f));
		out.append(QJsonObject{{QStringLiteral("scope"), it.key().left(separator)},
			{QStringLiteral("keys"), it.key().mid(separator + 1)}, {QStringLiteral("ids"), QJsonArray::fromStringList(it.value())}});
	}
	return out;
}

//! The safety copy Q050-202 makes before a preset replaces the map. A real file, not only the undo
//! step: it survives closing the app and can be imported with shortcuts.import.
QString backupShortcuts(QMenuBar* menus, ControlResult* error)
{
	const QString directory = ConfigManager::inst()->workingDir();
	const QString base = directory + QStringLiteral("shortcuts-backup-")
		+ QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
	QString path = base + QStringLiteral(".json");
	for (int suffix = 2; QFile::exists(path); ++suffix) { path = base + QStringLiteral("-%1.json").arg(suffix); }
	return writeShortcutSet(path, shortcutSetJson(menus), error) ? path : QString();
}

ControlResult listPresets()
{
	QJsonArray presets;
	for (const gui::ShortcutPreset& preset : gui::shortcutPresets())
	{
		presets.append(QJsonObject{{QStringLiteral("preset"), preset.id}, {QStringLiteral("name"), preset.name},
			{QStringLiteral("description"), preset.description}, {QStringLiteral("mappings"), preset.keysByCommand.size()}});
	}
	return ControlResult::success({{QStringLiteral("presets"), presets}, {QStringLiteral("count"), presets.size()}});
}

//! Q050-202: replace the overrides with a shipped preset, after a safety copy of the map it replaces.
ControlResult applyPreset(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.preset_apply")); }
	const gui::ShortcutPreset* preset = gui::shortcutPreset(args.value(QStringLiteral("preset")).toString());
	if (preset == nullptr)
	{
		QStringList ids;
		for (const gui::ShortcutPreset& p : gui::shortcutPresets()) { ids << p.id; }
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no shortcut preset '%1' (shortcuts.preset_list names %2)")
				.arg(args.value(QStringLiteral("preset")).toString(), ids.join(QStringLiteral(", "))));
	}
	const QList<sm::Binding> all = sm::bindings(menus);
	QJsonObject before = sm::overrides();
	QJsonObject wanted;
	QStringList unknown;
	QStringList invalid;
	QStringList mapped;
	for (auto it = preset->keysByCommand.begin(); it != preset->keysByCommand.end(); ++it)
	{
		bool ok = false;
		const QString key = sm::portable(it.value().toString(), &ok);
		if (!ok) { invalid << it.key(); continue; }
		const auto found = std::find_if(all.begin(), all.end(), [&it](const sm::Binding& b) { return b.command == it.key(); });
		if (found == all.end()) { unknown << it.key(); continue; }
		wanted.insert(found->id, key);
		mapped << it.key();
	}
	ControlResult error;
	const QString backup = backupShortcuts(menus, &error);
	if (backup.isEmpty()) { return error; }
	// A preset maps menu actions only: the panel overrides (every scoped context) are kept as they are.
	const QJsonObject panels = panelOverrides();
	const ControlResult result = storeShortcuts(menus, wanted, panels,
		{{QStringLiteral("preset"), preset->id}, {QStringLiteral("name"), preset->name}, {QStringLiteral("backup"), backup},
			{QStringLiteral("imported"), wanted.size()}, {QStringLiteral("mapped"), QJsonArray::fromStringList(mapped)},
			{QStringLiteral("unknown"), QJsonArray::fromStringList(unknown)}, {QStringLiteral("invalid"), QJsonArray::fromStringList(invalid)},
			{QStringLiteral("conflicts"), clashesUnder(allBindings(menus), wanted, panels)}},
		{QStringLiteral("shortcuts.import"), {{OverridesField, before}, {PanelsField, panels}, {QStringLiteral("replace"), true}}});
	return result;
}

//! A value that is a string key sequence, or nothing (invalid, and left out); @a ok says which.
QString importedKey(const QJsonValue& value, bool* ok)
{
	bool parsed = false;
	const QString keys = value.isString() ? sm::portable(value.toString(), &parsed) : QString();
	*ok = parsed;
	return keys;
}

//! What one import applied, and what it left out.
struct Imported
{
	QJsonArray unknown;
	QJsonArray invalid;  // not a string key sequence: left out (read as empty it would unbind the action)
	int count = 0;
	int panelEntries = 0;
};

//! The menu half of an import: @a incoming into @a overrides. @a known holds "scope<US>id" for
//! every action this build has, so an id the build does not know is REPORTED but still kept.
void importMenuMap(const QJsonObject& incoming, const QStringList& known, QJsonObject* overrides, Imported* out)
{
	for (auto it = incoming.begin(); it != incoming.end(); ++it)
	{
		bool ok = false;
		const QString keys = importedKey(it.value(), &ok);
		if (!ok) { out->invalid.append(it.key()); continue; }
		overrides->insert(it.key(), keys);
		// A menu id is stored bare, so the known list is asked with an empty scope.
		if (!known.contains(QChar(0x1f) + it.key())) { out->unknown.append(it.key()); }
		++out->count;
	}
}

//! The panel half: @a incomingPanels is nested scope -> id -> keys.
void importPanelMap(const QJsonObject& incomingPanels, const QStringList& known, QJsonObject* panels, Imported* out)
{
	for (auto scope = incomingPanels.begin(); scope != incomingPanels.end(); ++scope)
	{
		const QJsonObject within = scope.value().toObject();
		for (auto it = within.begin(); it != within.end(); ++it)
		{
			bool ok = false;
			const QString keys = importedKey(it.value(), &ok);
			if (!ok) { out->invalid.append(scope.key() + QStringLiteral(" ▸ ") + it.key()); continue; }
			*panels = ps::withStored(*panels, scope.key(), it.key(), keys);
			if (!known.contains(scope.key() + QChar(0x1f) + it.key()))
			{
				out->unknown.append(scope.key() + QStringLiteral(" ▸ ") + it.key());
			}
			++out->count;
			++out->panelEntries;
		}
	}
}

ControlResult importShortcuts(const QJsonObject& args)
{
	QMenuBar* menus = bar();
	if (menus == nullptr) { return noInterface(QStringLiteral("shortcuts.import")); }
	QJsonObject incoming;
	QJsonObject incomingPanels;
	bool hasPanels = false;
	ControlResult error;
	if (!importedOverrides(args, &incoming, &incomingPanels, &hasPanels, &error)) { return error; }
	const bool replace = args.value(QStringLiteral("replace")).toBool();
	QJsonObject overrides = replace ? QJsonObject() : sm::overrides();
	QJsonObject panels = replace && hasPanels ? QJsonObject() : panelOverrides();
	const QList<sm::Binding> all = allBindings(menus);
	QStringList known;  // "scope<US>id" for every action this build has, panel and menu alike
	for (const sm::Binding& binding : all) { known.append(binding.scope + QChar(0x1f) + binding.id); }
	Imported applied;
	importMenuMap(incoming, known, &overrides, &applied);
	importPanelMap(incomingPanels, known, &panels, &applied);
	return storeShortcuts(menus, overrides, panels,
		{{QStringLiteral("imported"), applied.count}, {QStringLiteral("unknown"), applied.unknown},
			{QStringLiteral("invalid"), applied.invalid},
			{QStringLiteral("conflicts"), clashesUnder(all, overrides, panels)},
			{QStringLiteral("panels"), applied.panelEntries}},
		{QStringLiteral("shortcuts.import"), inverseArgs()});
}

} // namespace

// The handlers the registration table (ControlCommandsShortcutsRegister.cpp) names.
namespace shortcutcommands
{
ControlResult list(const QJsonObject& args) { return listShortcuts(args); }
ControlResult set(const QJsonObject& args) { return setShortcut(args); }
ControlResult reset(const QJsonObject& args) { return resetShortcuts(args); }
ControlResult exportSet(const QJsonObject& args) { return exportShortcuts(args); }
ControlResult importSet(const QJsonObject& args) { return importShortcuts(args); }
ControlResult presetList(const QJsonObject&) { return listPresets(); }
ControlResult presetApply(const QJsonObject& args) { return applyPreset(args); }
} // namespace shortcutcommands

} // namespace lmms
