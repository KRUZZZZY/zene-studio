/*
 * PluginScanWorkerTest.cpp - plugin.scan_start through app.tasks: library loads off the interface thread
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

#include <QtTest>

#include <atomic>
#include <chrono>
#include <thread>

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>

#include "lmmsconfig.h"

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "PluginFactory.h"
#include "TaskCenter.h"

//! Build-tree plugin directory, injected by tests/CMakeLists.txt.
#ifndef LMMS_TEST_SCAN_PLUGIN_DIR
#define LMMS_TEST_SCAN_PLUGIN_DIR "plugins"
#endif

using namespace lmms;

namespace
{

#ifdef LMMS_BUILD_WIN32
constexpr const char* ModuleFile = "tripleoscillator.dll";
#else
constexpr const char* ModuleFile = "libtripleoscillator.so";
#endif

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject row(const QString& id)
{
	for (const QJsonValue& value : run(QStringLiteral("app.tasks")).result.value(QStringLiteral("tasks")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == id) { return value.toObject(); }
	}
	return {};
}

QString stateOf(const QString& id)
{
	return row(id).value(QStringLiteral("state")).toString();
}

int catalogueSize()
{
	return PluginFactory::instance()->descriptors().size();
}

} // namespace

class PluginScanWorkerTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_home;
	QTemporaryDir m_plugins;
	std::atomic<bool> m_release{false};
	std::atomic<bool> m_entered{false};
	std::atomic<std::thread::id> m_loader{};
	qint64 m_stamp = 0;

	//! A new fingerprint, so the scan's next run plans a load of the fixture module.
	void makeModuleStale()
	{
		QFile module(QDir(m_plugins.path()).filePath(QLatin1String(ModuleFile)));
		QVERIFY(module.open(QIODevice::ReadWrite));
		QVERIFY(module.setFileTime(QDateTime::currentDateTimeUtc().addSecs(++m_stamp), QFileDevice::FileModificationTime));
	}

	void holdTheLoads()
	{
		m_release.store(false);
		m_entered.store(false);
		PluginFactory::instance()->setBackgroundLoadHookForTesting([this] {
			m_entered.store(true);
			m_loader.store(std::this_thread::get_id());
			QElapsedTimer bound;
			bound.start();
			while (!m_release.load() && bound.elapsed() < 20000) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
		});
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		QVERIFY(m_plugins.isValid());
#ifdef LMMS_BUILD_WIN32
		QSKIP("a Windows test host cannot load plugin MODULE libraries (see PluginScanCacheTest)");
#endif
		qputenv("LMMS_PLUGIN_SCAN_CACHE", m_home.filePath(QStringLiteral("plugin-scan-cache.json")).toLocal8Bit());
		const QString source = QDir(QStringLiteral(LMMS_TEST_SCAN_PLUGIN_DIR)).filePath(QLatin1String(ModuleFile));
		QVERIFY2(QFile::copy(source, QDir(m_plugins.path()).filePath(QLatin1String(ModuleFile))), qPrintable(source));
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
		PluginFactory::instance();
		QDir::setSearchPaths(QStringLiteral("plugins"), {m_plugins.path()});
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void cleanup()
	{
		m_release.store(true);
		PluginFactory::instance()->setBackgroundLoadHookForTesting({});
		QTRY_VERIFY_WITH_TIMEOUT(!PluginFactory::instance()->backgroundScanActive(), 30000);
	}

	void aScanStartsListsLoadsOffThisThreadAndCompletes()
	{
		makeModuleStale();
		holdTheLoads();
		int ticks = 0;
		QTimer ticker;
		ticker.setInterval(1);
		connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
		ticker.start();

		const ControlResult started = run(QStringLiteral("plugin.scan_start"));
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		const QString id = started.result.value(QStringLiteral("task")).toString();
		QCOMPARE(id, QStringLiteral("plugin-scan-%1").arg(started.result.value(QStringLiteral("task_id")).toInt()));
		QCOMPARE(row(id).value(QStringLiteral("kind")).toString(), QStringLiteral("plugin_scan"));
		QCOMPARE(stateOf(id), QStringLiteral("running"));

		// The load is on a worker, and the event loop turns for as long as it is held there.
		QTRY_VERIFY_WITH_TIMEOUT(m_entered.load(), 5000);
		QVERIFY(m_loader.load() != std::this_thread::get_id());
		const int before = ticks;
		QTRY_VERIFY_WITH_TIMEOUT(ticks >= before + 20, 5000);
		QCOMPARE(stateOf(id), QStringLiteral("running"));

		m_release.store(true);
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(id), QStringLiteral("done"), 30000);
		QCOMPARE(row(id).value(QStringLiteral("progress")).toDouble(), 1.0);
		QVERIFY(!PluginFactory::instance()->pluginInfo("tripleoscillator").isNull());
		QCOMPARE(PluginFactory::instance()->scanStats().scanned, 1);
		QCOMPARE(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), id}}).errorKind, ControlErrorKind::Refused);
	}

	//! A library that never comes back from its load cannot hold the cancel, and the catalogue stands.
	void aCancelDuringALoadEndsTheRunNowAndKeepsTheCatalogue()
	{
		// One completed run first, so there is a published catalogue to keep.
		QVERIFY(run(QStringLiteral("plugin.scan_start")).ok);
		QTRY_VERIFY_WITH_TIMEOUT(!PluginFactory::instance()->backgroundScanActive(), 30000);
		const int published = catalogueSize();
		QVERIFY(published > 0);

		makeModuleStale();
		holdTheLoads();
		const QString id = run(QStringLiteral("plugin.scan_start")).result.value(QStringLiteral("task")).toString();
		QTRY_VERIFY_WITH_TIMEOUT(m_entered.load(), 5000);
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), id}}).ok);
		QCOMPARE(stateOf(id), QStringLiteral("cancelled"));  // with the worker still in the load
		QCOMPARE(PluginFactory::instance()->backgroundScanStatus().outcome, QStringLiteral("cancelled"));
		QCOMPARE(catalogueSize(), published);

		// A new run while the old worker is still in its load: the old report must not drive the new run.
		m_release.store(true);
		QThreadPool::globalInstance()->waitForDone(30000);
		QCoreApplication::processEvents();
		const QString next = run(QStringLiteral("plugin.scan_start")).result.value(QStringLiteral("task")).toString();
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(next), QStringLiteral("done"), 30000);
		QCOMPARE(catalogueSize(), published);
		QCOMPARE(stateOf(id), QStringLiteral("cancelled"));
	}

	void aReportFromAnEndedRunDoesNotTouchTheNextOne()
	{
		makeModuleStale();
		holdTheLoads();
		const QString first = run(QStringLiteral("plugin.scan_start")).result.value(QStringLiteral("task")).toString();
		QTRY_VERIFY_WITH_TIMEOUT(m_entered.load(), 5000);
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), first}}).ok);
		PluginFactory::instance()->setBackgroundLoadHookForTesting({});

		// The first worker is still held; a paced second run is on its way when that worker reports.
		const ControlResult second = run(QStringLiteral("plugin.scan_start"), {{QStringLiteral("pace_ms"), 200}});
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		const QString id = second.result.value(QStringLiteral("task")).toString();
		QVERIFY(id != first);
		m_release.store(true);
		QThreadPool::globalInstance()->waitForDone(30000);
		QCoreApplication::processEvents();
		QCOMPARE(stateOf(id), QStringLiteral("running"));
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(id), QStringLiteral("done"), 30000);
		QCOMPARE(stateOf(first), QStringLiteral("cancelled"));
	}
};

QTEST_MAIN(PluginScanWorkerTest)
#include "PluginScanWorkerTest.moc"
