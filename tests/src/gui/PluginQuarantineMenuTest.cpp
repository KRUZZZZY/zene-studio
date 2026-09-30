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
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		setPluginQuarantinePicker({});
		ControlRegistry::setReady(false);
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
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(PluginQuarantineMenuTest)
#include "PluginQuarantineMenuTest.moc"
