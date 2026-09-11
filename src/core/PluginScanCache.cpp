/*
 * PluginScanCache.cpp - on-disk cache of plugin scan results plus a quarantine list.
 *
 * Copyright (c) 2026 LMMS contributors
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

#include "PluginScanCache.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "ConfigManager.h"

namespace lmms
{

namespace
{

constexpr auto s_hasDescriptor = "has-descriptor";
constexpr auto s_notAPlugin = "not-a-plugin";
constexpr auto s_loadFailed = "load-failed";

QString statusToString(PluginScanRecord::Status status)
{
	switch (status)
	{
	case PluginScanRecord::Status::HasDescriptor: return s_hasDescriptor;
	case PluginScanRecord::Status::NotAPlugin: return s_notAPlugin;
	case PluginScanRecord::Status::LoadFailed: return s_loadFailed;
	}
	return s_notAPlugin;
}

bool statusFromString(const QString& text, PluginScanRecord::Status& out)
{
	if (text == QLatin1String(s_hasDescriptor)) { out = PluginScanRecord::Status::HasDescriptor; return true; }
	if (text == QLatin1String(s_notAPlugin)) { out = PluginScanRecord::Status::NotAPlugin; return true; }
	if (text == QLatin1String(s_loadFailed)) { out = PluginScanRecord::Status::LoadFailed; return true; }
	return false;
}

QJsonObject recordToJson(const PluginScanRecord& rec)
{
	QJsonObject obj;
	obj.insert("path", rec.filePath);
	obj.insert("size", double(rec.size));
	obj.insert("mtime", double(rec.mtimeMs));
	obj.insert("status", statusToString(rec.status));
	if (!rec.error.isEmpty()) { obj.insert("error", rec.error); }
	if (rec.status == PluginScanRecord::Status::HasDescriptor)
	{
		obj.insert("name", rec.name);
		obj.insert("displayName", rec.displayName);
		obj.insert("description", rec.description);
		obj.insert("author", rec.author);
		obj.insert("version", rec.version);
		obj.insert("type", rec.type);
		obj.insert("supportedFileTypes", rec.supportedFileTypes);
		obj.insert("logoName", rec.logoName);
		obj.insert("logoHasInlinePixmap", rec.logoHasInlinePixmap);
		obj.insert("subPluginFeatures", rec.hasSubPluginFeatures);
	}
	return obj;
}

/*! One malformed record is dropped, not the whole file: returns false for a
    record that cannot be trusted at all. */
bool recordFromJson(const QJsonObject& obj, PluginScanRecord& out)
{
	if (!obj.value("path").isString() || obj.value("path").toString().isEmpty()) { return false; }
	out.filePath = obj.value("path").toString();
	if (!obj.value("size").isDouble() || !obj.value("mtime").isDouble()) { return false; }
	out.size = qint64(obj.value("size").toDouble());
	out.mtimeMs = qint64(obj.value("mtime").toDouble());
	if (!statusFromString(obj.value("status").toString(), out.status)) { return false; }
	out.error = obj.value("error").toString();
	out.name = obj.value("name").toString();
	out.displayName = obj.value("displayName").toString();
	out.description = obj.value("description").toString();
	out.author = obj.value("author").toString();
	out.version = obj.value("version").toInt();
	out.type = obj.value("type").toInt(255);
	out.supportedFileTypes = obj.value("supportedFileTypes").toString();
	out.logoName = obj.value("logoName").toString();
	out.logoHasInlinePixmap = obj.value("logoHasInlinePixmap").toBool(false);
	out.hasSubPluginFeatures = obj.value("subPluginFeatures").toBool(true);
	return true;
}

/*!
 * Read and parse the cache file.
 *
 * Failure is reported once and is never fatal: the caller gets false and an
 * empty document, so the scan repeats all the work instead of trusting
 * anything it could not read.
 */
bool readCacheFile(const QString& path, QJsonDocument& out)
{
	QFile file(path);
	if (!file.exists()) { return false; } // no cache yet: a full scan, not an error
	if (!file.open(QIODevice::ReadOnly))
	{
		qWarning() << "plugin-scan-cache: cannot read" << path << "-" << file.errorString()
			<< "- scanning all plugins";
		return false;
	}

	QJsonParseError parseError{};
	out = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !out.isObject())
	{
		qWarning() << "plugin-scan-cache: ignoring corrupt cache" << path << "-"
			<< parseError.errorString() << "- scanning all plugins";
		out = QJsonDocument();
		return false;
	}
	return true;
}

} // namespace


PluginScanCache::PluginScanCache(const QString& filePath) :
	m_filePath(filePath)
{
}


QString PluginScanCache::defaultFilePath()
{
	// An explicit path wins: tests and portable installs point this at a
	// scratch file (an empty value means "no persistence").
	if (qEnvironmentVariableIsSet("LMMS_PLUGIN_SCAN_CACHE"))
	{
		return qEnvironmentVariable("LMMS_PLUGIN_SCAN_CACHE");
	}
	// The product writes plugin-scan-cache.json next to the user's "plugins"
	// directory. No application (a bare library user, a unit test) and no
	// working directory yet means there is nowhere to persist to, and the scan
	// stays a plain full scan - which is exactly what today's behaviour is.
	if (QCoreApplication::instance() == nullptr) { return QString(); }
	const QString workingDir = ConfigManager::inst()->workingDir();
	if (!QDir(workingDir).exists()) { return QString(); }
	return workingDir + "plugin-scan-cache.json";
}


bool PluginScanCache::load()
{
	m_files.clear();
	m_quarantine.clear();
	m_dirty = false;

	if (!isPersistent()) { return false; }

	QJsonDocument doc;
	if (!readCacheFile(m_filePath, doc)) { return false; }

	const QJsonObject root = doc.object();
	if (root.value("version").toInt(0) != s_formatVersion)
	{
		qWarning() << "plugin-scan-cache: ignoring" << m_filePath
			<< "written in an unknown format version" << root.value("version").toInt(0)
			<< "- scanning all plugins";
		return false;
	}

	readFileRecords(root);
	readQuarantine(root);
	m_dirty = false;
	return true;
}


void PluginScanCache::readFileRecords(const QJsonObject& root)
{
	for (const QJsonValue& value : root.value("files").toArray())
	{
		PluginScanRecord rec;
		if (value.isObject() && recordFromJson(value.toObject(), rec)) { m_files.insert(rec.filePath, rec); }
	}
}


void PluginScanCache::readQuarantine(const QJsonObject& root)
{
	for (const QJsonValue& value : root.value("quarantine").toArray())
	{
		const QJsonObject entry = value.toObject();
		const QString path = entry.value("path").toString();
		if (path.isEmpty()) { continue; }
		m_quarantine.append({path, entry.value("reason").toString()});
	}
}


bool PluginScanCache::save() const
{
	if (!isPersistent()) { return false; }

	QJsonArray files;
	for (const PluginScanRecord& rec : m_files) { files.append(recordToJson(rec)); }

	QJsonArray quarantine;
	for (const auto& entry : m_quarantine)
	{
		QJsonObject obj;
		obj.insert("path", entry.first);
		obj.insert("reason", entry.second);
		quarantine.append(obj);
	}

	QJsonObject root;
	root.insert("version", s_formatVersion);
	root.insert("files", files);
	root.insert("quarantine", quarantine);

	// QSaveFile: the previous cache stays intact unless the new one lands whole.
	QSaveFile out(m_filePath);
	if (!out.open(QIODevice::WriteOnly))
	{
		qWarning() << "plugin-scan-cache: cannot write" << m_filePath << "-" << out.errorString();
		return false;
	}
	if (out.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !out.commit())
	{
		qWarning() << "plugin-scan-cache: cannot write" << m_filePath << "-" << out.errorString();
		return false;
	}
	m_dirty = false;
	return true;
}


const PluginScanRecord* PluginScanCache::lookup(const QFileInfo& file) const
{
	const auto it = m_files.constFind(file.absoluteFilePath());
	if (it == m_files.constEnd()) { return nullptr; }

	const PluginScanRecord& rec = *it;
	if (rec.size != file.size() || rec.mtimeMs != file.lastModified().toMSecsSinceEpoch()) { return nullptr; }
	return &rec;
}


void PluginScanCache::store(const PluginScanRecord& record)
{
	if (record.filePath.isEmpty()) { return; }

	// "Unchanged" is defined as "would be written identically": the JSON form
	// is the cache's own notion of a record, so comparing it needs no
	// field-by-field list to keep in sync with the serialiser.
	const auto it = m_files.constFind(record.filePath);
	if (it != m_files.constEnd() && recordToJson(*it) == recordToJson(record))
	{
		return; // unchanged: do not dirty the cache
	}

	m_files.insert(record.filePath, record);
	m_dirty = true;
}


bool PluginScanCache::isQuarantined(const QString& path) const
{
	for (const auto& entry : m_quarantine)
	{
		if (entry.first == path) { return true; }
	}
	return false;
}


QString PluginScanCache::quarantineReason(const QString& path) const
{
	for (const auto& entry : m_quarantine)
	{
		if (entry.first == path) { return entry.second; }
	}
	return QString();
}


void PluginScanCache::addToQuarantine(const QString& path, const QString& reason)
{
	if (path.isEmpty() || isQuarantined(path)) { return; }
	m_quarantine.append({path, reason});
	m_dirty = true;
}


bool PluginScanCache::removeFromQuarantine(const QString& path)
{
	for (auto it = m_quarantine.begin(); it != m_quarantine.end(); ++it)
	{
		if (it->first == path)
		{
			m_quarantine.erase(it);
			m_dirty = true;
			return true;
		}
	}
	return false;
}

} // namespace lmms
