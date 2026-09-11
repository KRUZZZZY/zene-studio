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
#include <QDebug>
#include <QDir>
#include <QLibrary>
#include <QRegularExpression>
#include <memory>
#include "lmmsconfig.h"

#include "ConfigManager.h"
#include "embed.h"
#include "Plugin.h"

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

/*!
 * Host-owned copies of the descriptors rebuilt from cache records.
 *
 * A rebuilt descriptor must stay valid for as long as anyone can hold a
 * pointer to it, so its strings and its pixmap loader live here, owned by the
 * factory, and are only released when the next scan replaces the whole set
 * (the same lifetime rule the live path has: a scan replaces m_pluginInfos).
 */
class PluginFactory::CachedDescriptorStore
{
public:
	struct Entry
	{
		std::vector<std::string> strings;
		std::shared_ptr<PixmapLoader> logo;
		Plugin::Descriptor descriptor{};
	};

	std::vector<std::unique_ptr<Entry>> entries;
};

namespace
{

QString fromUtf8OrEmpty(const char* text)
{
	return text ? QString::fromUtf8(text) : QString();
}

/*!
 * Can this remembered record stand in for the live descriptor?
 *
 * Two shapes cannot be rebuilt from JSON and must always take the loading
 * path: a descriptor whose sub-plugin enumeration is a virtual call into the
 * library (SubPluginFeatures), and a descriptor whose logo is a compiled-in
 * XPM (there is no name to look up). Everything else - the native LMMS
 * plugins - is plain data: strings, a version, a type, a pixmap name.
 */
bool cacheRecordServes(const PluginScanRecord& record)
{
	if (record.status != PluginScanRecord::Status::HasDescriptor) { return false; }
	if (record.hasSubPluginFeatures) { return false; }
	if (record.logoHasInlinePixmap) { return false; }
	return true;
}

//! What one candidate file needs from this scan.
enum class ScanPlan
{
	Load, //!< dlopen it and resolve the descriptor: new, changed, or not rebuildable
	Serve, //!< answer from the cache; the library stays closed until first use
	Skip, //!< remembered as "no plugin"/"does not load": leave the file alone
};

//! The plugin's own descriptor, remembered from the last time it was loaded.
PluginScanRecord recordFromDescriptor(const QFileInfo& file, const Plugin::Descriptor& descriptor)
{
	PluginScanRecord record;
	record.filePath = file.absoluteFilePath();
	record.size = file.size();
	record.mtimeMs = file.lastModified().toMSecsSinceEpoch();
	record.status = PluginScanRecord::Status::HasDescriptor;
	record.name = fromUtf8OrEmpty(descriptor.name);
	record.displayName = fromUtf8OrEmpty(descriptor.displayName);
	record.description = fromUtf8OrEmpty(descriptor.description);
	record.author = fromUtf8OrEmpty(descriptor.author);
	record.supportedFileTypes = fromUtf8OrEmpty(descriptor.supportedFileTypes);
	record.version = descriptor.version;
	record.type = static_cast<int>(descriptor.type);
	if (descriptor.logo)
	{
		record.logoName = QString::fromStdString(descriptor.logo->pixmapName());
		record.logoHasInlinePixmap = descriptor.logo->xpm() != nullptr;
	}
	record.hasSubPluginFeatures = descriptor.subPluginFeatures != nullptr;
	return record;
}

} // namespace


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
Plugin::Descriptor* PluginFactory::descriptorFromCacheRecord(const PluginScanRecord& record)
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

	m_cachedDescriptors->entries.push_back(std::move(entry));
	return &m_cachedDescriptors->entries.back()->descriptor;
}

void PluginFactory::discoverPlugins()
{
	DescriptorMap descriptors;
	PluginInfoList pluginInfos;
	m_pluginByExt.clear();
	m_scanStats = ScanStats();
	m_cachedDescriptors = std::make_unique<CachedDescriptorStore>();

	// Re-read the cache on every scan, so an edited quarantine list takes
	// effect without a restart. A missing, unreadable or corrupt file is not an
	// error: load() reports it and this scan simply repeats all the work.
	m_scanCache.load();

	QSet<QFileInfo> files;
	for (const QString& searchPath : QDir::searchPaths("plugins"))
	{
		auto discoveredPluginList = QDir(searchPath).entryInfoList(nameFilters);
		files.unite(QSet<QFileInfo>(discoveredPluginList.begin(), discoveredPluginList.end()));
	}

	// Apply any plugin filters from environment LMMS_EXCLUDE_PLUGINS
	filterPlugins(files);

	m_scanStats.candidateFiles = files.size();

	// The quarantine list is the user's "hide this plugin, permanently": it
	// wins over everything the environment says, and the scan reports what it
	// skipped and why (ScanStats, scanReport()).
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

	// Decide what this run has to do for every file that is still a candidate:
	// load it (new, changed, or not rebuildable from the cache), serve it from
	// the cache, or leave it alone because it was remembered as containing no
	// plugin at all.
	const auto scanPlan = [this](const QFileInfo& file) {
		const PluginScanRecord* record = m_scanCache.lookup(file);
		if (record == nullptr) { return ScanPlan::Load; }
		if (record->status != PluginScanRecord::Status::HasDescriptor) { return ScanPlan::Skip; }
		return cacheRecordServes(*record) ? ScanPlan::Serve : ScanPlan::Load;
	};

	QHash<QString, ScanPlan> plans;
	QSet<QFileInfo> toLoad;
	for (const QFileInfo& file : files)
	{
		const ScanPlan plan = scanPlan(file);
		plans.insert(file.absoluteFilePath(), plan);
		switch (plan)
		{
		case ScanPlan::Serve:
			m_scanStats.servedFromCache++;
			break;
		case ScanPlan::Skip:
			m_scanStats.negativeFromCache++;
			if (const PluginScanRecord* record = m_scanCache.lookup(file);
				record != nullptr && record->status == PluginScanRecord::Status::LoadFailed)
			{
				// Same error text the failed load produced last time, so
				// Plugin::instantiate() still explains itself.
				m_errors[file.baseName()] = record->error;
			}
			break;
		case ScanPlan::Load:
			m_scanStats.scanned++;
			toLoad.insert(file);
			break;
		}
	}

	// Cheap dependency handling: zynaddsubfx needs ZynAddSubFxCore. By loading
	// all libraries twice we ensure that libZynAddSubFxCore is found. Only the
	// files this run actually loads take part; a cache-served plugin is loaded
	// on demand, when it is first instantiated.
	for (const QFileInfo& file : toLoad)
	{
		QLibrary(file.absoluteFilePath()).load();
	}

	auto addSupportedFileTypes =
		[this](QString supportedFileTypes,
			const PluginInfo& info,
			const Plugin::Descriptor::SubPluginFeatures::Key* key = nullptr)
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
				m_pluginByExt.insert(ext, infoAndKey);
			}
		}
	};

	for (const QFileInfo& file : files)
	{
		const ScanPlan plan = plans.value(file.absoluteFilePath(), ScanPlan::Load);
		if (plan == ScanPlan::Serve)
		{
			const PluginScanRecord* record = m_scanCache.lookup(file);
			if (record == nullptr) { continue; } // cannot happen: plans were just built

			// Cache hit: no dlopen, no symbol resolution. The library is
			// loaded when the plugin is first used (QLibrary::resolve()).
			PluginInfo info;
			info.file = file;
			info.library = std::make_shared<QLibrary>(file.absoluteFilePath());
			info.descriptor = descriptorFromCacheRecord(*record);
			pluginInfos << info;

			addSupportedFileTypes(QString(info.descriptor->supportedFileTypes), info);
			descriptors.insert(info.descriptor->type, info.descriptor);
			continue;
		}
		if (plan == ScanPlan::Skip)
		{
			// Remembered as "no plugin" or "fails to load": nothing to do. A
			// changed file no longer matches its fingerprint and is scanned
			// again (PluginScanCache::lookup()).
			continue;
		}

		PluginScanRecord record;
		record.filePath = file.absoluteFilePath();
		record.size = file.size();
		record.mtimeMs = file.lastModified().toMSecsSinceEpoch();

		auto library = std::make_shared<QLibrary>(file.absoluteFilePath());
		if (! library->load()) {
			record.status = PluginScanRecord::Status::LoadFailed;
			record.error = library->errorString();
			m_scanCache.store(record);
			m_errors[file.baseName()] = library->errorString();
			qWarning("%s", library->errorString().toLocal8Bit().data());
			continue;
		}

		Plugin::Descriptor* pluginDescriptor = nullptr;
		if (library->resolve("lmms_plugin_main"))
		{
			QString descriptorName = file.baseName() + "_plugin_descriptor";
			if( descriptorName.left(3) == "lib" )
			{
				descriptorName = descriptorName.mid(3);
			}

			pluginDescriptor = reinterpret_cast<Plugin::Descriptor*>(library->resolve(descriptorName.toUtf8().constData()));
			if(pluginDescriptor == nullptr)
			{
				qWarning() << qApp->translate("PluginFactory", "Zene Studio plugin %1 does not have a plugin descriptor named %2!").
							  arg(file.absoluteFilePath()).arg(descriptorName);
				m_scanCache.store(record);
				continue;
			}
		}

		if(pluginDescriptor)
		{
			PluginInfo info;
			info.file = file;
			info.library = library;
			info.descriptor = pluginDescriptor;
			pluginInfos << info;

			if (info.descriptor->supportedFileTypes)
				addSupportedFileTypes(QString(info.descriptor->supportedFileTypes), info);

			if (info.descriptor->subPluginFeatures)
			{
				Plugin::Descriptor::SubPluginFeatures::KeyList
					subPluginKeys;
				info.descriptor->subPluginFeatures->listSubPluginKeys(
					info.descriptor,
					subPluginKeys);
				for(const Plugin::Descriptor::SubPluginFeatures::Key& key
					: subPluginKeys)
				{
					addSupportedFileTypes(key.additionalFileExtensions(), info, &key);
				}
			}

			descriptors.insert(info.descriptor->type, info.descriptor);
			m_scanCache.store(recordFromDescriptor(file, *info.descriptor));
		}
		else
		{
			// Loads fine but is not an LMMS plugin (helper libraries live in
			// the same folders): remember it so the next scan does not even
			// open it.
			m_scanCache.store(record);
		}
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
