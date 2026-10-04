/*
 * ControlCommandsBrowserNavigation.cpp - the browser's history and the source it shows in each view
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
/*! 0.5.0 L11, where the browser has been and where it opens:
 *  - browser.browse / browser.history / browser.history_go (WF-014): every list the browser shows - a
 *    search, an indexed search, a collection, a similar-sound search - run through browse is remembered,
 *    and history_go steps back and forward through them (running the remembered one again, so the list
 *    is what the library holds now). Session state, bounded to MaxHistory entries;
 *  - browser.sources / browser.remember_source (WF-023): the source the browser shows in each view
 *    (arrange, mix, edit), a preference in the config file the browser panel restores when the view
 *    changes and writes when the user picks a source. Interface state: neither is journalled. */

#include <algorithm>
#include <vector>

#include <QJsonArray>

#include "ConfigManager.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxHistory = 64;

const QStringList& browseCommands()
{
	static const QStringList ids{QStringLiteral("browser.query"), QStringLiteral("browser.index_query"),
		QStringLiteral("browser.collection_run"), QStringLiteral("browser.similar")};
	return ids;
}

const QStringList& contexts()
{
	static const QStringList names{QStringLiteral("arrange"), QStringLiteral("mix"), QStringLiteral("edit")};
	return names;
}

struct History
{
	std::vector<QJsonObject> entries;
	int position = -1;
};

History& history()
{
	static History h;
	return h;
}

//! Run @a entry's command; its result with the entry and where the history stands.
ControlResult runEntry(const QJsonObject& entry)
{
	ControlResult result = ControlRegistry::instance()->invoke(entry.value(QStringLiteral("command")).toString(),
		entry.value(QStringLiteral("args")).toObject());
	if (!result.ok) { return result; }
	result.result.insert(QStringLiteral("entry"), entry);
	result.result.insert(QStringLiteral("history_position"), history().position);
	result.result.insert(QStringLiteral("history_size"), static_cast<int>(history().entries.size()));
	return result;
}

ControlResult browse(const QJsonObject& args)
{
	const QJsonObject entry{{QStringLiteral("command"), args.value(QStringLiteral("command"))},
		{QStringLiteral("args"), args.value(QStringLiteral("args")).toObject()}, {QStringLiteral("label"), args.value(QStringLiteral("label"))}};
	History& h = history();
	const std::vector<QJsonObject> before = h.entries;
	const int was = h.position;
	// Theme 3 (0.5.0 UX): a search refined as it is typed replaces the entry it refines, not one entry per keystroke.
	const bool replace = args.value(QStringLiteral("replace")).toBool() && h.position >= 0;
	h.entries.resize(static_cast<std::size_t>(h.position + 1));  // a new step drops the forward history
	if (replace) { h.entries.back() = entry; }
	else { h.entries.push_back(entry); }
	if (static_cast<int>(h.entries.size()) > MaxHistory) { h.entries.erase(h.entries.begin()); }
	h.position = static_cast<int>(h.entries.size()) - 1;
	ControlResult result = runEntry(entry);
	if (!result.ok)
	{
		h.entries = before;  // a list that could not be shown is not remembered
		h.position = was;
	}
	return result;
}

ControlResult listHistory(const QJsonObject&)
{
	QJsonArray entries;
	for (const QJsonObject& entry : history().entries) { entries.append(entry); }
	return ControlResult::success({{QStringLiteral("entries"), entries}, {QStringLiteral("position"), history().position},
		{QStringLiteral("limit"), MaxHistory}});
}

ControlResult historyGo(const QJsonObject& args)
{
	History& h = history();
	const int target = h.position + args.value(QStringLiteral("step")).toInt(-1);
	if (h.entries.empty() || target < 0 || target >= static_cast<int>(h.entries.size()))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the browser history has no entry there"));
	}
	const int was = h.position;
	h.position = target;  // before running: the reply reports where the history stands
	ControlResult result = runEntry(h.entries[static_cast<std::size_t>(target)]);
	if (!result.ok) { h.position = was; }  // as browse(): a list that could not be shown is not where the history is
	return result;
}

QString sourceKey(const QString& context) { return QStringLiteral("source_") + context; }

ControlResult sources(const QJsonObject&)
{
	QJsonObject remembered;
	for (const QString& context : contexts()) { remembered.insert(context, ConfigManager::inst()->value(QStringLiteral("browser"), sourceKey(context))); }
	return ControlResult::success({{QStringLiteral("sources"), remembered}});
}

ControlResult rememberSource(const QJsonObject& args)
{
	const QString context = args.value(QStringLiteral("context")).toString();
	const QString source = args.value(QStringLiteral("source")).toString();
	ConfigManager::inst()->setValue(QStringLiteral("browser"), sourceKey(context), source);
	ConfigManager::inst()->saveConfigFile();  // as the other preferences are: a crash must not forget it
	return ControlResult::success({{QStringLiteral("context"), context}, {QStringLiteral("source"), source}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	const QStringList& required, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("browser.") + QLatin1String(verb), description, objectSchema(args, QJsonArray::fromStringList(required)), false, handler, false);
}

} // namespace

void registerBrowserNavigationCommands(ControlRegistry& registry)
{
	add(registry, "browse", QStringLiteral("Show a list in the browser and remember it: runs 'command' (browser.query, "
		"browser.index_query, browser.collection_run or browser.similar) with 'args', answers its result, and adds it to the "
		"browser's history ('label' is what the history shows) - or, with 'replace', puts it in place of the current entry (a "
		"search refined as it is typed). A step after going back drops the forward history."),
		{{QStringLiteral("command"), enumProperty(browseCommands())}, {QStringLiteral("args"), objectProperty()},
			{QStringLiteral("label"), stringProperty()}, {QStringLiteral("replace"), booleanProperty()}}, {QStringLiteral("command")}, browse);
	add(registry, "history", QStringLiteral("The browser's history (most recent last) and where it stands. Session state."), {}, {}, listHistory);
	add(registry, "history_go", QStringLiteral("Go back ('step' -1, the default) or forward (+1) - or several steps - in the "
		"browser's history: the remembered list is run again and its result answered."),
		{{QStringLiteral("step"), integerProperty(-MaxHistory, MaxHistory)}}, {}, historyGo);
	add(registry, "sources", QStringLiteral("The source the browser shows in each view (arrange, mix, edit) - empty when none "
		"is remembered. A preference in the config file."), {}, {}, sources);
	add(registry, "remember_source", QStringLiteral("Remember 'source' (a browser source's title: Devices, Search, Presets, "
		"Samples, ...) as what the browser shows in view 'context'; the browser switches to it when that view opens. What "
		"picking a source in the browser does. A preference in the config file, not the project: not journalled."),
		{{QStringLiteral("context"), enumProperty(contexts())}, {QStringLiteral("source"), stringProperty()}},
		{QStringLiteral("context"), QStringLiteral("source")}, rememberSource);
}

} // namespace lmms
