/*
 * PluginFactory.h
 *
 * Copyright (c) 2015 Lukas W <lukaswhl/at/gmail.com>
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

#ifndef LMMS_PLUGIN_FACTORY_H
#define LMMS_PLUGIN_FACTORY_H

#include <memory>
#include <string>
#include <vector>

#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

#include "PluginScanCache.h"
#include "lmms_export.h"
#include "Plugin.h"

class QLibrary;  // IWYU pragma: keep

namespace lmms
{

class LMMS_EXPORT PluginFactory
{
public:
	struct PluginInfo
	{
		QString name() const;
		QFileInfo file;
		std::shared_ptr<QLibrary> library = nullptr;
		Plugin::Descriptor* descriptor = nullptr;

		bool isNull() const {return ! library;}
	};
	using PluginInfoList = QList<PluginInfo>;
	using DescriptorMap = QMultiMap<Plugin::Type, Plugin::Descriptor*>;

	/*!
	 * What the last discoverPlugins() run did. This is the scan's own report:
	 * how many files were found, how many the quarantine list hid, how many
	 * were answered from the cache without touching the library, and how many
	 * were actually loaded.
	 */
	struct ScanStats
	{
		int candidateFiles = 0;    //!< files in the search paths, after the env filter
		int quarantined = 0;       //!< skipped because the quarantine list names them
		int servedFromCache = 0;   //!< descriptor from the cache; library not loaded yet
		int negativeFromCache = 0; //!< remembered as "no plugin" / "fails to load"
		int scanned = 0;           //!< libraries loaded and resolved in this run
		int descriptors = 0;       //!< PluginInfos emitted (cached + scanned)
		QStringList quarantinedPaths;
		QStringList quarantinedReasons;
	};

	PluginFactory();
	~PluginFactory();

	static void setupSearchPaths();
	static QList<QRegularExpression> getExcludePatterns(const char* envVar);

	/// Returns the singleton instance of PluginFactory. You won't need to call
	/// this directly, use pluginFactory instead.
	static PluginFactory* instance();

	/// Returns a list of all found plugins' descriptors.
	Plugin::DescriptorList descriptors() const;
	Plugin::DescriptorList descriptors(Plugin::Type type) const;

	struct PluginInfoAndKey
	{
		PluginInfo info;
		Plugin::Descriptor::SubPluginFeatures::Key key;
		bool isNull() const { return info.isNull(); }
	};

	/// Returns a list of all found plugins' PluginFactory::PluginInfo objects.
	const PluginInfoList& pluginInfos() const;
	/// Returns a plugin that support the given file extension
	PluginInfoAndKey pluginSupportingExtension(const QString& ext);

	/// Returns the PluginInfo object of the plugin with the given name.
	/// If the plugin is not found, an empty PluginInfo is returned (use
	/// PluginInfo::isNull() to check this).
	PluginInfo pluginInfo(const char* name) const;

	/// When loading a library fails during discovery, the error string is saved.
	/// It can be retrieved by calling this function.
	QString errorString(QString pluginName) const;

	/// The scan cache this factory reads and writes. Exposed so the UI (and the
	/// tests) can quarantine a plugin; see docs/PLUGIN-SCAN-CACHE.md.
	PluginScanCache& scanCache() { return m_scanCache; }

	/// What the last discoverPlugins() run did (see ScanStats).
	const ScanStats& scanStats() const { return m_scanStats; }

	/// One line describing the last scan, for the log and for bug reports.
	QString scanReport() const;

#ifdef LMMS_TESTING
	/// Test hook: has the singleton been constructed at all? Used to prove that
	/// GUI startup does not trigger plugin discovery.
	static bool instanceExists() { return s_instance != nullptr; }
#endif

public slots:
	void discoverPlugins();

private:
	DescriptorMap m_descriptors;
	PluginInfoList m_pluginInfos;

	QMap<QString, PluginInfoAndKey> m_pluginByExt;
	std::vector<std::string> m_garbage; //!< cleaned up at destruction

	QHash<QString, QString> m_errors;

	PluginScanCache m_scanCache;
	ScanStats m_scanStats;

	/*!
	 * Host-owned copies of the descriptors rebuilt from cache records: the
	 * strings and the pixmap loader such a descriptor points at live here, so
	 * the rebuilt descriptor stays valid after the cache is gone.
	 */
	class CachedDescriptorStore;
	std::unique_ptr<CachedDescriptorStore> m_cachedDescriptors;

	//! Rebuild a descriptor from a cache record (declared here, defined in the .cpp).
	Plugin::Descriptor* descriptorFromCacheRecord(const PluginScanRecord& record);

	/*!
	 * Plugin discovery, split into its stages so that no single method carries
	 * the whole scan (per-method CCN target: tests/complexity-gate.sh). Each
	 * stage below is one of the steps discoverPlugins() used to inline; the
	 * sequence, the order of the decisions and the observable behaviour are
	 * unchanged - only the function boundaries moved.
	 */
	QSet<QFileInfo> candidatePluginFiles() const;
	void dropQuarantinedPlugins(QSet<QFileInfo>& files);
	void scanOnePlugin(const QFileInfo& file, PluginInfoList& pluginInfos,
		DescriptorMap& descriptors);
	void appendLoadedPlugin(const QFileInfo& file, const std::shared_ptr<QLibrary>& library,
		Plugin::Descriptor* descriptor, PluginInfoList& pluginInfos,
		DescriptorMap& descriptors);
	void appendCacheServedPlugin(const PluginScanRecord& record, const QFileInfo& file,
		PluginInfoList& pluginInfos, DescriptorMap& descriptors);
	void addSupportedFileTypes(const QString& supportedFileTypes, const PluginInfo& info,
		const Plugin::Descriptor::SubPluginFeatures::Key* key = nullptr);

	static std::unique_ptr<PluginFactory> s_instance;

	static void filterPlugins(QSet<QFileInfo>& files);
};

//Short-hand function
LMMS_EXPORT PluginFactory* getPluginFactory();


} // namespace lmms

#endif // LMMS_PLUGIN_FACTORY_H
