/*
 * BrowserTagStore.cpp - the persisted tag store: path -> tags, in a JSON file
 *                       in the user's config directory.
 *
 * The header (include/BrowserCatalog.h) records why the tags live in the
 * config directory rather than in the project file. This file is the mechanism:
 * a small JSON document, written atomically, read back on demand, with every
 * failure reported to the caller instead of dropped - an agent that believes it
 * tagged a file must be able to tell that it did not.
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

#include "BrowserCatalog.h"

#include <algorithm>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

#include "ConfigManager.h"

namespace lmms
{

namespace
{

//! The store's format version, so a future reader can tell an old document from
//! a new one instead of inferring it from which keys happen to be present.
constexpr int kFormatVersion = 1;

bool sameTag(const QString& a, const QString& b)
{
	return QString::compare(a, b, Qt::CaseInsensitive) == 0;
}

//! Inserts \a tag in sorted position, case-insensitively unique. False when the
//! list already carried it (in any spelling: "Bass" and "bass" are one tag).
bool insertTag(QStringList* tags, const QString& tag)
{
	for (const QString& present : *tags)
	{
		if (sameTag(present, tag)) { return false; }
	}
	tags->append(tag);
	std::sort(tags->begin(), tags->end(), [](const QString& a, const QString& b) {
		return QString::compare(a, b, Qt::CaseInsensitive) < 0;
	});
	return true;
}

bool eraseTag(QStringList* tags, const QString& tag)
{
	for (int i = 0; i < tags->size(); ++i)
	{
		if (sameTag(tags->at(i), tag))
		{
			tags->removeAt(i);
			return true;
		}
	}
	return false;
}

//! Writes \a tags under \a key, dropping the entry when the list is empty: the
//! store holds files that carry at least one tag, so "no tags left" is the
//! absence of an entry and not an empty array.
void storeEntry(QHash<QString, QStringList>* map, const QString& key, const QStringList& tags)
{
	if (tags.isEmpty()) { map->remove(key); }
	else { map->insert(key, tags); }
}

//! NULL-safe read of the store's tag table.
QJsonObject tagsTableOf(const QJsonDocument& document, bool* ok, QString* error)
{
	if (!document.isObject())
	{
		*ok = false;
		*error = QStringLiteral("the tag store's root is not a JSON object");
		return QJsonObject();
	}
	const QJsonValue tags = document.object().value(QStringLiteral("tags"));
	if (!tags.isObject())
	{
		*ok = false;
		*error = QStringLiteral("the tag store carries no \"tags\" object");
		return QJsonObject();
	}
	*ok = true;
	error->clear();
	return tags.toObject();
}

} // namespace

BrowserTagStore& BrowserTagStore::instance()
{
	static BrowserTagStore store;
	return store;
}

QString BrowserTagStore::storePath() const
{
	return m_path.isEmpty() ? browserTagStorePath() : m_path;
}

QString BrowserTagStore::lastError() const
{
	return m_error;
}

bool BrowserTagStore::openAt(const QString& path)
{
	m_path = path;
	return load();
}

bool BrowserTagStore::load()
{
	m_tags.clear();
	m_error.clear();
	if (m_path.isEmpty()) { m_path = browserTagStorePath(); }

	QFile file(m_path);
	if (!file.exists()) { return true; }
	if (!file.open(QIODevice::ReadOnly))
	{
		m_error = QStringLiteral("cannot read %1: %2").arg(m_path, file.errorString());
		return false;
	}

	bool ok = false;
	const QJsonObject tags = tagsTableOf(QJsonDocument::fromJson(file.readAll()), &ok, &m_error);
	file.close();
	if (!ok) { return false; }

	for (auto it = tags.begin(); it != tags.end(); ++it)
	{
		QStringList list;
		for (const QJsonValue& value : it.value().toArray())
		{
			const QString tag = value.toString().trimmed();
			if (!tag.isEmpty()) { insertTag(&list, tag); }
		}
		storeEntry(&m_tags, it.key(), list);
	}
	return true;
}

bool BrowserTagStore::save() const
{
	QJsonObject tags;
	for (auto it = m_tags.begin(); it != m_tags.end(); ++it)
	{
		if (it.value().isEmpty()) { continue; }
		tags.insert(it.key(), QJsonArray::fromStringList(it.value()));
	}
	QJsonObject root;
	root.insert(QStringLiteral("version"), kFormatVersion);
	root.insert(QStringLiteral("tags"), tags);

	const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
	// storePath(), not m_path: the store has a default location, and a save that
	// ran before any load would otherwise be asked to write to no file at all.
	QSaveFile file(storePath());
	if (!file.open(QIODevice::WriteOnly))
	{
		m_error = QStringLiteral("cannot write %1: %2").arg(storePath(), file.errorString());
		return false;
	}
	if (file.write(bytes) != bytes.size() || !file.commit())
	{
		m_error = QStringLiteral("cannot write %1: %2").arg(storePath(), file.errorString());
		return false;
	}
	m_error.clear();
	return true;
}

QStringList BrowserTagStore::tagsOf(const QString& file) const
{
	return m_tags.value(browserCanonicalKey(file));
}

bool BrowserTagStore::has(const QString& file, const QString& tag) const
{
	for (const QString& present : tagsOf(file))
	{
		if (sameTag(present, tag)) { return true; }
	}
	return false;
}

int BrowserTagStore::entryCount() const
{
	return m_tags.size();
}

QStringList BrowserTagStore::allTags() const
{
	QStringList out;
	for (auto it = m_tags.begin(); it != m_tags.end(); ++it)
	{
		for (const QString& tag : it.value()) { insertTag(&out, tag); }
	}
	return out;
}

int BrowserTagStore::filesWithTag(const QString& tag) const
{
	int count = 0;
	for (auto it = m_tags.begin(); it != m_tags.end(); ++it)
	{
		for (const QString& present : it.value())
		{
			if (sameTag(present, tag))
			{
				++count;
				break;
			}
		}
	}
	return count;
}

bool BrowserTagStore::add(const QString& file, const QString& tag, QString* error)
{
	const QString key = browserCanonicalKey(file);
	const QStringList before = m_tags.value(key);
	QStringList after = before;
	if (!insertTag(&after, tag))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("%1 already carries the tag '%2'").arg(key, tag);
		}
		return false;
	}

	storeEntry(&m_tags, key, after);
	if (save()) { return true; }

	// The write did not reach the disk, so the in-memory store is put back the
	// way the file still is: a failed call leaves nothing behind anywhere.
	storeEntry(&m_tags, key, before);
	if (error != nullptr) { *error = m_error; }
	return false;
}

bool BrowserTagStore::remove(const QString& file, const QString& tag, QString* error)
{
	const QString key = browserCanonicalKey(file);
	const QStringList before = m_tags.value(key);
	QStringList after = before;
	if (!eraseTag(&after, tag))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("%1 does not carry the tag '%2'").arg(key, tag);
		}
		return false;
	}

	storeEntry(&m_tags, key, after);
	if (save()) { return true; }

	storeEntry(&m_tags, key, before);
	if (error != nullptr) { *error = m_error; }
	return false;
}

void BrowserTagStore::clear()
{
	m_tags.clear();
}

} // namespace lmms
