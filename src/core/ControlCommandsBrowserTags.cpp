/*
 * ControlCommandsBrowserTags.cpp - the browser.* group's mutating half:
 *                                  browser.tag.add and browser.tag.remove.
 *
 * WHAT THESE WRITE, AND WHY THE INVERSE IS A COMMAND. A tag is filed in the
 * user's config directory (include/BrowserCatalog.h records why not the project
 * file). That store is not a JournallingObject and the project journal holds
 * nothing for it, so SPEC A16's true_inverse - "a live checkpoint restores it"
 * - does not exist here. The class is SNAPSHOT: the tag set the edit started
 * from is recorded in the transaction's before-state, and the inverse is the
 * PAIRED COMMAND (`applies: "command"`), which control.undo dispatches through
 * the registry. `browser.tag.add` and `browser.tag.remove` are therefore each
 * other's inverse, and the test drives add -> control.undo -> read back, not
 * just the descriptor.
 *
 * Every refusal happens BEFORE the write: a tag that is already there, a tag
 * that is not there, an unreadable store, a library already at the distinct-tag
 * bound. A refused call leaves the in-memory store and the file exactly as they
 * were.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "BrowserCatalog.h"
#include "ControlBrowserSupport.h"
#include "ControlRegistry.h"

namespace lmms
{

using namespace control;

namespace
{

//! The store, re-read from its file. False with a typed refusal when the file
//! exists and cannot be parsed: overwriting a store we failed to read would
//! destroy the library it holds.
bool loadedStore(BrowserTagStore** store, ControlResult* error)
{
	BrowserTagStore* instance = &BrowserTagStore::instance();
	if (!instance->load())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the tag store %1 could not be read, so nothing was written: %2")
				.arg(instance->storePath(), instance->lastError()));
		return false;
	}
	*store = instance;
	return true;
}

QString addMechanism()
{
	return QStringLiteral("snapshot: the tag store is a user-config JSON file, not a "
		"JournallingObject, so no ProjectJournal checkpoint can hold it. The inverse is the "
		"paired COMMAND browser.tag.remove (applies=command), dispatched by control.undo "
		"through the registry; the tag set the edit started from is in the transaction's "
		"before-state");
}

QString removeMechanism()
{
	return QStringLiteral("snapshot: the tag store is a user-config JSON file, not a "
		"JournallingObject, so no ProjectJournal checkpoint can hold it. The inverse is the "
		"paired COMMAND browser.tag.add (applies=command), dispatched by control.undo "
		"through the registry; the tag set the edit started from is in the transaction's "
		"before-state");
}

ControlResult tagAdd(const QJsonObject& args)
{
	QString key;
	QString tag;
	ControlResult error;
	if (!readBrowserTagArguments(args, &key, &tag, &error)) { return error; }

	BrowserTagStore* store = nullptr;
	if (!loadedStore(&store, &error)) { return error; }

	if (store->has(key, tag))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 already carries the tag '%2': an edit that changes nothing is "
				"not an edit, and nothing was written").arg(key, tag));
	}
	const QStringList vocabulary = store->allTags();
	if (!vocabulary.contains(tag, Qt::CaseInsensitive) && vocabulary.size() >= MaxDistinctTags)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the library already holds the %1 distinct tags the store bounds; "
				"browser.tag.remove one before adding '%2'").arg(MaxDistinctTags).arg(tag));
	}

	const QStringList before = store->tagsOf(key);
	QString why;
	if (!store->add(key, tag, &why))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the tag was not persisted: %1").arg(why));
	}

	QJsonObject result = browserTagState(key, store->tagsOf(key));
	result.insert(QStringLiteral("tag"), tag);
	result.insert(QStringLiteral("action"), QStringLiteral("added"));
	result.insert(QStringLiteral("__transaction"),
		browserTagTransaction(key, tag, before, QStringLiteral("browser.tag.remove"), addMechanism()));
	return ControlResult::success(result);
}

ControlResult tagRemove(const QJsonObject& args)
{
	QString key;
	QString tag;
	ControlResult error;
	if (!readBrowserTagArguments(args, &key, &tag, &error)) { return error; }

	BrowserTagStore* store = nullptr;
	if (!loadedStore(&store, &error)) { return error; }

	if (!store->has(key, tag))
	{
		const QStringList present = store->tagsOf(key);
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 does not carry the tag '%2' (it carries %3)")
				.arg(key, tag, present.isEmpty() ? QStringLiteral("none")
					: QStringLiteral("'") + present.join(QStringLiteral("', '")) + QStringLiteral("'")));
	}

	const QStringList before = store->tagsOf(key);
	QString why;
	if (!store->remove(key, tag, &why))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the removal was not persisted: %1").arg(why));
	}

	QJsonObject result = browserTagState(key, store->tagsOf(key));
	result.insert(QStringLiteral("tag"), tag);
	result.insert(QStringLiteral("action"), QStringLiteral("removed"));
	result.insert(QStringLiteral("__transaction"),
		browserTagTransaction(key, tag, before, QStringLiteral("browser.tag.add"), removeMechanism()));
	return ControlResult::success(result);
}

} // namespace

void registerBrowserTagCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("browser.tag.add");
		cmd.group = QStringLiteral("browser");
		cmd.verb = QStringLiteral("tag.add");
		cmd.description = QStringLiteral("Tag a file in the browser's library. The tag is "
			"filed against the file's canonical path in a JSON store in the user's config "
			"directory, and the write is reported: a tag that cannot be persisted is a "
			"refusal, not a silent success. Refused when the file already carries the tag. "
			"Reversible: the recorded inverse is browser.tag.remove, which control.undo "
			"dispatches.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("tag"), stringProperty()},
		}, {QStringLiteral("path"), QStringLiteral("tag")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("tag"), stringProperty()},
			{QStringLiteral("action"), stringProperty()},
			{QStringLiteral("tags"), browserStringListProperty()},
			{QStringLiteral("store_path"), stringProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return tagAdd(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("browser.tag.remove");
		cmd.group = QStringLiteral("browser");
		cmd.verb = QStringLiteral("tag.remove");
		cmd.description = QStringLiteral("Remove a tag from a file in the browser's library, "
			"persisting the store. Not-found when the file does not carry the tag: a removal "
			"that removes nothing is a refusal. Reversible: the recorded inverse is "
			"browser.tag.add, which control.undo dispatches.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("tag"), stringProperty()},
		}, {QStringLiteral("path"), QStringLiteral("tag")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("tag"), stringProperty()},
			{QStringLiteral("action"), stringProperty()},
			{QStringLiteral("tags"), browserStringListProperty()},
			{QStringLiteral("store_path"), stringProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return tagRemove(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
