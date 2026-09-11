/*
 * PluginScanCache.h - on-disk cache of plugin scan results plus a quarantine list.
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

#ifndef LMMS_PLUGIN_SCAN_CACHE_H
#define LMMS_PLUGIN_SCAN_CACHE_H

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

class QJsonObject;

namespace lmms
{

/*!
 * The remembered outcome of scanning one plugin file.
 *
 * A record is only trusted when the file's path, size *and* modification time
 * still match (see PluginScanCache::lookup()), so a re-built or replaced
 * library is always re-scanned.
 *
 * This class is pure data: it never loads a library and never touches the
 * audio thread. It is filled in by PluginFactory's scan and read back on the
 * next scan.
 */
class LMMS_EXPORT PluginScanRecord
{
public:
	enum class Status {
		HasDescriptor, //!< an LMMS plugin: its descriptor is remembered below
		NotAPlugin,    //!< loaded (or not) but exposes no LMMS plugin descriptor
		LoadFailed,    //!< QLibrary::load() refused the file
	};

	QString filePath; //!< absolute path
	qint64 size = -1;
	qint64 mtimeMs = -1;
	Status status = Status::NotAPlugin;
	QString error; //!< QLibrary::errorString(), for LoadFailed

	// --- descriptor metadata, meaningful for Status::HasDescriptor ---------
	QString name;
	QString displayName;
	QString description;
	QString author;
	QString supportedFileTypes;
	QString logoName; //!< pixmap name of the descriptor's logo, or empty
	int version = 0;
	int type = 255; //!< Plugin::Type as an int (255 = Undefined)

	/*!
	 * True when the descriptor's logo is a compiled-in XPM rather than a
	 * pixmap name: such a logo cannot be rebuilt without loading the library,
	 * so PluginFactory never serves that file from the cache.
	 */
	bool logoHasInlinePixmap = false;

	/*!
	 * True when the descriptor carries SubPluginFeatures. Rebuilding those
	 * needs the library itself (sub-plugin enumeration is a virtual call into
	 * it), so PluginFactory never serves such a file from the cache.
	 */
	bool hasSubPluginFeatures = true;
};

/*!
 * A JSON-backed cache of plugin scan results plus a quarantine (skip) list.
 *
 * Failure tolerance is the contract, not a nicety: a missing, unreadable,
 * corrupt, wrongly-versioned or partly-malformed file must degrade to a full
 * scan. load() therefore never throws, never aborts and never leaves a
 * half-populated cache behind; save() writes through QSaveFile so a failed
 * write leaves the previous file intact. Discovery results must never depend on
 * the state of this file - it only decides how much work a scan repeats.
 *
 * Where it runs: constructed and used only from PluginFactory's scan, which is
 * a GUI-thread path (see docs/PLUGIN-SCAN-CACHE.md, "threads"). Nothing here
 * is called from an audio-thread path.
 */
class LMMS_EXPORT PluginScanCache
{
public:
	PluginScanCache() = default;
	explicit PluginScanCache(const QString& filePath);

	/*!
	 * The product default: plugin-scan-cache.json in the user's LMMS working
	 * directory (ConfigManager::workingDir(), the same directory whose
	 * "plugins" subdirectory is searched). Tests get an empty path - i.e.
	 * persistence switched off - unless LMMS_PLUGIN_SCAN_CACHE names a file.
	 */
	static QString defaultFilePath();

	const QString& filePath() const { return m_filePath; }
	bool isPersistent() const { return !m_filePath.isEmpty(); }

	/*! Read the file. Returns false and leaves the cache empty on any failure. */
	bool load();
	/*! Write the file. Returns false (and leaves the old file alone) on failure. */
	bool save() const;

	bool isDirty() const { return m_dirty; }

	/*!
	 * The remembered scan of \a file, or nullptr when it was never scanned or
	 * the file changed since (path + size + mtime must all match).
	 */
	const PluginScanRecord* lookup(const QFileInfo& file) const;

	/*! Remember (or refresh) one file's scan outcome. */
	void store(const PluginScanRecord& record);

	// --- quarantine / skip list --------------------------------------------
	bool isQuarantined(const QString& path) const;
	QString quarantineReason(const QString& path) const;
	void addToQuarantine(const QString& path, const QString& reason);
	bool removeFromQuarantine(const QString& path);
	QList<QPair<QString, QString>> quarantineEntries() const { return m_quarantine; }

	int fileCount() const { return m_files.size(); }
	int quarantineCount() const { return m_quarantine.size(); }

	static constexpr int s_formatVersion = 1;

private:
	QString m_filePath;
	QHash<QString, PluginScanRecord> m_files;
	QList<QPair<QString, QString>> m_quarantine; //!< path -> reason, in file order
	mutable bool m_dirty = false;

	//! Apply the "files"/"quarantine" arrays of a parsed cache file.
	void readFileRecords(const QJsonObject& root);
	void readQuarantine(const QJsonObject& root);
};

} // namespace lmms

#endif // LMMS_PLUGIN_SCAN_CACHE_H
