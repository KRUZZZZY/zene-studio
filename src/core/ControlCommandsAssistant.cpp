/*
 * ControlCommandsAssistant.cpp - app.ask and app.whats_new: answers and release notes, offline
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

/*! 0.5.0 W3 L14:
 *  - app.ask (WF-142): a question answered from what the program ships - the manual's sections and every
 *    registered command's description - by matching its words (each weighted by how rare it is across the
 *    two). Offline by design: no network call is made and nothing is sent anywhere;
 *  - app.whats_new (WF-141): the release notes the program ships (data/whatsnew/notes.json) for this
 *    version, and whether they have been shown here (window.whats_new shows them, once per version, at
 *    start). There is no update check: nothing is fetched. */

#include <algorithm>
#include <cmath>
#include <vector>

#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "JsonFile.h"
#include "lmmsversion.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int DefaultAnswers = 5;
constexpr int MaxAnswers = 20;
constexpr int SnippetChars = 280;

//! One thing an answer can point at: a manual section or a command.
struct Document
{
	QString kind;   //!< "manual" or "command"
	QString id;     //!< the manual anchor (app.help 'topic') or the command id
	QString title;
	QString text;
	QStringList words;
};

QStringList wordsOf(const QString& text)
{
	static const QRegularExpression split(QStringLiteral("[^a-z0-9]+"));
	static const QSet<QString> stop{QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("how"),
		QStringLiteral("can"), QStringLiteral("what"), QStringLiteral("with"), QStringLiteral("does"), QStringLiteral("this"),
		QStringLiteral("that"), QStringLiteral("from"), QStringLiteral("into"), QStringLiteral("are"), QStringLiteral("you"),
		QStringLiteral("not"), QStringLiteral("its"), QStringLiteral("one"), QStringLiteral("use"), QStringLiteral("when")};
	QStringList out;
	for (const QString& word : text.toLower().split(split, Qt::SkipEmptyParts))
	{
		if (word.size() >= 3 && !stop.contains(word)) { out.append(word); }
	}
	return out;
}

QString plainText(QString html)
{
	static const QRegularExpression tag(QStringLiteral("<[^>]+>"));
	html.remove(tag);
	html.replace(QStringLiteral("&gt;"), QStringLiteral(">")).replace(QStringLiteral("&lt;"), QStringLiteral("<"));
	return html.replace(QStringLiteral("&amp;"), QStringLiteral("&")).simplified();
}

//! The manual's sections: one document per <h2 id=...>, up to the next one.
void addManual(std::vector<Document>* docs)
{
	QFile file(ConfigManager::inst()->dataDir() + QStringLiteral("manual/index.html"));
	if (!file.open(QIODevice::ReadOnly)) { return; }
	const QString html = QString::fromUtf8(file.readAll());
	static const QRegularExpression section(QStringLiteral("<h2 id=\"([^\"]+)\">(.*?)</h2>(.*?)(?=<h2 |</body>)"),
		QRegularExpression::DotMatchesEverythingOption);
	for (auto it = section.globalMatch(html); it.hasNext();)
	{
		const QRegularExpressionMatch m = it.next();
		Document doc{QStringLiteral("manual"), m.captured(1), plainText(m.captured(2)), plainText(m.captured(3)), {}};
		doc.words = wordsOf(doc.title + QLatin1Char(' ') + doc.text);
		docs->push_back(doc);
	}
}

void addCommands(std::vector<Document>* docs)
{
	ControlRegistry* registry = ControlRegistry::instance();
	for (const QString& id : registry->commandIds())
	{
		const ControlCommand* command = registry->command(id);
		if (command == nullptr) { continue; }
		Document doc{QStringLiteral("command"), id, id, command->description, {}};
		QString idWords = id;
		idWords.replace(QLatin1Char('.'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
		// The id's own words count twice: a question naming "tempo" should find transport.set_tempo first.
		doc.words = wordsOf(idWords + QLatin1Char(' ') + idWords + QLatin1Char(' ') + command->description);
		docs->push_back(doc);
	}
}

//! How many documents hold each word.
QHash<QString, int> documentFrequency(const std::vector<Document>& docs)
{
	QHash<QString, int> df;
	for (const Document& doc : docs)
	{
		const QSet<QString> unique(doc.words.begin(), doc.words.end());
		for (const QString& word : unique) { ++df[word]; }
	}
	return df;
}

double score(const Document& doc, const QStringList& query, const QHash<QString, int>& df, std::size_t total)
{
	double sum = 0.0;
	for (const QString& word : query)
	{
		const int count = static_cast<int>(doc.words.count(word));
		if (count == 0) { continue; }
		const double rarity = std::log(1.0 + static_cast<double>(total) / std::max(1, df.value(word)));
		sum += rarity * (1.0 + std::log(static_cast<double>(count)));
	}
	return sum;
}

QJsonObject answerJson(const Document& doc, double points)
{
	return {{QStringLiteral("kind"), doc.kind}, {QStringLiteral("id"), doc.id}, {QStringLiteral("title"), doc.title},
		{QStringLiteral("text"), doc.text.left(SnippetChars)}, {QStringLiteral("score"), points}};
}

ControlResult ask(const QJsonObject& args)
{
	const QStringList query = wordsOf(args.value(QStringLiteral("question")).toString());
	if (query.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("ask a question with a few words in it"));
	}
	std::vector<Document> docs;
	addManual(&docs);
	addCommands(&docs);
	const QHash<QString, int> df = documentFrequency(docs);
	std::vector<std::pair<double, std::size_t>> ranked;
	for (std::size_t i = 0; i < docs.size(); ++i)
	{
		if (const double points = score(docs[i], query, df, docs.size()); points > 0.0) { ranked.emplace_back(points, i); }
	}
	std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	const auto limit = static_cast<std::size_t>(std::clamp(args.value(QStringLiteral("limit")).toInt(DefaultAnswers), 1, MaxAnswers));
	QJsonArray answers;
	for (std::size_t i = 0; i < ranked.size() && i < limit; ++i) { answers.append(answerJson(docs[ranked[i].second], ranked[i].first)); }
	return ControlResult::success({{QStringLiteral("answers"), answers}, {QStringLiteral("searched"), static_cast<int>(docs.size())},
		{QStringLiteral("offline"), true}});
}

QString seenVersion()
{
	return ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("whatsnewseen"));
}

//! The notes for this version: the newest entry whose version this one starts with, else the newest.
QJsonObject notesForThisVersion(const QJsonArray& versions)
{
	const QString current = QString::fromUtf8(LMMS_VERSION);
	for (const QJsonValue& entry : versions)
	{
		const QString version = entry.toObject().value(QStringLiteral("version")).toString();
		if (!version.isEmpty() && current.startsWith(version)) { return entry.toObject(); }
	}
	return versions.isEmpty() ? QJsonObject() : versions.first().toObject();
}

ControlResult whatsNew(const QJsonObject&)
{
	QString error;
	const QJsonObject doc = jsonfile::read(ConfigManager::inst()->dataDir() + QStringLiteral("whatsnew/notes.json"), &error);
	const QJsonArray versions = doc.value(QStringLiteral("versions")).toArray();
	if (versions.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, error.isEmpty() ? QStringLiteral("no release notes are installed") : error);
	}
	const QString current = QString::fromUtf8(LMMS_VERSION);
	return ControlResult::success({{QStringLiteral("version"), current}, {QStringLiteral("notes"), notesForThisVersion(versions)},
		{QStringLiteral("seen"), seenVersion() == current}, {QStringLiteral("all"), versions}, {QStringLiteral("update_check"), false}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("app");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.requiresEngine = false;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void markWhatsNewSeen()
{
	ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("whatsnewseen"), QString::fromUtf8(LMMS_VERSION));
	ConfigManager::inst()->saveConfigFile();
}

void registerAssistantCommands(ControlRegistry& registry)
{
	add(registry, "ask", QStringLiteral("Answer a question about the program from what it ships: the manual's sections and "
		"every command's description, best match first ({kind: manual|command, id, title, text, score}; a manual id is an "
		"app.help topic, a command id can be run). 'limit' answers (5 when left out, at most 20). Offline: nothing is "
		"sent anywhere."),
		objectSchema({{QStringLiteral("question"), stringProperty()}, {QStringLiteral("limit"), integerProperty(1, MaxAnswers)}},
			{QStringLiteral("question")}), ask);
	add(registry, "whats_new", QStringLiteral("The release notes the program ships for this version ('notes': version, "
		"title, items), every version's ('all'), and whether they have been shown here ('seen'; window.whats_new shows "
		"them, and the interface does so once per version at start). Nothing is fetched: there is no update check."),
		objectSchema({}), whatsNew);
}

} // namespace lmms
