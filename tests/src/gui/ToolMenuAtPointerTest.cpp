/*
 * ToolMenuAtPointerTest.cpp - Navigate > Tool Menu at Pointer leaves the tool menu open
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

/*! cluster gui-1105: choosing Navigate > Tool Menu at Pointer (window.tool_menu) closes the Navigate menu and leaves
 *  the tool menu - the arranger's and the note editor's tools - open at the pointer, whether the item is clicked or
 *  chosen with the keyboard, for a person at a display. The socket command answers at once with the entries. In an
 *  agent instance (--control-socket, as the GUI sweep starts Zene) the registry is headless by design and no popup is
 *  shown (UnattendedRun.h) - the case the sweep reported as a dismissed menu; it was a false positive. */

#include <QtTest>

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QWindow>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MenuBarExtras.h"
#include "MenuTestSupport.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! The shown menu (a popup) carrying the "Arranger" section the tool menu opens with, or nullptr.
QMenu* shownToolMenu()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		auto* menu = qobject_cast<QMenu*>(widget);
		if (menu == nullptr || !menu->isVisible()) { continue; }
		for (QAction* action : menu->actions())
		{
			if (action->isSeparator() && action->text() == QObject::tr("Arranger")) { return menu; }
		}
	}
	return nullptr;
}

void closeToolMenus()
{
	while (QWidget* popup = QApplication::activePopupWidget()) { popup->close(); }
	QCoreApplication::processEvents();
}

} // namespace

class ToolMenuAtPointerTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		// A person at a display (the offscreen platform is one): the menu is shown, not only answered.
		m_headless = ControlRegistry::instance()->isHeadless();
		ControlRegistry::instance()->setHeadless(false);
		getGUI()->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(getGUI()->mainWindow()));
		m_navigate = installNavigateMenu(getGUI()->mainWindow()->menuBar());
		QVERIFY(m_navigate != nullptr);
	}

	void cleanupTestCase()
	{
		closeToolMenus();
		ControlRegistry::instance()->setHeadless(m_headless);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void cleanup() { closeToolMenus(); }

private:
	//! Opens the Navigate menu the way a person does: a click on its title in the main window's menu bar.
	void openFromMenuBar()
	{
		QMenuBar* bar = getGUI()->mainWindow()->menuBar();
		QWindow* main = getGUI()->mainWindow()->windowHandle();
		QVERIFY(main != nullptr);
		const QPoint title = bar->mapTo(getGUI()->mainWindow(), bar->actionGeometry(m_navigate->menuAction()).center());
		QTest::mouseMove(main, title);
		QTest::mouseClick(main, Qt::LeftButton, {}, title);
		QTRY_VERIFY(m_navigate->isVisible());
		QVERIFY(QTest::qWaitForWindowExposed(m_navigate));
	}

private slots:
	//! A click on the item: the Navigate menu closes and the tool menu stays open.
	void clickLeavesToolMenuOpen()
	{
		QAction* item = test::menuAction(m_navigate, {QStringLiteral("Tool Menu at Pointer")});
		QVERIFY(item != nullptr);
		QVERIFY(item->isEnabled());
		openFromMenuBar();
		// Through the window, as the window system delivers a click (QGuiApplication's popup handling sees it too).
		QWindow* window = m_navigate->windowHandle();
		QVERIFY(window != nullptr);
		const QPoint at = m_navigate->actionGeometry(item).center();
		QTest::mouseMove(window, at);
		QTest::mouseClick(window, Qt::LeftButton, {}, at);
		QCoreApplication::processEvents();
		QCoreApplication::processEvents();
		QVERIFY(!m_navigate->isVisible());
		QMenu* tools = shownToolMenu();
		QVERIFY2(tools != nullptr, "the tool menu was dismissed with the Navigate menu");
		QCOMPARE(QApplication::activePopupWidget(), tools);
		QVERIFY(test::menuAction(tools, {QStringLiteral("draw")}) != nullptr);
	}

	//! The same item chosen from the keyboard (Return on the active item).
	void keyboardLeavesToolMenuOpen()
	{
		QAction* item = test::menuAction(m_navigate, {QStringLiteral("Tool Menu at Pointer")});
		QVERIFY(item != nullptr);
		m_navigate->popup(QPoint(40, 40));
		QVERIFY(QTest::qWaitForWindowExposed(m_navigate));
		m_navigate->setActiveAction(item);
		QTest::keyClick(m_navigate, Qt::Key_Return);
		QCoreApplication::processEvents();
		QCoreApplication::processEvents();
		QVERIFY(!m_navigate->isVisible());
		QVERIFY2(shownToolMenu() != nullptr, "the tool menu was dismissed with the Navigate menu");
	}

	//! The command from the socket (no menu in the way) still answers at once with the entries and shows the menu.
	void commandAnswersWithEntries()
	{
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("window.tool_menu"), {});
		QVERIFY(result.ok);
		QCOMPARE(result.result.value(QStringLiteral("shown")).toBool(), true);
		QVERIFY(!result.result.value(QStringLiteral("entries")).toArray().isEmpty());
		QCoreApplication::processEvents();
		QVERIFY(shownToolMenu() != nullptr);
	}

	//! By design, an agent instance (--control-socket) is headless: the entries are the answer and no popup is shown.
	//! This is what the GUI sweep (which starts Zene with a control socket) saw, not a dismissed menu.
	void agentInstanceAnswersWithoutMenu()
	{
		ControlRegistry::instance()->setHeadless(true);
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("window.tool_menu"), {});
		QCoreApplication::processEvents();
		ControlRegistry::instance()->setHeadless(false);
		QVERIFY(result.ok);
		QCOMPARE(result.result.value(QStringLiteral("shown")).toBool(), false);
		QVERIFY(!result.result.value(QStringLiteral("entries")).toArray().isEmpty());
		QVERIFY(shownToolMenu() == nullptr);
	}

private:
	QTemporaryDir m_home;
	QMenu* m_navigate = nullptr;
	bool m_headless = false;
};

QTEST_MAIN(ToolMenuAtPointerTest)
#include "ToolMenuAtPointerTest.moc"
