/*
 * PluginFactoryScanDetail.h - the plugin scan's per-file steps, shared by the
 *                             synchronous and the background runs.
 *
 * Q050-090 moved this block out of PluginFactory.cpp unchanged: the
 * synchronous discoverPlugins() loop and the background chunk
 * (PluginFactoryBackgroundScan.cpp) must decide, load and remember each file
 * in exactly the same way, or the two runs would churn the cache against each
 * other. The anonymous namespace gives each including translation unit its own
 * copies; exactly two include it (PluginFactory.cpp,
 * PluginFactoryBackgroundScan.cpp).
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

#ifndef LMMS_PLUGIN_FACTORY_SCAN_DETAIL_H
#define LMMS_PLUGIN_FACTORY_SCAN_DETAIL_H

#include <memory>
#include <string>

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QLibrary>
#include <QSet>
#include <QString>

#include "Plugin.h"
#include "PluginFactory.h"
#include "PluginScanCache.h"
#include "SafeStart.h"
#include "embed.h"

// QT qHash specialization for plugin files, defined in PluginFactory.cpp:
// QSet<QFileInfo> needs it wherever a scan TU builds one.
qint64 qHash(const QFileInfo& fi);

namespace lmms
{

namespace
{

// The block below moved out of PluginFactory.cpp unchanged (Q050-090): `inline`
// is what lets a header carry it - each including translation unit compiles
// its own copies, and no unused-function warning fires for the half one of
// them does not call.

inline QString fromUtf8OrEmpty(const char* text)
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
inline bool cacheRecordServes(const PluginScanRecord& record)
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
inline PluginScanRecord recordFromDescriptor(const QFileInfo& file, const Plugin::Descriptor& descriptor)
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

/*!
 * The fingerprint fields a remembered record is only trusted with: path, size
 * and mtime. PluginScanCache::lookup() serves a record only while all three
 * still match, so a record built here is a valid "this file was looked at"
 * marker (not a plugin, or a failed load) and is rebuilt by the next scan.
 */
inline PluginScanRecord fingerprintRecord(const QFileInfo& file)
{
	PluginScanRecord record;
	record.filePath = file.absoluteFilePath();
	record.size = file.size();
	record.mtimeMs = file.lastModified().toMSecsSinceEpoch();
	return record;
}

//! What one attempted dlopen produced.
struct PluginLoadOutcome
{
	std::shared_ptr<QLibrary> library;        //!< the candidate library, loaded or not
	Plugin::Descriptor* descriptor = nullptr; //!< set only when one resolved
	bool loaded = false;                      //!< QLibrary::load() succeeded
	QString error;                            //!< QLibrary::errorString() when !loaded
	QString missingDescriptorWarning;         //!< set when the file loads but names no descriptor
};

/*!
 * Load one candidate and try to resolve the descriptor out of it.
 *
 * The three outcomes the caller distinguishes are: the library did not load
 * (error holds QLibrary::errorString()); it loaded but exposes no descriptor
 * (descriptor is null - with missingDescriptorWarning set when the file did
 * export lmms_plugin_main and the named symbol was missing, which is a real
 * error worth a warning, versus a helper library that is simply not a plugin);
 * or the descriptor resolved.
 */
inline PluginLoadOutcome loadPluginDescriptor(const QFileInfo& file)
{
	PluginLoadOutcome outcome;
	// Name this file in the safe-start marker for the length of the dlopen: a
	// module whose static initialiser kills the process is then identified by
	// the next launch (Q050-049). Cheap, and only third-party files are written.
	const lmms::safestart::ActivePluginScope activePlugin("scan", std::string(),
		file.absoluteFilePath().toStdString(), "lmms-module");
	outcome.library = std::make_shared<QLibrary>(file.absoluteFilePath());
	if (!outcome.library->load())
	{
		outcome.error = outcome.library->errorString();
		return outcome;
	}
	outcome.loaded = true;

	if (!outcome.library->resolve("lmms_plugin_main")) { return outcome; }

	QString descriptorName = file.baseName() + "_plugin_descriptor";
	if (descriptorName.left(3) == "lib")
	{
		descriptorName = descriptorName.mid(3);
	}

	outcome.descriptor = reinterpret_cast<Plugin::Descriptor*>(
		outcome.library->resolve(descriptorName.toUtf8().constData()));
	if (outcome.descriptor == nullptr)
	{
		outcome.missingDescriptorWarning =
			qApp->translate("PluginFactory",
				"Zene Studio plugin %1 does not have a plugin descriptor named %2!")
				.arg(file.absoluteFilePath()).arg(descriptorName);
	}
	return outcome;
}

//! What the remembered record says this run should do with one candidate.
inline ScanPlan planForCandidate(const PluginScanRecord* record)
{
	if (record == nullptr) { return ScanPlan::Load; }
	if (record->status != PluginScanRecord::Status::HasDescriptor) { return ScanPlan::Skip; }
	return cacheRecordServes(*record) ? ScanPlan::Serve : ScanPlan::Load;
}

/*!
 * Decide what this run has to do for every file that is still a candidate, and
 * count the work each decision implies into the scan stats. Split from the
 * scan loop so the decision table and the loop that acts on it are read
 * separately.
 */
inline void planPluginScan(const QSet<QFileInfo>& files, const PluginScanCache& cache,
	QHash<QString, ScanPlan>& plans, QSet<QFileInfo>& toLoad,
	PluginFactory::ScanStats& stats, QHash<QString, QString>& errors)
{
	for (const QFileInfo& file : files)
	{
		const PluginScanRecord* record = cache.lookup(file);
		const ScanPlan plan = planForCandidate(record);
		plans.insert(file.absoluteFilePath(), plan);
		switch (plan)
		{
		case ScanPlan::Serve:
			stats.servedFromCache++;
			break;
		case ScanPlan::Skip:
			stats.negativeFromCache++;
			if (record != nullptr && record->status == PluginScanRecord::Status::LoadFailed)
			{
				// Same error text the failed load produced last time, so
				// Plugin::instantiate() still explains itself.
				errors[file.baseName()] = record->error;
			}
			break;
		case ScanPlan::Load:
			stats.scanned++;
			toLoad.insert(file);
			break;
		}
	}
}

/*!
 * Cheap dependency handling: zynaddsubfx needs ZynAddSubFxCore, and loading
 * every library this run actually loads twice is what makes it resolvable.
 * A cache-served plugin takes no part - it is loaded on demand, when it is
 * first instantiated.
 */
inline void preloadPluginLibraries(const QSet<QFileInfo>& toLoad)
{
	for (const QFileInfo& file : toLoad)
	{
		QLibrary(file.absoluteFilePath()).load();
	}
}

} // namespace

} // namespace lmms

#endif // LMMS_PLUGIN_FACTORY_SCAN_DETAIL_H
