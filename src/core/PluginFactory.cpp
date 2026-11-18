/*
 * PluginFactory.cpp
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

#include "PluginFactory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include "SafeStart.h"
#include <QRegularExpression>
#include <memory>
#include "lmmsconfig.h"

#include "ConfigManager.h"
#include "embed.h"
#include "Plugin.h"
#include "PluginFactoryScanDetail.h"

// QT qHash specialization, needs to be in global namespace
qint64 qHash(const QFileInfo& fi)
{
	return qHash(fi.absoluteFilePath());
}

namespace lmms
{


#ifdef LMMS_BUILD_WIN32
	QStringList nameFilters("*.dll");
#else
	QStringList nameFilters("lib*.so");
#endif

std::unique_ptr<PluginFactory> PluginFactory::s_instance;


PluginFactory::PluginFactory() :
	m_scanCache(PluginScanCache::defaultFilePath()),
	m_cachedDescriptors(std::make_unique<CachedDescriptorStore>())
{
	setupSearchPaths();
	discoverPlugins();
}


PluginFactory::~PluginFactory() = default;


void PluginFactory::setupSearchPaths()
{
	// Adds a search path relative to the main executable if the path exists.
	auto addRelativeIfExists = [](const QString & path) {
		QDir dir(qApp->applicationDirPath());
		if (!path.isEmpty() && dir.cd(path)) {
			QDir::addSearchPath("plugins", dir.absolutePath());
		}
	};

	// We're either running Zene Studio installed on an Unixoid or we're running a
	// portable version like we do on Windows.
	// We want to find our plugins in both cases:
	//  (a) Installed (Unix):
	//      e.g. binary at /usr/bin/zene - plugin dir at /usr/lib/zene/
	//  (b) Portable:
	//      e.g. binary at "C:/Program Files/Zene Studio/zene.exe"
	//           plugins at "C:/Program Files/Zene Studio/plugins/"

#ifndef LMMS_BUILD_WIN32
	addRelativeIfExists("../lib/zene"); // Installed
#endif
	addRelativeIfExists("plugins"); // Portable
#ifdef PLUGIN_DIR // We may also have received a relative directory via a define
	addRelativeIfExists(PLUGIN_DIR);
#endif
	// Or via an environment variable:
	if (const char* env_path = std::getenv("LMMS_PLUGIN_DIR"))
		QDir::addSearchPath("plugins", env_path);

	QDir::addSearchPath("plugins", ConfigManager::inst()->workingDir() + "plugins");
}

PluginFactory* PluginFactory::instance()
{
	if (s_instance == nullptr)
		s_instance = std::make_unique<PluginFactory>();

	return s_instance.get();
}

PluginFactory* getPluginFactory()
{
	return PluginFactory::instance();
}

Plugin::DescriptorList PluginFactory::descriptors() const
{
	return m_descriptors.values();
}

Plugin::DescriptorList PluginFactory::descriptors(Plugin::Type type) const
{
	return m_descriptors.values(type);
}

const PluginFactory::PluginInfoList& PluginFactory::pluginInfos() const
{
	return m_pluginInfos;
}

PluginFactory::PluginInfoAndKey PluginFactory::pluginSupportingExtension(const QString& ext)
{
	return m_pluginByExt.value(ext, PluginInfoAndKey());
}

PluginFactory::PluginInfo PluginFactory::pluginInfo(const char* name) const
{
	for (const PluginInfo& info : m_pluginInfos)
	{
		if (qstrcmp(info.descriptor->name, name) == 0)
			return info;
	}
	return PluginInfo();
}

QString PluginFactory::errorString(QString pluginName) const
{
	static QString notfound = qApp->translate("PluginFactory", "Plugin not found.");
	return m_errors.value(pluginName, notfound);
}

QString PluginFactory::scanReport() const
{
	QString report = QStringLiteral("plugin-scan: %1 file(s), %2 quarantined, %3 served from cache, "
					"%4 known-bad skipped, %5 scanned, %6 plugin(s)")
					 .arg(m_scanStats.candidateFiles)
					 .arg(m_scanStats.quarantined)
					 .arg(m_scanStats.servedFromCache)
					 .arg(m_scanStats.negativeFromCache)
					 .arg(m_scanStats.scanned)
					 .arg(m_scanStats.descriptors);
	if (!m_scanStats.quarantinedPaths.isEmpty())
	{
		report += QStringLiteral("; skipped");
		for (int i = 0; i < m_scanStats.quarantinedPaths.size(); ++i)
		{
			report += QStringLiteral(" %1 (%2)")
						  .arg(m_scanStats.quarantinedPaths[i],
							  m_scanStats.quarantinedReasons[i].isEmpty()
								  ? QStringLiteral("quarantined")
								  : m_scanStats.quarantinedReasons[i]);
		}
	}
	return report;
}

/*!
 * Rebuild the descriptor of a cached plugin.
 *
 * Only the fields a descriptor exposes to the browser and the menus are
 * rebuilt - name, display names, version, type, supported file types and the
 * pixmap name of the logo. The library itself is not loaded: the PluginInfo
 * carries a QLibrary that has been pointed at the file but not loaded, and
 * QLibrary::resolve() loads it on demand, which is exactly what
 * Plugin::instantiate() does. SubPluginFeatures and compiled-in logo XPMs are
 * excluded before we get here (see cacheRecordServes()).
 */
Plugin::Descriptor* PluginFactory::descriptorFromCacheRecord(const PluginScanRecord& record,
	CachedDescriptorStore& store)
{
	auto entry = std::make_unique<CachedDescriptorStore::Entry>();
	entry->strings.reserve(5);
	entry->strings.push_back(record.name.toStdString());
	entry->strings.push_back(record.displayName.toStdString());
	entry->strings.push_back(record.description.toStdString());
	entry->strings.push_back(record.author.toStdString());
	entry->strings.push_back(record.supportedFileTypes.toStdString());

	Plugin::Descriptor& descriptor = entry->descriptor;
	descriptor.name = entry->strings[0].c_str();
	descriptor.displayName = entry->strings[1].c_str();
	descriptor.description = entry->strings[2].c_str();
	descriptor.author = entry->strings[3].c_str();
	descriptor.supportedFileTypes = record.supportedFileTypes.isEmpty() ? nullptr : entry->strings[4].c_str();
	descriptor.version = record.version;
	descriptor.type = static_cast<Plugin::Type>(record.type);
	if (!record.logoName.isEmpty())
	{
		entry->logo = std::make_shared<PixmapLoader>(record.logoName.toStdString());
		descriptor.logo = entry->logo.get();
	}
	descriptor.subPluginFeatures = nullptr;

	store.entries.push_back(std::move(entry));
	return &store.entries.back()->descriptor;
}

PluginFactory::ScanSink PluginFactory::backgroundScanSink()
{
	return ScanSink{&m_backgroundInfos, &m_backgroundDescriptors, &m_backgroundByExt,
		&m_backgroundErrors, &m_scanCache, m_backgroundStore.get()};
}

void PluginFactory::discoverPlugins()
{
	DescriptorMap descriptors;
	PluginInfoList pluginInfos;
	m_pluginByExt.clear();
	m_scanStats = ScanStats();
	m_retiredCachedDescriptors.push_back(std::move(m_cachedDescriptors));
	m_cachedDescriptors = std::make_unique<CachedDescriptorStore>();
	// The locals become the live catalogue at the end; the extensions, errors
	// and cache records land live as they are taken, exactly as before.
	ScanSink sink{&pluginInfos, &descriptors, &m_pluginByExt, &m_errors,
		&m_scanCache, m_cachedDescriptors.get()};

	// Re-read the cache on every scan, so an edited quarantine list takes
	// effect without a restart. A missing, unreadable or corrupt file is not an
	// error: load() reports it and this scan simply repeats all the work.
	m_scanCache.load();

	QSet<QFileInfo> files = candidatePluginFiles();
	m_scanStats.candidateFiles = files.size();
	dropQuarantinedPlugins(files);

	QHash<QString, ScanPlan> plans;
	QSet<QFileInfo> toLoad;
	planPluginScan(files, m_scanCache, plans, toLoad, m_scanStats, m_errors);

	preloadPluginLibraries(toLoad);

	for (const QFileInfo& file : files)
	{
		const ScanPlan plan = plans.value(file.absoluteFilePath(), ScanPlan::Load);
		if (plan == ScanPlan::Serve)
		{
			const PluginScanRecord* record = m_scanCache.lookup(file);
			if (record == nullptr) { continue; } // cannot happen: plans were just built
			appendCacheServedPlugin(*record, file, sink);
			continue;
		}
		if (plan == ScanPlan::Skip)
		{
			// Remembered as "no plugin" or "fails to load": nothing to do. A
			// changed file no longer matches its fingerprint and is scanned
			// again (PluginScanCache::lookup()).
			continue;
		}

		scanOnePlugin(file, sink);
	}

	m_pluginInfos = pluginInfos;
	m_descriptors = descriptors;
	m_scanStats.descriptors = pluginInfos.size();

	if (m_scanCache.isPersistent() && m_scanCache.isDirty())
	{
		m_scanCache.save();
	}

	qInfo().noquote() << scanReport();
}

/*!
 * The candidates: one entry per file in the plugin search paths, after the
 * LMMS_EXCLUDE_PLUGINS filter. Split out of discoverPlugins() so the search
 * paths, the name filters and the env filter are one readable step.
 */
QSet<QFileInfo> PluginFactory::candidatePluginFiles() const
{
	QSet<QFileInfo> files;
	for (const QString& searchPath : QDir::searchPaths("plugins"))
	{
		auto discoveredPluginList = QDir(searchPath).entryInfoList(nameFilters);
		files.unite(QSet<QFileInfo>(discoveredPluginList.begin(), discoveredPluginList.end()));
	}

	// Apply any plugin filters from environment LMMS_EXCLUDE_PLUGINS
	filterPlugins(files);
	return files;
}

/*!
 * The quarantine list is the user's "hide this plugin, permanently": it wins
 * over everything the environment says, and the scan reports what it skipped
 * and why (ScanStats, scanReport()).
 */
void PluginFactory::dropQuarantinedPlugins(QSet<QFileInfo>& files)
{
	QSet<QFileInfo> hidden;
	for (const QFileInfo& file : files)
	{
		if (m_scanCache.isQuarantined(file.absoluteFilePath())) { hidden.insert(file); }
	}
	for (const QFileInfo& file : hidden)
	{
		files.remove(file);
		m_scanStats.quarantined++;
		m_scanStats.quarantinedPaths.append(file.absoluteFilePath());
		m_scanStats.quarantinedReasons.append(m_scanCache.quarantineReason(file.absoluteFilePath()));
	}
}

/*!
 * What a descriptor claims to open, remembered per extension. Shared by the
 * two paths that append a plugin (cache-served and freshly loaded) so both
 * register the same way, including the sub-plugin keys of a
 * SubPluginFeatures descriptor.
 */
void PluginFactory::addSupportedFileTypes(const QString& supportedFileTypes,
	const PluginInfo& info, QMap<QString, PluginInfoAndKey>& byExt,
	const Plugin::Descriptor::SubPluginFeatures::Key* key)
{
	if(!supportedFileTypes.isNull())
	{
		for (const QString& ext : supportedFileTypes.split(','))
		{
			//qDebug() << "Plugin " << info.name()
			//	<< "supports" << ext;
			PluginInfoAndKey infoAndKey;
			infoAndKey.info = info;
			infoAndKey.key = key
				? *key
				: Plugin::Descriptor::SubPluginFeatures::Key();
			byExt.insert(ext, infoAndKey);
		}
	}
}

/*!
 * Turn one candidate into a PluginInfo, walking exactly the path the old
 * inline loop did: dlopen, resolve lmms_plugin_main, resolve the named
 * descriptor, and remember the outcome - the fingerprint-keyed record - for
 * every one of the three outcomes so the next scan does not repeat the work.
 */
PluginScanRecord::Status PluginFactory::scanOnePlugin(const QFileInfo& file, ScanSink& sink)
{
	const PluginLoadOutcome outcome = loadPluginDescriptor(file);

	if (!outcome.loaded)
	{
		PluginScanRecord record = fingerprintRecord(file);
		record.status = PluginScanRecord::Status::LoadFailed;
		record.error = outcome.error;
		sink.cache->store(record);
		sink.errors->insert(file.baseName(), outcome.error);
		qWarning("%s", outcome.error.toLocal8Bit().data());
		return PluginScanRecord::Status::LoadFailed;
	}

	if (outcome.descriptor == nullptr)
	{
		if (!outcome.missingDescriptorWarning.isEmpty())
		{
			qWarning() << outcome.missingDescriptorWarning;
		}
		// Loads fine but is not an LMMS plugin (helper libraries live in
		// the same folders): remember it so the next scan does not even
		// open it.
		sink.cache->store(fingerprintRecord(file));
		return PluginScanRecord::Status::NotAPlugin;
	}

	appendLoadedPlugin(file, outcome.library, outcome.descriptor, sink);
	return PluginScanRecord::Status::HasDescriptor;
}

//! Append a plugin this run actually loaded, and remember its descriptor.
void PluginFactory::appendLoadedPlugin(const QFileInfo& file,
	const std::shared_ptr<QLibrary>& library, Plugin::Descriptor* descriptor,
	ScanSink& sink)
{
	PluginInfo info;
	info.file = file;
	info.library = library;
	info.descriptor = descriptor;
	sink.infos->append(info);

	if (info.descriptor->supportedFileTypes)
	{
		addSupportedFileTypes(QString(info.descriptor->supportedFileTypes), info, *sink.byExt);
	}

	if (info.descriptor->subPluginFeatures)
	{
		Plugin::Descriptor::SubPluginFeatures::KeyList subPluginKeys;
		info.descriptor->subPluginFeatures->listSubPluginKeys(info.descriptor, subPluginKeys);
		for (const Plugin::Descriptor::SubPluginFeatures::Key& key : subPluginKeys)
		{
			addSupportedFileTypes(key.additionalFileExtensions(), info, *sink.byExt, &key);
		}
	}

	sink.descriptors->insert(info.descriptor->type, info.descriptor);
	sink.cache->store(recordFromDescriptor(file, *info.descriptor));
}

/*!
 * Append a plugin answered from the cache: no dlopen, no symbol resolution.
 * The library is loaded when the plugin is first used (QLibrary::resolve()).
 */
void PluginFactory::appendCacheServedPlugin(const PluginScanRecord& record, const QFileInfo& file,
	ScanSink& sink)
{
	PluginInfo info;
	info.file = file;
	info.library = std::make_shared<QLibrary>(file.absoluteFilePath());
	info.descriptor = descriptorFromCacheRecord(record, *sink.cacheStore);
	sink.infos->append(info);

	addSupportedFileTypes(QString(info.descriptor->supportedFileTypes), info, *sink.byExt);
	sink.descriptors->insert(info.descriptor->type, info.descriptor);
}

// Builds QList<QRegularExpression> based on environment variable envVar
QList<QRegularExpression> PluginFactory::getExcludePatterns(const char* envVar) {
	QList<QRegularExpression> excludePatterns;
	QString excludePatternString = std::getenv(envVar);

	if (!excludePatternString.isEmpty()) {
		QStringList patterns = excludePatternString.split(',');
		for (const QString& pattern : patterns) {
			if (pattern.trimmed().isEmpty()) {
				continue;
			}
			QRegularExpression regex(pattern.trimmed());
			if (regex.isValid()) {
				excludePatterns << regex;
			} else {
				qWarning() << "Invalid regular expression:" << pattern;
			}
		}
	}
	return excludePatterns;
}

// Filter plugins based on environment variable, e.g. export LMMS_EXCLUDE_PLUGINS="libcarla"
void PluginFactory::filterPlugins(QSet<QFileInfo>& files) {
	// Get filter
	QList<QRegularExpression> excludePatterns = getExcludePatterns("LMMS_EXCLUDE_PLUGINS");
	if (excludePatterns.isEmpty()) {
		return;
	}

  	// Get files to remove
	QSet<QFileInfo> filesToRemove;
	for (const QFileInfo& fileInfo : files) {
		bool exclude = false;
		QString filePath = fileInfo.filePath();

		for (const QRegularExpression& pattern : excludePatterns) {
			if (pattern.match(filePath).hasMatch()) {
				exclude = true;
				break;
			}
		}

		if (exclude) {
			filesToRemove.insert(fileInfo);
		}
	}

	// Remove them
	for (const QFileInfo& fileInfo : filesToRemove) {
		files.remove(fileInfo);
	}
}

QString PluginFactory::PluginInfo::name() const
{
	return descriptor ? descriptor->name : QString();
}


} // namespace lmms
