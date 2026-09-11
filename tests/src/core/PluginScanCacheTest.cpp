/*
 * PluginScanCacheTest.cpp
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

/*!
 * \file PluginScanCacheTest.cpp
 *
 * The scan cache and the quarantine list, driven through the real
 * PluginFactory against a real built plugin module (tripleoscillator), so the
 * enumeration path itself - file discovery, QLibrary::load(), symbol
 * resolution - is exercised, not just the JSON layer.
 *
 * Every count asserted here is taken after pinFixtureAndRescan(): the search
 * path is pinned to the fixture directory and the scan is re-run, so the
 * numbers cannot depend on what else lives in the machine's plugin
 * directories.
 *
 * What is covered:
 *   - cache round-trip through the on-disk JSON
 *   - a hit is only a hit for the same path + size + mtime
 *   - a warm cache serves the descriptor without loading the library, and the
 *     library still loads on first use (QLibrary::resolve)
 *   - a changed plugin file is re-scanned
 *   - a file remembered as "no plugin"/"does not load" is not opened again
 *   - the quarantine list hides a plugin from discovery and survives a reload
 *   - a corrupt / missing cache file degrades to a full scan, never to an
 *     empty plugin list
 *   - with no cache file at all, discovery finds exactly what a disabled
 *     cache finds
 *   - constructing the plugin browser does not scan; showing it does
 *
 * What is NOT covered (documented in docs/PLUGIN-SCAN-CACHE.md): third-party
 * plugins whose descriptors carry SubPluginFeatures or compiled-in logo XPMs
 * always take the loading path (no fixture for either here), and no test can
 * time out a plugin that hangs inside dlopen().
 *
 * Windows: the module-loading half is skipped, with the same reasoning as
 * ScriptEngineTest - a Windows test host cannot load a plugin MODULE library
 * (its import descriptor names lmms.exe).
 */

#include <QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "embed.h"
#include "Plugin.h"
#include "PluginBrowser.h"
#include "PluginFactory.h"
#include "PluginScanCache.h"

//! Build-tree plugin directory, injected by tests/CMakeLists.txt.
#ifndef LMMS_TEST_SCAN_PLUGIN_DIR
#define LMMS_TEST_SCAN_PLUGIN_DIR "plugins"
#endif

namespace
{

/*! The module file name the scanner looks for on this platform. */
constexpr auto moduleFileName() -> const char*
{
#ifdef LMMS_BUILD_WIN32
	return "tripleoscillator.dll";
#else
	return "libtripleoscillator.so";
#endif
}

constexpr auto testHostCanLoadPluginModules() -> bool
{
#if defined(Q_OS_WIN)
	return false;
#else
	return true;
#endif
}

/*! Descriptor metadata of every instrument plugin, for set comparisons. */
QStringList pluginFingerprints(lmms::PluginFactory& factory)
{
	QStringList out;
	for (const lmms::Plugin::Descriptor* desc : factory.descriptors(lmms::Plugin::Type::Instrument))
	{
		out << QStringLiteral("%1|%2|%3|%4|%5")
				   .arg(QString::fromUtf8(desc->name),
					   QString::fromUtf8(desc->displayName),
					   QString::fromUtf8(desc->author),
					   QString::number(desc->version),
					   QString::number(static_cast<int>(desc->type)));
	}
	out.sort();
	return out;
}

} // namespace


class PluginScanCacheTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		m_pluginDir.reset(new QTemporaryDir(QDir::tempPath() + "/zene-scan-plugins-XXXXXX"));
		QVERIFY2(m_pluginDir->isValid(), "cannot create the plugin directory fixture");
		m_cacheDir.reset(new QTemporaryDir(QDir::tempPath() + "/zene-scan-cache-XXXXXX"));
		QVERIFY2(m_cacheDir->isValid(), "cannot create the cache directory fixture");

		// The scanner only looks at lib*.so (LMMS_BUILD_WIN32: *.dll), so the real
		// module is copied in under its own, discoverable name.
		const QFileInfo module(QDir(QStringLiteral(LMMS_TEST_SCAN_PLUGIN_DIR)).filePath(QLatin1String(moduleFileName())));
		if (module.exists())
		{
			QVERIFY2(QFile::copy(module.absoluteFilePath(), pluginPath()),
				qPrintable(QStringLiteral("cannot copy %1 into the fixture").arg(module.absoluteFilePath())));
		}

		// LMMS_PLUGIN_DIR is deliberately NOT set here: every test pins the search
		// path to the fixture itself (pinFixtureAndRescan), so the factory
		// constructor's scan of the machine's default directories cannot pre-warm
		// the cache the assertions are about. The plugin browser test sets the
		// variable itself, because that is the code path it exercises.

		qInfo() << "fixture plugin dir:" << m_pluginDir->path() << "module copied:" << pluginModuleCopied();
	}
	void cleanupTestCase()
	{
		m_pluginDir.reset();
		m_cacheDir.reset();
	}

	/*! Runs before every test: clear a bad-file fixture a failed earlier slot
	    may have left behind (a failure aborts the slot, not the process). */
	void init()
	{
		// A failed earlier slot aborts before its own cleanup runs; make sure the
		// fixture directory holds exactly what the next slot expects.
		QFile::remove(m_pluginDir->filePath("libnotaplugin.so"));
		QFile::remove(m_pluginDir->filePath("liblazyprobe.so"));
	}

	// --- the cache/quarantine layer itself ---------------------------------
	void testCacheRoundTrip()
	{
		using namespace lmms;

		// A record of each kind: a plugin, and a file that refuses to load. The
		// "broken" file is written for real, so its fingerprint matches a real
		// QFileInfo (a lookup against a file that does not exist can never hit).
		const QString brokenPath = m_cacheDir->filePath("libbroken.so");
		{
			QFile file(brokenPath);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write("abc");
		}

		PluginScanRecord plugin;
		plugin.filePath = pluginPath();
		plugin.size = QFileInfo(pluginPath()).size();
		plugin.mtimeMs = QFileInfo(pluginPath()).lastModified().toMSecsSinceEpoch();
		plugin.status = PluginScanRecord::Status::HasDescriptor;
		plugin.name = "tripleoscillator";
		plugin.displayName = "TripleOscillator";
		plugin.description = "Three powerful oscillators";
		plugin.author = "Tobias Doerffel";
		plugin.supportedFileTypes = "";
		plugin.logoName = "tripleoscillator/logo";
		plugin.version = 0x0110;
		plugin.type = static_cast<int>(Plugin::Type::Instrument);
		plugin.hasSubPluginFeatures = false;

		PluginScanRecord broken;
		broken.filePath = brokenPath;
		broken.size = QFileInfo(brokenPath).size();
		broken.mtimeMs = QFileInfo(brokenPath).lastModified().toMSecsSinceEpoch();
		broken.status = PluginScanRecord::Status::LoadFailed;
		broken.error = "invalid ELF header";

		const QString reason = "crashes the host on load";
		{
			PluginScanCache cache(cachePath("roundtrip"));
			QVERIFY(cache.isPersistent());
			cache.store(plugin);
			cache.store(broken);
			cache.addToQuarantine(plugin.filePath, reason);
			QVERIFY(cache.isDirty());
			QVERIFY2(cache.save(), "the cache must be writable in its own directory");
		}

		// Same data, read back by a fresh instance: this is the next process start.
		PluginScanCache reloaded(cachePath("roundtrip"));
		QVERIFY(reloaded.load());
		QCOMPARE(reloaded.fileCount(), 2);
		QCOMPARE(reloaded.quarantineCount(), 1);

		const PluginScanRecord* got = reloaded.lookup(QFileInfo(pluginPath()));
		QVERIFY2(got != nullptr, "the unchanged plugin file must be a cache hit");
		QCOMPARE(got->status, PluginScanRecord::Status::HasDescriptor);
		QCOMPARE(got->name, plugin.name);
		QCOMPARE(got->displayName, plugin.displayName);
		QCOMPARE(got->description, plugin.description);
		QCOMPARE(got->author, plugin.author);
		QCOMPARE(got->logoName, plugin.logoName);
		QCOMPARE(got->version, plugin.version);
		QCOMPARE(got->type, plugin.type);
		QVERIFY(!got->hasSubPluginFeatures);

		const PluginScanRecord* gotBroken = reloaded.lookup(QFileInfo(brokenPath));
		QVERIFY(gotBroken != nullptr);
		QCOMPARE(gotBroken->status, PluginScanRecord::Status::LoadFailed);
		QCOMPARE(gotBroken->error, broken.error);

		QVERIFY(reloaded.isQuarantined(plugin.filePath));
		QCOMPARE(reloaded.quarantineReason(plugin.filePath), reason);
		QCOMPARE(reloaded.quarantineEntries().size(), qsizetype(1));
	}
	void testLookupMissesOnChangedFingerprint()
	{
		using namespace lmms;

		const QFileInfo file(pluginPath());
		QVERIFY(file.exists());

		PluginScanRecord record;
		record.filePath = file.absoluteFilePath();
		record.size = file.size();
		record.mtimeMs = file.lastModified().toMSecsSinceEpoch();
		record.status = PluginScanRecord::Status::NotAPlugin;

		PluginScanCache cache(cachePath("fingerprint"));
		cache.store(record);
		QVERIFY(cache.lookup(file) != nullptr);

		// A same-size lookalike with a different modification time must miss.
		PluginScanRecord older = record;
		older.mtimeMs -= 1000;
		PluginScanCache cache2(cachePath("fingerprint"));
		cache2.store(older);
		QVERIFY2(cache2.lookup(file) == nullptr, "a different mtime must not be served from the cache");

		// Same for a different size.
		PluginScanRecord smaller = record;
		smaller.size -= 1;
		PluginScanCache cache3(cachePath("fingerprint"));
		cache3.store(smaller);
		QVERIFY2(cache3.lookup(file) == nullptr, "a different size must not be served from the cache");
	}
	void testQuarantineSurvivesSaveAndLoad()
	{
		using namespace lmms;

		const QString path = m_pluginDir->filePath("libquarantine-me.so");
		PluginScanCache cache(cachePath("quarantine"));
		QVERIFY(!cache.isQuarantined(path));
		cache.addToQuarantine(path, "hung the host");
		cache.addToQuarantine(path, "a second reason must not overwrite the first");
		QCOMPARE(cache.quarantineCount(), 1);
		QVERIFY(cache.save());

		PluginScanCache reloaded(cachePath("quarantine"));
		QVERIFY(reloaded.load());
		QVERIFY(reloaded.isQuarantined(path));
		QCOMPARE(reloaded.quarantineReason(path), QStringLiteral("hung the host"));

		// Un-quarantining is a real operation, not a file edit.
		QVERIFY(reloaded.removeFromQuarantine(path));
		QVERIFY(!reloaded.isQuarantined(path));
		QVERIFY(reloaded.save());

		PluginScanCache empty(cachePath("quarantine"));
		QVERIFY(empty.load());
		QCOMPARE(empty.quarantineCount(), 0);
	}
	void testCorruptCacheDegradesToFullScan()
	{
		using namespace lmms;

		const QString cacheFile = cachePath("corrupt");
		{
			QFile file(cacheFile);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write("{ this is not json, it is a corrupted cache");
		}

		PluginScanCache cache(cacheFile);
		QVERIFY2(!cache.load(), "a corrupt cache must report failure");
		QCOMPARE(cache.fileCount(), 0);
		QCOMPARE(cache.quarantineCount(), 0);

		// ... and the scan itself must not degrade to an empty plugin list. The
		// search path is pinned before construction so the constructor's own scan
		// is the one meeting the corrupt file.
		QDir::setSearchPaths("plugins", QStringList{m_pluginDir->path()});
		qputenv("LMMS_PLUGIN_SCAN_CACHE", cacheFile.toLocal8Bit());
		PluginFactory factory;
		QCOMPARE(factory.scanStats().candidateFiles, 1);
		QCOMPARE(factory.scanStats().scanned, 1);
		QCOMPARE(factory.scanStats().servedFromCache, 0);
		QVERIFY2(!factory.pluginInfo("tripleoscillator").isNull(),
			"a corrupt cache must never empty the plugin list");

		// The scan wrote a valid cache back, so the next start is warm.
		{
			PluginScanCache rewritten(cacheFile);
			QVERIFY2(rewritten.load(), "the scan must replace a corrupt cache with a valid one");
		}
		PluginFactory second;
		QCOMPARE(second.scanStats().servedFromCache, 1);
		QVERIFY(!second.pluginInfo("tripleoscillator").isNull());
	}

	// --- driven through PluginFactory and a real plugin module --------------
	void testRealPluginIsScannedThenServedFromCache()
	{
		using namespace lmms;

		if (!pluginModuleCopied()) { QSKIP("no built tripleoscillator module to scan"); }
		if (!testHostCanLoadPluginModules()) { QSKIP("a Windows test host cannot load plugin MODULE libraries"); }

		const QString cacheFile = cachePath("warm");
		qputenv("LMMS_PLUGIN_SCAN_CACHE", cacheFile.toLocal8Bit());

		// Cold: the cache file does not exist - a full scan.
		hideFixtureFromSearchPaths();
		PluginFactory cold;
		pinFixtureAndRescan(cold, cacheFile);
		QCOMPARE(cold.scanStats().candidateFiles, 1);
		QCOMPARE(cold.scanStats().scanned, 1);
		QCOMPARE(cold.scanStats().servedFromCache, 0);
		QCOMPARE(cold.scanStats().quarantined, 0);
		const PluginFactory::PluginInfo coldInfo = cold.pluginInfo("tripleoscillator");
		QVERIFY2(!coldInfo.isNull(), "the real module must be discovered");
		QVERIFY(coldInfo.library->isLoaded());
		QCOMPARE(QString::fromUtf8(coldInfo.descriptor->displayName), QStringLiteral("TripleOscillator"));
		QVERIFY2(QFileInfo::exists(cacheFile), "the first scan must write the cache");

		// Warm: same fingerprint, so no file is opened at all.
		hideFixtureFromSearchPaths();
		PluginFactory warm;
		pinFixtureAndRescan(warm);
		QCOMPARE(warm.scanStats().candidateFiles, 1);
		QCOMPARE(warm.scanStats().scanned, 0);
		QCOMPARE(warm.scanStats().servedFromCache, 1);
		QCOMPARE(warm.scanStats().descriptors, 1);
		QCOMPARE(pluginFingerprints(warm), pluginFingerprints(cold));

		const PluginFactory::PluginInfo warmInfo = warm.pluginInfo("tripleoscillator");
		QVERIFY(!warmInfo.isNull());
		QCOMPARE(warmInfo.file.absoluteFilePath(), pluginPath());
		QCOMPARE(warmInfo.descriptor->version, 0x0110);
		QCOMPARE(static_cast<int>(warmInfo.descriptor->type), static_cast<int>(Plugin::Type::Instrument));
		QVERIFY(warmInfo.descriptor->subPluginFeatures == nullptr);
		QVERIFY2(warmInfo.descriptor->logo != nullptr, "the descriptor's logo must survive the cache");
		QCOMPARE(QString::fromStdString(warmInfo.descriptor->logo->pixmapName()),
			QStringLiteral("tripleoscillator/logo"));
	}
	void testChangedPluginFileIsRescanned()
	{
		using namespace lmms;

		if (!pluginModuleCopied()) { QSKIP("no built tripleoscillator module to scan"); }
		if (!testHostCanLoadPluginModules()) { QSKIP("a Windows test host cannot load plugin MODULE libraries"); }

		const QString cacheFile = cachePath("changed");
		qputenv("LMMS_PLUGIN_SCAN_CACHE", cacheFile.toLocal8Bit());

		hideFixtureFromSearchPaths();
		PluginFactory first;
		pinFixtureAndRescan(first, cacheFile);
		QCOMPARE(first.scanStats().scanned, 1);

		hideFixtureFromSearchPaths();
		PluginFactory second;
		pinFixtureAndRescan(second);
		QCOMPARE(second.scanStats().servedFromCache, 1);

		// The file changed under the cache (a replaced build): the size stays the
		// same on purpose, so this proves the mtime half of the fingerprint.
		QFile file(pluginPath());
		QVERIFY(file.open(QIODevice::ReadWrite));
		QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime));
		file.close();

		hideFixtureFromSearchPaths();
		PluginFactory third;
		pinFixtureAndRescan(third);
		QCOMPARE(third.scanStats().candidateFiles, 1);
		QCOMPARE(third.scanStats().servedFromCache, 0);
		QCOMPARE(third.scanStats().scanned, 1);
		QVERIFY(!third.pluginInfo("tripleoscillator").isNull());

		// Fingerprints agree again from here on.
		hideFixtureFromSearchPaths();
		PluginFactory fourth;
		pinFixtureAndRescan(fourth);
		QCOMPARE(fourth.scanStats().servedFromCache, 1);
	}
	void testCacheHitDoesNotLoadTheLibrary()
	{
		using namespace lmms;

		if (!pluginModuleCopied()) { QSKIP("no built tripleoscillator module to scan"); }
		if (!testHostCanLoadPluginModules()) { QSKIP("a Windows test host cannot load plugin MODULE libraries"); }

		// A copy of the module this process has never opened, with a hand-written
		// cache record standing in for a previous run that scanned it:
		//
		// QLibrary::isLoaded() reports the file's *global* loaded state, so "was it
		// opened?" can only be proven for a file this process has not already
		// dlopen()ed - hence the copy and the seeded record.
		const QString lazyPath = m_pluginDir->filePath("liblazyprobe.so");
		QVERIFY(QFile::copy(pluginPath(), lazyPath));
		const QFileInfo lazy(lazyPath);

		const QString cacheFile = cachePath("lazy");
		qputenv("LMMS_PLUGIN_SCAN_CACHE", cacheFile.toLocal8Bit());
		{
			PluginScanCache seed(cacheFile);
			PluginScanRecord record;
			record.filePath = lazy.absoluteFilePath();
			record.size = lazy.size();
			record.mtimeMs = lazy.lastModified().toMSecsSinceEpoch();
			record.status = PluginScanRecord::Status::HasDescriptor;
			record.name = "lazyprobe";
			record.displayName = "LazyProbe";
			record.description = "a copy of the module, cached by the last run";
			record.author = "the cache";
			record.logoName = "tripleoscillator/logo";
			record.version = 0x0110;
			record.type = static_cast<int>(Plugin::Type::Instrument);
			record.hasSubPluginFeatures = false;
			seed.store(record);
			QVERIFY(seed.save());
		}

		hideFixtureFromSearchPaths();
		PluginFactory factory;
		pinFixtureAndRescan(factory);
		QCOMPARE(factory.scanStats().candidateFiles, 2); // the copy + tripleoscillator
		QCOMPARE(factory.scanStats().scanned, 1);        // only the untouched one is loaded
		QCOMPARE(factory.scanStats().servedFromCache, 1);

		const PluginFactory::PluginInfo info = factory.pluginInfo("lazyprobe");
		QVERIFY(!info.isNull());
		QVERIFY2(!info.library->isLoaded(), "a cache hit must not dlopen the plugin");
		QVERIFY(info.library->fileName() == lazyPath);
		QCOMPARE(QString::fromUtf8(info.descriptor->displayName), QStringLiteral("LazyProbe"));

		// ... but the first real use does, at the exact call Plugin::instantiate
		// makes, and resolves the plugin's entry point.
		QVERIFY2(info.library->resolve("lmms_plugin_main") != nullptr,
			"the deferred library must still load on demand");
		QVERIFY(info.library->isLoaded());
	}
	void testQuarantinedPluginIsHiddenFromDiscovery()
	{
		using namespace lmms;

		if (!pluginModuleCopied()) { QSKIP("no built tripleoscillator module to scan"); }
		if (!testHostCanLoadPluginModules()) { QSKIP("a Windows test host cannot load plugin MODULE libraries"); }

		const QString cacheFile = cachePath("hidden");
		qputenv("LMMS_PLUGIN_SCAN_CACHE", cacheFile.toLocal8Bit());

		{
			PluginFactory warm;
			pinFixtureAndRescan(warm);
			QVERIFY(!warm.pluginInfo("tripleoscillator").isNull());
			warm.scanCache().addToQuarantine(pluginPath(), "hung the host on load");
			QVERIFY(warm.scanCache().save());
		}

		// A new factory is what a restart looks like.
		PluginFactory quarantined;
		pinFixtureAndRescan(quarantined);
		QCOMPARE(quarantined.scanStats().candidateFiles, 1);
		QCOMPARE(quarantined.scanStats().quarantined, 1);
		QCOMPARE(quarantined.scanStats().servedFromCache, 0);
		QCOMPARE(quarantined.scanStats().scanned, 0);
		QVERIFY2(quarantined.pluginInfo("tripleoscillator").isNull(),
			"a quarantined plugin must not be discoverable");
		QVERIFY(quarantined.descriptors(Plugin::Type::Instrument).isEmpty());
		QCOMPARE(quarantined.scanStats().quarantinedPaths, QStringList{pluginPath()});
		QCOMPARE(quarantined.scanStats().quarantinedReasons, QStringList{QStringLiteral("hung the host on load")});
		QVERIFY2(quarantined.scanReport().contains("quarantined"), qPrintable(quarantined.scanReport()));
		QVERIFY2(quarantined.scanReport().contains(pluginPath()), qPrintable(quarantined.scanReport()));

		// And it is still hidden after a save/load cycle.
		PluginScanCache reloaded(cacheFile);
		QVERIFY(reloaded.load());
		QVERIFY(reloaded.isQuarantined(pluginPath()));

		// Un-quarantine for the tests that follow, through the same API.
		PluginScanCache clean(cacheFile);
		QVERIFY(clean.load());
		QVERIFY(clean.removeFromQuarantine(pluginPath()));
		QVERIFY(clean.save());
	}
	void testKnownBadFileIsNotLoadedAgain()
	{
		using namespace lmms;

		if (!pluginModuleCopied()) { QSKIP("no built tripleoscillator module to scan"); }

		const QString badFile = m_pluginDir->filePath("libnotaplugin.so");
		{
			QFile file(badFile);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write("this file is not a shared object\n");
		}

		const QString cacheFile = cachePath("negative");
		qputenv("LMMS_PLUGIN_SCAN_CACHE", cacheFile.toLocal8Bit());

		hideFixtureFromSearchPaths();
		PluginFactory first;
		pinFixtureAndRescan(first, cacheFile);
		QCOMPARE(first.scanStats().candidateFiles, 2);
		QCOMPARE(first.scanStats().scanned, 2);
		QVERIFY(!first.errorString("libnotaplugin").isEmpty());

		// Second scan: the real plugin comes from the cache, the broken file is
		// left alone - neither one is opened.
		hideFixtureFromSearchPaths();
		PluginFactory second;
		pinFixtureAndRescan(second);
		QCOMPARE(second.scanStats().candidateFiles, 2);
		QCOMPARE(second.scanStats().scanned, 0);
		QCOMPARE(second.scanStats().servedFromCache, 1);
		QCOMPARE(second.scanStats().negativeFromCache, 1);
		QVERIFY(!second.pluginInfo("tripleoscillator").isNull());
		QVERIFY2(!second.errorString("libnotaplugin").isEmpty(),
			"the remembered load failure must still explain itself");

		// A changed bad file is tried again (the fingerprint, not the name, decides).
		QFile file(badFile);
		QVERIFY(file.open(QIODevice::Append));
		file.write("and now it is longer\n");
		file.close();

		hideFixtureFromSearchPaths();
		PluginFactory third;
		pinFixtureAndRescan(third);
		QCOMPARE(third.scanStats().scanned, 1);

		QVERIFY(QFile::remove(badFile)); // leave the fixture as the other tests expect it
	}
	void testCacheDisabledBehavesLikeAColdCache()
	{
		using namespace lmms;

		if (!pluginModuleCopied()) { QSKIP("no built tripleoscillator module to scan"); }
		if (!testHostCanLoadPluginModules()) { QSKIP("a Windows test host cannot load plugin MODULE libraries"); }

		// Today's behaviour, byte for byte: no cache at all. An empty path means
		// "no persistence" (set explicitly: whether the machine happens to have a
		// working directory must not decide what this test asserts).
		qputenv("LMMS_PLUGIN_SCAN_CACHE", "");
		hideFixtureFromSearchPaths();
		PluginFactory noCache;
		pinFixtureAndRescan(noCache);
		QVERIFY2(!noCache.scanCache().isPersistent(), "an empty cache path must mean no persistence");
		QCOMPARE(noCache.scanStats().scanned, 1);
		QVERIFY(!QFileInfo::exists(m_cacheDir->filePath("no-cache.json")));

		// A cache file that does not exist yet (the first run of the new build).
		const QString coldFile = cachePath("no-cache");
		qputenv("LMMS_PLUGIN_SCAN_CACHE", coldFile.toLocal8Bit());
		hideFixtureFromSearchPaths();
		PluginFactory cold;
		pinFixtureAndRescan(cold, coldFile);
		QCOMPARE(cold.scanStats().scanned, 1);
		QCOMPARE(cold.scanStats().servedFromCache, 0);
		QCOMPARE(cold.scanStats().candidateFiles, noCache.scanStats().candidateFiles);
		QCOMPARE(cold.scanStats().descriptors, noCache.scanStats().descriptors);
		QCOMPARE(pluginFingerprints(cold), pluginFingerprints(noCache));
	}

	// --- start-up: discovery must not run before the user asks for it -------
	void testPluginBrowserDefersDiscoveryToFirstShow()
	{
		using namespace lmms;

		// Nothing in this process has asked for a plugin yet.
		QVERIFY2(!PluginFactory::instanceExists(),
			"no earlier code path may have constructed the plugin factory");

		// Constructing the browser (what start-up does) must not scan plugins...
		gui::PluginBrowser browser(nullptr);
		QVERIFY2(!PluginFactory::instanceExists(),
			"constructing the plugin browser must not trigger plugin discovery");

		// ...showing it (what the user does) must.
		qputenv("LMMS_PLUGIN_DIR", m_pluginDir->path().toLocal8Bit());
		browser.resize(320, 240);
		browser.show();
		QTest::qWait(0);
		QVERIFY2(PluginFactory::instanceExists(),
			"showing the plugin browser must trigger plugin discovery");
		QVERIFY(getPluginFactory()->scanStats().candidateFiles >= 1);
		if (pluginModuleCopied())
		{
			QVERIFY(!getPluginFactory()->pluginInfo("tripleoscillator").isNull());
		}

		browser.hide();
	}

private:
	QString cachePath(const QString& name) const { return m_cacheDir->filePath(name + ".json"); }
	QString pluginPath() const { return m_pluginDir->filePath(QLatin1String(moduleFileName())); }
	bool pluginModuleCopied() const { return QFileInfo::exists(pluginPath()); }

	/*!
	 * Pin the plugin search path to the fixture directory and re-run the scan.
	 * Test assertions on candidate/served/scanned counts are only meaningful
	 * after this: the factory constructor has already scanned whatever the
	 * machine's default plugin directories contain.
	 *
	 * With \a coldCacheFile the cache file is deleted first, so this run is a
	 * first run (the search paths are process-global and persist across slots,
	 * which is why the constructor's own scan cannot be used as the "cold" step).
	 */
	void pinFixtureAndRescan(lmms::PluginFactory& factory, const QString& coldCacheFile = QString())
	{
		if (!coldCacheFile.isEmpty()) { QFile::remove(coldCacheFile); }
		QDir::setSearchPaths("plugins", QStringList{m_pluginDir->path()});
		factory.discoverPlugins();
	}

	/*!
	 * Keep the fixture out of the search paths until a test explicitly pins it:
	 * the list is process-global, so a factory constructor would otherwise
	 * scan the fixture before the scan the test is actually about (and warm
	 * the cache it is about to assert on).
	 */
	void hideFixtureFromSearchPaths() { QDir::setSearchPaths("plugins", QStringList{}); }

	QScopedPointer<QTemporaryDir> m_pluginDir;
	QScopedPointer<QTemporaryDir> m_cacheDir;
};

QTEST_MAIN(PluginScanCacheTest)
#include "PluginScanCacheTest.moc"
