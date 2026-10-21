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
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include "PluginScanCache.h"
#include "lmms_export.h"
#include "Plugin.h"
#include "TaskCenter.h"

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

	// --- background rescan (Q050-090) ------------------------------------
	// A rescan that does not block the window: startBackgroundScan() marks the
	// run active, shows it in the task area and returns BEFORE touching a
	// directory or a library, and the planning (candidate files, quarantine,
	// the per-file plan) plus the dependency pre-load then run as scheduled
	// cancellable chunks - one event-loop turn each, one library load per
	// pre-load turn, ONE file per scan turn. The interface - and plugin.list,
	// which keeps serving the PREVIOUS catalogue until the run commits - stays
	// usable throughout, including while the libraries load. plugin.scan_status
	// reads the progress, plugin.scan_cancel stops the run and keeps the
	// previous catalogue and cache. Everything here is main-thread confined:
	// control handlers run on the UI thread (ControlRegistry::runOnUiThread)
	// and the chunks are QTimer turns on the same thread, so no member here
	// needs a lock. Nothing here touches the audio thread.

	//! One file a background run has finished, as plugin.scan_status reports it.
	struct BackgroundScanFile
	{
		QString path;
		//! served-from-cache | scanned | not-a-plugin | load-failed
		QString status;
		QString name;
		QString error;
	};

	//! The background run's state, as plugin.scan_status reports it.
	struct BackgroundScanStatus
	{
		bool active = false;
		bool cancelRequested = false;
		//! never | running | done | cancelled
		QString outcome = QStringLiteral("never");
		int totalFiles = 0;
		int doneFiles = 0;
		QString currentFile;
		QList<BackgroundScanFile> files;
		int taskId = -1;
		//! The devices plugin.list serves (the previous run's catalogue while
		//! a run is active): the proof the window keeps working.
		int catalogueCount = 0;
		QString report;
	};

	bool backgroundScanActive() const { return m_background.active; }
	BackgroundScanStatus backgroundScanStatus() const { return m_background; }

	//! Begin a background run over the current candidate files. Marks the run
	//! active, shows it in the task area and returns: the planning (candidate
	//! files, the per-file plan) and every library load run as scheduled
	//! cancellable chunks AFTER this returns, so the task, the active status
	//! and plugin.scan_cancel exist before the first potentially slow load.
	//! @a paceMs is the delay between turns (0 = the next event-loop turn):
	//! the pacing a progress display - and a cancel test - observes. The
	//! scan_start reply carries total_files 0: the count is planned on the
	//! run's first turn and read through plugin.scan_status. False and *error
	//! set when a run is already active.
	bool startBackgroundScan(int paceMs, QString* error);
	//! Ask the active run to stop at its next file boundary; the previous
	//! catalogue and cache are kept. False and *error set when no run is active.
	bool requestBackgroundScanCancel(QString* error);

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

	//! The active run's state (see startBackgroundScan): main-thread only.
	BackgroundScanStatus m_background;
	//! Whether the active run has planned yet (see prepareBackgroundScan):
	//! scan_start returns before any directory or library work, so the task
	//! and the active status exist before the first potentially slow load.
	bool m_backgroundPrepared = false;
	//! Files the active run still has to pre-load, in scan order, one library
	//! per turn (see preloadPluginLibraries): the zynaddsubfx dependency
	//! lands before any resolve needs it, and a cancel between two loads
	//! stops the run before the next one.
	QList<QFileInfo> m_backgroundPreloadRemaining;
	//! Files the active run still has to look at, in scan order.
	QList<QFileInfo> m_backgroundRemaining;
	//! What the active run has built so far; committed to m_pluginInfos /
	//! m_descriptors / m_scanStats only when the run finishes uncancelled.
	//! The cache is the exception: records are true measurements, so they are
	//! stored live as they are taken (the Modules list shows them mid-run) and
	//! a cancelled run's measurements stay cached for the next run to serve.
	PluginInfoList m_backgroundInfos;
	DescriptorMap m_backgroundDescriptors;
	QMap<QString, PluginInfoAndKey> m_backgroundByExt;
	QHash<QString, QString> m_backgroundErrors;
	ScanStats m_backgroundStats;
	int m_backgroundPaceMs = 0;
	int m_backgroundTaskLocal = 0;
	//! The token of the open background-scan task (TaskCenter reports from one generation need it).
	TaskToken m_backgroundTaskToken;

	//! One file of the active run, then the next turn - or the commit when the
	//! queue is empty. Main thread only; re-scheduled through the TaskCenter
	//! (a QObject that outlives the factory) so no timer of its own is needed.
	void stepBackgroundScan();
	//! The run's first turn: list the candidates, drop the quarantine list and
	//! count the per-file plan into the staging stats, queuing the libraries
	//! to pre-load and the files to scan. No dlopen: every load waits for its
	//! own turn (see preloadBackgroundLibrary), so this turn stays cheap and
	//! a cancel requested before it still stops the run with nothing done.
	void prepareBackgroundScan();
	//! One library of the active run's dependency pre-load (see
	//! preloadPluginLibraries): exactly one dlopen, then the next turn.
	void preloadBackgroundLibrary();
	//! One file of the active run, decided and scanned exactly as the
	//! synchronous loop does (see discoverPlugins), into the staging above -
	//! then the next turn, or the commit when the queue is empty.
	void scanNextBackgroundFile();
	//! Schedule the run's next turn after @a paceMs. Main thread only.
	void scheduleBackgroundScanStep();
	//! One file of the active run, decided and scanned exactly as the
	//! synchronous loop does (see discoverPlugins), into the staging above.
	void scanBackgroundFile(const QFileInfo& file);
	//! The run ended (finished or cancelled): commit or discard the staging and
	//! close the task. Main thread only.
	void endBackgroundScan(bool cancelled);
	//! Throw the staging away (see endBackgroundScan's cancelled half).
	void discardBackgroundStaging();
	//! The one-line summary endBackgroundScan reports, in scanReport()'s shape.
	QString backgroundScanReport() const;

	/*!
	 * Host-owned copies of the descriptors rebuilt from cache records: the
	 * strings and the pixmap loader such a descriptor points at live here, so
	 * the rebuilt descriptor stays valid after the cache is gone.
	 */
	class CachedDescriptorStore
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
	// Descriptor pointers escape into browser rows, so each retired store stays
	// owned until the factory is destroyed.
	std::vector<std::unique_ptr<CachedDescriptorStore>> m_retiredCachedDescriptors;
	std::unique_ptr<CachedDescriptorStore> m_cachedDescriptors;
	//! The active run's rebuilt descriptors; swapped in at commit, discarded on
	//! cancel (with the staging infos that point into it).
	std::unique_ptr<CachedDescriptorStore> m_backgroundStore;

	//! Where one scan run assembles its results: the live members for a
	//! synchronous run, the m_background* staging for a background run (whose
	//! cache is the live one - see m_backgroundInfos).
	struct ScanSink
	{
		PluginInfoList* infos;
		DescriptorMap* descriptors;
		QMap<QString, PluginInfoAndKey>* byExt;
		QHash<QString, QString>* errors;
		PluginScanCache* cache;
		CachedDescriptorStore* cacheStore;
	};

	//! The active background run's sink: infos, descriptors, extensions and
	//! errors stage, the cache stores live (see m_backgroundInfos).
	ScanSink backgroundScanSink();

	//! Rebuild a descriptor from a cache record into @a store.
	Plugin::Descriptor* descriptorFromCacheRecord(const PluginScanRecord& record,
		CachedDescriptorStore& store);

	/*!
	 * Plugin discovery, split into its stages so that no single method carries
	 * the whole scan (per-method CCN target: tests/complexity-gate.sh). Each
	 * stage below is one of the steps discoverPlugins() used to inline; the
	 * sequence, the order of the decisions and the observable behaviour are
	 * unchanged - only the function boundaries moved.
	 */
	QSet<QFileInfo> candidatePluginFiles() const;
	void dropQuarantinedPlugins(QSet<QFileInfo>& files);
	//! Scan one candidate, storing its record through @a sink. Returns the
	//! stored status, so a background chunk can report the per-file outcome.
	PluginScanRecord::Status scanOnePlugin(const QFileInfo& file, ScanSink& sink);
	void appendLoadedPlugin(const QFileInfo& file, const std::shared_ptr<QLibrary>& library,
		Plugin::Descriptor* descriptor, ScanSink& sink);
	void appendCacheServedPlugin(const PluginScanRecord& record, const QFileInfo& file,
		ScanSink& sink);
	void addSupportedFileTypes(const QString& supportedFileTypes, const PluginInfo& info,
		QMap<QString, PluginInfoAndKey>& byExt,
		const Plugin::Descriptor::SubPluginFeatures::Key* key = nullptr);

	static std::unique_ptr<PluginFactory> s_instance;

	static void filterPlugins(QSet<QFileInfo>& files);
};

//Short-hand function
LMMS_EXPORT PluginFactory* getPluginFactory();


} // namespace lmms

#endif // LMMS_PLUGIN_FACTORY_H
