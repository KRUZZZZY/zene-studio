/*
 * UpdateCheckHiddenTest.cpp - a copy something else updates shows nothing of the update check
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

/*! Q050-501. The real application started as a Flatpak copy, which is the same absence -DZENE_UPDATE_CHECK=OFF leaves:
 *  no commands, no Help entry, no Settings boxes, no question, and no request even with consent in the config. */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "SetupDialog.h"
#include "UpdateCheck.h"
#include "UpdateCheckWindows.h"
#include "WorkspaceMenus.h"
#include "../core/UpdateFeedFixture.h"

using namespace lmms;
using namespace lmms::gui;

class UpdateCheckHiddenTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("FLATPAK_ID", "org.example.Zene");
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void nothingOfItExists()
	{
		QCOMPARE(updatecheck::managedBy(), QStringLiteral("flatpak"));
		updatetest::FeedServer feed;
		feed.answer(updatetest::release("v999.0.0"));
		ConfigManager::inst()->setValue(QStringLiteral("updatecheck"), QStringLiteral("feed"), feed.url());
		updatecheck::savePreferences({true, true, false});

		for (const char* id : {"app.update_check_get", "app.update_check_set", "app.update_check_run", "window.update_check", "window.update_download_page"})
		{
			QVERIFY2(!ControlRegistry::instance()->hasCommand(QLatin1String(id)), id);
		}

		// The full Help menu, built as the product builds it, has no entry.
		QMenuBar bar;
		bar.addMenu(QStringLiteral("Help"));
		installWorkspaceMenus(&bar);
		for (QAction* top : getGUI()->mainWindow()->menuBar()->actions() + bar.actions())
		{
			if (top->menu() == nullptr) { continue; }
			for (QAction* action : top->menu()->actions())
			{
				QVERIFY(!action->property("controlCommand").toString().contains(QStringLiteral("update_")));
				QVERIFY2(!action->text().contains(QStringLiteral("Updates")), qPrintable(action->text()));
			}
		}

		SetupDialog settings;
		QVERIFY(settings.findChild<QCheckBox*>(QStringLiteral("updateCheckEnabled")) == nullptr);
		settings.reject();

		runUpdateCheckStartup(getGUI()->mainWindow());
		for (QWidget* widget : QApplication::topLevelWidgets()) { QVERIFY(!widget->objectName().startsWith(QStringLiteral("update"))); }
		QCOMPARE(updatecheck::UpdateChecker::instance()->check(true), updatecheck::UpdateChecker::Start::Off);
		QTest::qWait(300);
		QCOMPARE(feed.requests.size(), 0);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(UpdateCheckHiddenTest)
#include "UpdateCheckHiddenTest.moc"
