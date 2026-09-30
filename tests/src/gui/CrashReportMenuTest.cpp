/*
 * CrashReportMenuTest.cpp - Help > Crash Reports arms, shows, keeps and deletes the report
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

/*! The submenu drives the reporter end to end over a throwaway directory: armed through the
 *  socket, a report file written where a crash would write it, then from the menu - the line
 *  names the report as not yet offered, Stop Offering acknowledges it (the file stays), Delete
 *  asks first (a No keeps it) and then discards it, Show Report Folder opens the report
 *  directory, and Report Crashes disarms. POSIX only: on Windows the module is a documented no-op
 *  and every writer refuses (include/CrashReporter.h). */

#include <QtTest>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "CrashReportMenu.h"
#include "Engine.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QJsonObject reports()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("crash.list_reports"), QJsonObject{}).result;
}

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

QString line(QMenu* menu) { return menu->actions().at(1)->text(); }

} // namespace

class CrashReportMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("crash.disable"), QJsonObject{});
		setCrashReportDeleteConfirm({});
		setCrashReportFolderOpener({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theLineNamesTheState()
	{
		QCOMPARE(crashReportStatusText({{QStringLiteral("armed"), false}, {QStringLiteral("report_count"), 0}}),
			QStringLiteral("Not armed, no report"));
		QCOMPARE(crashReportStatusText({{QStringLiteral("armed"), true}, {QStringLiteral("report_count"), 1},
			{QStringLiteral("pending"), true}}), QStringLiteral("Armed, one report not yet offered"));
		QCOMPARE(crashReportStatusText({{QStringLiteral("armed"), true}, {QStringLiteral("report_count"), 1},
			{QStringLiteral("pending"), false}}), QStringLiteral("Armed, one report (already offered)"));
	}

	void theReportIsKeptOrDeletedFromTheMenu()
	{
#ifdef _WIN32
		QSKIP("the crash reporter is a documented no-op on Windows (include/CrashReporter.h)");
#endif
		QVERIFY(m_reports.isValid());
		const ControlResult armed = ControlRegistry::instance()->invoke(QStringLiteral("crash.enable"),
			{{QStringLiteral("directory"), m_reports.path()}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		const QString reportPath = reports().value(QStringLiteral("report_path")).toString();
		QVERIFY(!reportPath.isEmpty());
		QVERIFY(QDir().mkpath(QFileInfo(reportPath).path()));
		{
			QFile report(reportPath);
			QVERIFY(report.open(QIODevice::WriteOnly));
			report.write("zene crash report (test)\n");
		}

		QMenu parent;
		QMenu* crash = addCrashReportMenu(&parent);
		QAction* arm = item(crash, QStringLiteral("Report Crashes"));
		QAction* acknowledge = item(crash, QStringLiteral("Stop Offering the Report"));
		QAction* discard = item(crash, QStringLiteral("Delete the Report..."));
		QAction* folder = item(crash, QStringLiteral("Show Report Folder"));
		QVERIFY(arm && acknowledge && discard && folder);
		QVERIFY(arm->isChecked());
		QCOMPARE(line(crash), QStringLiteral("Armed, one report not yet offered"));
		QVERIFY(acknowledge->isEnabled());

		acknowledge->trigger();
		QVERIFY(!reports().value(QStringLiteral("pending")).toBool());
		QVERIFY(QFileInfo::exists(reportPath));
		QCOMPARE(line(crash), QStringLiteral("Armed, one report (already offered)"));
		QVERIFY(!acknowledge->isEnabled());

		QString asked;
		setCrashReportDeleteConfirm([&asked](const QString& path) { asked = path; return false; });
		discard->trigger();
		QCOMPARE(asked, reportPath);
		QVERIFY(QFileInfo::exists(reportPath));
		setCrashReportDeleteConfirm([](const QString&) { return true; });
		discard->trigger();
		QVERIFY(!QFileInfo::exists(reportPath));
		QCOMPARE(line(crash), QStringLiteral("Armed, no report"));
		QVERIFY(!discard->isEnabled());

		QString opened;
		setCrashReportFolderOpener([&opened](const QString& dir) { opened = dir; });
		folder->trigger();
		QCOMPARE(opened, reports().value(QStringLiteral("report_directory")).toString());
		QVERIFY(!opened.isEmpty());

		arm->trigger();
		QVERIFY(!reports().value(QStringLiteral("armed")).toBool());
		QVERIFY(!arm->isChecked());
		QCOMPARE(line(crash), QStringLiteral("Not armed, no report"));
	}

private:
	QTemporaryDir m_home;
	QTemporaryDir m_reports;
};

QTEST_MAIN(CrashReportMenuTest)
#include "CrashReportMenuTest.moc"
