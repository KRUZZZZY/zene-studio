/*
 * MainWindowGeometryTest.cpp - the main window opens where, and as large as, the user last left it
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

/*! 0.5.0 theme 22 (docs/KNOWN-LIMITATIONS.md: "The main window's own geometry is not remembered; it opens
 *  maximised"), in the real application: with nothing remembered the main window opens maximised, as before; a
 *  window's normal place and its maximised state are kept in the config file (ui/main.geometry) with its screen
 *  and come back; a place off every connected screen is pulled back inside one, title bar reachable. */

#include <QtTest>

#include <QFile>
#include <QGuiApplication>
#include <QScreen>

#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MainWindowGeometry.h"

using namespace lmms;
using namespace lmms::gui;

class MainWindowGeometryTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void init()
	{
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("main.geometry"), QString());
	}

	void withNothingRememberedItOpensMaximised()
	{
		MainWindow* window = getGUI()->mainWindow();
		QVERIFY(mainwindowgeometry::remembered().isEmpty());
		QVERIFY(!mainwindowgeometry::showRestored(window));
		QTRY_VERIFY(window->isMaximized());
	}

	void aNormalPlaceIsKeptAndComesBack()
	{
		QWidget window;
		window.setGeometry(QRect(40, 60, 300, 200));
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		const QRect left = window.geometry();
		mainwindowgeometry::save(&window);
		const QJsonObject kept = mainwindowgeometry::remembered();
		QCOMPARE(kept.value(QStringLiteral("x")).toInt(), left.x());
		QCOMPARE(kept.value(QStringLiteral("y")).toInt(), left.y());
		QCOMPARE(kept.value(QStringLiteral("width")).toInt(), left.width());
		QCOMPARE(kept.value(QStringLiteral("height")).toInt(), left.height());
		QCOMPARE(kept.value(QStringLiteral("maximized")).toBool(), false);
		QCOMPARE(kept.value(QStringLiteral("screen")).toString(), window.screen()->name());
		// Written to the config file, so the next start reads it.
		QFile file(m_home.filePath(QStringLiteral("zene.xml")));
		QVERIFY(file.open(QIODevice::ReadOnly));
		QVERIFY(file.readAll().contains("main.geometry"));

		window.setGeometry(QRect(0, 40, 120, 90));
		QVERIFY(mainwindowgeometry::showRestored(&window));
		QVERIFY(!window.isMaximized());
		QCOMPARE(window.geometry(), left);
	}

	void aMaximisedWindowComesBackMaximisedOverItsNormalPlace()
	{
		QWidget window;
		window.setGeometry(QRect(50, 70, 300, 200));
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		const QRect normal = window.geometry();
		window.showMaximized();
		QTRY_VERIFY(window.isMaximized());
		mainwindowgeometry::save(&window);
		const QJsonObject kept = mainwindowgeometry::remembered();
		QVERIFY(kept.value(QStringLiteral("maximized")).toBool());
		QCOMPARE(kept.value(QStringLiteral("width")).toInt(), normal.width());  // the normal size, not the screen's

		window.showNormal();
		QTRY_VERIFY(!window.isMaximized());
		QVERIFY(mainwindowgeometry::showRestored(&window));
		QTRY_VERIFY(window.isMaximized());
		QCOMPARE(window.normalGeometry().size(), normal.size());
	}

	void aPlaceOffEveryScreenIsPulledBack()
	{
		const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
		const QJsonObject gone{{QStringLiteral("screen"), QStringLiteral("a monitor that was unplugged")},
			{QStringLiteral("x"), area.right() + 4000}, {QStringLiteral("y"), area.top() - 3000},
			{QStringLiteral("width"), 300}, {QStringLiteral("height"), 200}, {QStringLiteral("maximized"), false}};
		const QRect placed = mainwindowgeometry::placement(gone);
		QVERIFY2(area.contains(placed), qPrintable(QStringLiteral("%1,%2 %3x%4").arg(placed.x()).arg(placed.y()).arg(placed.width()).arg(placed.height())));
		QVERIFY(placed.top() >= area.top() + 24);  // room for the title bar above it
		QCOMPARE(placed.size(), QSize(300, 200));
		// Larger than every screen: shrunk to fit.
		QJsonObject huge = gone;
		huge.insert(QStringLiteral("width"), area.width() * 3);
		huge.insert(QStringLiteral("height"), area.height() * 3);
		QVERIFY(area.contains(mainwindowgeometry::placement(huge)));
		// No size: nothing to place.
		QJsonObject empty = gone;
		empty.insert(QStringLiteral("width"), 0);
		QVERIFY(mainwindowgeometry::placement(empty).isEmpty());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(MainWindowGeometryTest)
#include "MainWindowGeometryTest.moc"
