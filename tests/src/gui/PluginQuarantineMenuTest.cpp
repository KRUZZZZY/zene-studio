/*
 * PluginQuarantineMenuTest.cpp - Edit > Plugin Quarantine adds and releases a quarantined file
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

/*! Over a throwaway scan-cache file (LMMS_PLUGIN_SCAN_CACHE, set before the engine starts, so the
 *  edits are persistent - the commands refuse an in-memory cache): Quarantine a Plugin File...
 *  adds the picked file (a cancelled pick adds nothing), the rebuilt submenu offers Release for
 *  it with the path and reason as its tooltip, Release takes it off, and a file that is gone is
 *  marked so. The counts line is pinned. */

#include <QtTest>

#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "PluginQuarantineMenu.h"
#include "SafeStart.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QStringList quarantined()
{
	QStringList paths;
	const QJsonArray list = ControlRegistry::instance()->invoke(QStringLiteral("plugin.scan_cache_get_state"),
		QJsonObject{}).result.value(QStringLiteral("quarantine")).toArray();
	for (const QJsonValue& entry : list) { paths << entry.toObject().value(QStringLiteral("path")).toString(); }
	return paths;
}

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

} // namespace

class PluginQuarantineMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("LMMS_PLUGIN_SCAN_CACHE", m_home.filePath(QStringLiteral("plugin-scan-cache.json")).toLocal8Bit());
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		QVERIFY2(!ControlRegistry::startupComplete(),
			"the GUI prompt runs before the registry startup-ready flag is set");
	}

	void cleanupTestCase()
	{
		setPluginQuarantinePicker({});
		setCrashQuarantineAsker({});
		setCrashQuarantineFailureReporter({});
		Engine::destroy();
	}

	void theLineCountsTheCache()
	{
		QCOMPARE(pluginQuarantineStatusText({{QStringLiteral("entry_count"), 12}, {QStringLiteral("quarantine_count"), 2}}),
			QStringLiteral("12 files in the scan cache, 2 quarantined"));
	}

	void aFileIsQuarantinedAndReleasedFromTheMenu()
	{
		const QString plugin = m_home.filePath(QStringLiteral("libcrashy.so"));
		{
			QFile file(plugin);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write("not really a plugin");
		}
		QMenu parent;
		QMenu* menu = addPluginQuarantineMenu(&parent);
		emit menu->aboutToShow();
		QVERIFY(item(menu, QStringLiteral("Rescan Plugins")) != nullptr);
		QAction* add = item(menu, QStringLiteral("Quarantine a Plugin File..."));
		QVERIFY(add != nullptr);
		QVERIFY(add->isEnabled());

		setPluginQuarantinePicker([] { return QString(); });
		add->trigger();
		QVERIFY(!quarantined().contains(plugin));

		setPluginQuarantinePicker([plugin] { return plugin; });
		add->trigger();
		QVERIFY(quarantined().contains(plugin));
		QAction* release = item(menu, QStringLiteral("Release libcrashy.so"));
		QVERIFY(release != nullptr);
		QCOMPARE(release->toolTip(), plugin + QStringLiteral(" - quarantined from the menu"));
		QVERIFY(menu->actions().first()->text().endsWith(QStringLiteral("1 quarantined")));

		QVERIFY(QFile::remove(plugin));
		emit menu->aboutToShow();
		release = item(menu, QStringLiteral("Release libcrashy.so (file gone)"));
		QVERIFY(release != nullptr);
		release->trigger();
		QVERIFY(!quarantined().contains(plugin));
		QVERIFY(item(menu, QStringLiteral("Release libcrashy.so (file gone)")) == nullptr);
		QAction* rescan = item(menu, QStringLiteral("Rescan Plugins"));
		QVERIFY(rescan != nullptr);
		rescan->trigger();
		QVERIFY2(!menu->actions().first()->text().startsWith(QStringLiteral("Refused:")),
			"the release menu's rescan command must work before registry startup readiness");
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}

	// Q050-049: a marker that names the plug-in the last run died inside produces ONE offer; yes
	// quarantines exactly that file through plugin.scan_cache_quarantine_add, and the ordinary
	// Release item still frees it.
	void aCrashQuarantinedPluginIsOfferedOnceAndReleasable()
	{
		QTemporaryDir work;
		QVERIFY(work.isValid());
		const QString plugin = work.filePath(QStringLiteral("libcrashed.so"));
		{
			QFile marker(work.filePath(QString::fromLatin1(safestart::kMarkerName)));
			QVERIFY(marker.open(QIODevice::WriteOnly));
			marker.write(("Zene Studio safe-start marker v1\npid=4242\ntime_unix=1\nsafe_start_runs=0\n"
				"active_phase=scan\nactive_format=lmms-module\nactive_plugin=\nactive_file="
				+ plugin + "\n").toUtf8());
		}
		QVERIFY(safestart::install(work.path().toStdString()));
		safestart::beginSession(); // initial safe-start setup before GuiApplication
		QVERIFY(safestart::install(work.path().toStdString()));
		safestart::beginSession(); // GUI setup repeats it after GuiApplication
		QVERIFY(safestart::crashSuspect().present);
		QVERIFY(!ControlRegistry::startupComplete());

		int asked = 0;
		QString prompt;
		setCrashQuarantineAsker([&](const QString& text) { ++asked; prompt = text; return false; });
		QVERIFY2(!offerCrashQuarantine(), "answering no must quarantine nothing");
		QCOMPARE(asked, 1);
		QVERIFY(prompt.contains(plugin));
		QVERIFY(!quarantined().contains(plugin));

		setCrashQuarantineAsker([&](const QString&) { ++asked; return true; });
		QVERIFY(offerCrashQuarantine());
		QVERIFY(quarantined().contains(plugin));

		int failures = 0;
		QString failure;
		setCrashQuarantineFailureReporter([&](const QString& text) { ++failures; failure = text; });
		QVERIFY(!offerCrashQuarantine());
		QCOMPARE(asked, 3);
		QCOMPARE(failures, 1);
		QVERIFY(failure.contains(QStringLiteral("already quarantined")));

		QMenu parent;
		QMenu* menu = addPluginQuarantineMenu(&parent);
		emit menu->aboutToShow();
		QAction* release = item(menu, QStringLiteral("Release libcrashed.so (file gone)"));
		QVERIFY(release != nullptr);
		QVERIFY(release->toolTip().contains(QStringLiteral("scanned")));
		release->trigger();
		QVERIFY2(!quarantined().contains(plugin), "the crash-quarantined entry must be releasable");

		setCrashQuarantineAsker({});
		setCrashQuarantineFailureReporter({});
		safestart::endSession();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(PluginQuarantineMenuTest)
#include "PluginQuarantineMenuTest.moc"
