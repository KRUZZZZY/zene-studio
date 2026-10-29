/*
 * ControlCommandsBrowserCollections.cpp - browser.collection_*: saved searches, smart collections
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
/*! 0.5.0 L11 (WF-006): saved searches - smart collections. A collection is a named browser.query (its
 *  text - an expression, BrowserSearch.h - tags, kind, root and category), kept in the user's config
 *  directory beside the browser tags (<config>/browser-collections.json): library state, not project state.
 *  Running one answers what matches NOW, so a collection follows the library as files and tags come
 *  and go. Saving and removing are one undo step each (the previous collection list comes back). */

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include "BrowserCatalog.h"
#include "ConfigManager.h"
#include "ControlBrowserSupport.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control;

namespace
{

//! The store file; ZENE_BROWSER_COLLECTIONS names another (a test points it at a directory of its own).
QString storePath()
{
	const QString overridden = qEnvironmentVariable("ZENE_BROWSER_COLLECTIONS");
	return !overridden.isEmpty() ? overridden : QDir(ConfigManager::inst()->workingDir()).filePath(QStringLiteral("browser-collections.json"));
}

//! The stored collections in @a all; false when the file is there but is not a JSON object (damaged or
//! hand-edited) - a missing or empty file is an empty store.
bool readStore(QJsonObject* all)
{
	*all = {};
	QFile file(storePath());
	if (!file.open(QIODevice::ReadOnly)) { return !file.exists(); }
	const QByteArray bytes = file.readAll();
	if (bytes.trimmed().isEmpty()) { return true; }
	QJsonParseError parsed;
	const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parsed);
	*all = doc.object();
	return parsed.error == QJsonParseError::NoError && doc.isObject();
}

//! The stored collections in @a all; false (with @a error) when the store is damaged. A
//! missing or empty file is an empty store, so this only refuses what cannot be read.
bool readAll(QJsonObject* all, ControlResult* error)
{
	if (readStore(all)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the collections file %1 is not a JSON object, so "
		"nothing was read from it; repair or move it, then try again").arg(storePath()));
	return false;
}

//! readStore for an edit: a damaged store is refused, never overwritten with what one edit leaves of it.
bool readForEdit(QJsonObject* all, ControlResult* error)
{
	if (readStore(all)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the collections file %1 is not a JSON object, so it "
		"was left as it is; repair or move it, then try again").arg(storePath()));
	return false;
}

bool writeAll(const QJsonObject& all)
{
	const QByteArray bytes = QJsonDocument(all).toJson(QJsonDocument::Indented);
	QSaveFile file(storePath());
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	// Written whole or not at all: a write the kernel cuts short is cancelled, never committed as the store.
	if (file.write(bytes) != bytes.size())
	{
		file.cancelWriting();
		return false;
	}
	return file.commit();
}

//! The store snapshot an undo (@a isUndo) or a redo writes. A write that fails is recorded with
//! recordActionRestoreFailure, so control.undo / control.redo refuse instead of answering success over a
//! store that still holds what it held before the step.
void writeStep(const QJsonObject& snapshot, bool isUndo)
{
	if (writeAll(snapshot)) { return; }
	recordActionRestoreFailure(QStringLiteral("the collections file %1 was NOT %2 - the write failed, so the store still "
		"holds what it held when the %3 ran").arg(storePath(), isUndo ? QStringLiteral("restored") : QStringLiteral("re-applied"),
		isUndo ? QStringLiteral("undo") : QStringLiteral("redo")));
}

//! Replace the store with @a next as one undo step.
ControlResult store(const QJsonObject& next, const QJsonObject& answer)
{
	QJsonObject before;
	ControlResult error;
	if (!readForEdit(&before, &error)) { return error; }
	if (!writeAll(next))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the collections file %1 could not be written").arg(storePath()));
	}
	addUndoStep([before]() { writeStep(before, true); }, [next]() { writeStep(next, false); });
	return ControlResult::success(answer);
}

ControlResult save(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a collection needs a 'name'")); }
	QJsonObject query;
	// 0.5.0 L11 (WF-007): the category facet rides along, so a collection saved
	// from a category-filtered search runs the same filter later.
	for (const char* key : {"text", "tags", "kind", "root", "category"})
	{
		if (args.contains(QLatin1String(key))) { query.insert(QLatin1String(key), args.value(QLatin1String(key))); }
	}
	// Checked by the reader browser.query uses, so a collection that can never run is refused now.
	BrowserQuery parsed;
	ControlResult error;
	if (!readBrowserQueryArguments(query, &parsed, &error)) { return error; }
	QJsonObject all;
	if (!readForEdit(&all, &error)) { return error; }
	all.insert(name, query);
	return store(all, {{QStringLiteral("name"), name}, {QStringLiteral("query"), query}, {QStringLiteral("store_path"), storePath()}});
}

ControlResult remove(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	QJsonObject all;
	ControlResult error;
	if (!readForEdit(&all, &error)) { return error; }
	if (!all.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no collection '%1'").arg(name)); }
	all.remove(name);
	return store(all, {{QStringLiteral("removed"), name}, {QStringLiteral("count"), all.size()}});
}

ControlResult list(const QJsonObject&)
{
	QJsonArray out;
	QJsonObject all;
	ControlResult error;
	if (!readAll(&all, &error)) { return error; }
	for (auto it = all.begin(); it != all.end(); ++it)
	{
		out.append(QJsonObject{{QStringLiteral("name"), it.key()}, {QStringLiteral("query"), it.value()}});
	}
	return ControlResult::success({{QStringLiteral("collections"), out}, {QStringLiteral("store_path"), storePath()}});
}

ControlResult run(const QJsonObject& args)
{
	QJsonObject all;
	ControlResult error;
	if (!readAll(&all, &error)) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString();
	if (!all.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no collection '%1'").arg(name)); }
	QJsonObject query = all.value(name).toObject();
	for (const char* key : {"limit", "offset", "probe"})
	{
		if (args.contains(QLatin1String(key))) { query.insert(QLatin1String(key), args.value(QLatin1String(key))); }
	}
	ControlResult result = browserQueryCommand(query);
	if (result.ok) { result.result.insert(QStringLiteral("collection"), name); }
	return result;
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&), bool mutating)
{
	control::defineCommandWithSchema(registry, QStringLiteral("browser.") + QLatin1String(verb), description, args, mutating, handler, false);
}

} // namespace

void registerBrowserCollectionCommands(ControlRegistry& registry)
{
	add(registry, "collection_save", QStringLiteral("Save a search as a smart collection: 'name' and browser.query's 'text' (a "
		"substring or an expression - OR, -word, \"phrase\", name: path: tag: kind: ext:), 'tags', 'kind', 'root' and "
		"'category'. A collection of the same name is replaced. Kept beside the browser tags in the config directory, not the "
		"project; one undo step."),
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("text"), stringProperty()},
			{QStringLiteral("tags"), arrayProperty()}, {QStringLiteral("kind"), stringProperty()},
			{QStringLiteral("root"), stringProperty()}, {QStringLiteral("category"), stringProperty()}},
			{QStringLiteral("name")}), save, true);
	add(registry, "collection_remove", QStringLiteral("Remove a smart collection ('name'). One undo step."),
		objectSchema({{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("name")}), remove, true);
	add(registry, "collections", QStringLiteral("The saved smart collections, each with its query."), objectSchema({}), list, false);
	add(registry, "collection_run", QStringLiteral("What a smart collection ('name') finds now - browser.query's answer for its "
		"stored query, with 'limit', 'offset' and 'probe' as browser.query takes them."),
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("limit"), integerProperty(1, 500)},
			{QStringLiteral("offset"), integerProperty(0, 2147483647)}, {QStringLiteral("probe"), booleanProperty()}},
			{QStringLiteral("name")}), run, false);
}

} // namespace lmms
