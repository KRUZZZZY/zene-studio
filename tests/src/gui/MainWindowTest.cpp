/*
 * MainWindowTest.cpp - the shell window's own startup and floating-window behaviour
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
/*! The two things MainWindow::finalize() and toggleWindow() decide for themselves, over the REAL
 *  application: a floating editor is hidden as a whole window (its QMdiSubWindow frame goes with
 *  it, not just the widget inside), and an unattended first run does not record the first-run setup
 *  as done - the dialog was skipped, so the next interactive launch still has to ask. */

#include <QtTest>

#include <QAction>
#include <QMenu>
#include <QTabBar>
#include <QTabBar>
#include <QWidget>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControllerRackView.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "ProjectJournal.h"
#include "Shell.h"
#include "ShellRegionTabs.h"
#include "Shell.h"
#include "ShellRegionTabs.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! gui-1030: a real left click on the bottom region's tab for `id`, in the live window, from a fresh Arrange view
//! with the Note Editor open below - the state the GUI sweep clicked from.
void clickBottomTab(const QString& id, const QString& expectedPanel)
{
	Shell* shell = getGUI()->mainWindow()->shell();
	shell->setView(ShellView::Arrange);
	if (!shell->model().isVisible(QStringLiteral("detail")))
	{
		QVERIFY(run(QStringLiteral("window.toggle"), {{QStringLiteral("editor"), QStringLiteral("detail")}}).ok);
	}
	ShellRegionTabs* tabs = shell->regionTabs(ShellRegion::Bottom);
	QVERIFY(tabs != nullptr);
	QTRY_VERIFY(tabs->isVisible());
	QCOMPARE(tabs->currentId(), QStringLiteral("detail"));
	const int index = static_cast<int>(tabs->tabIds().indexOf(id));
	QVERIFY(index >= 0);
	QTabBar* bar = tabs->bar();
	QVERIFY(bar->isVisible());
	const QRect rect = bar->tabRect(index);
	QVERIFY(bar->rect().contains(rect.center()));
	QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, rect.center());
	QCOMPARE(shell->model().openIn(ShellRegion::Bottom), expectedPanel);
	QVERIFY(shell->model().isVisible(expectedPanel));
	QCOMPARE(tabs->currentId(), expectedPanel);
	QVERIFY(!shell->model().isVisible(QStringLiteral("detail")));
}

} // namespace

class MainWindowTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		getGUI()->mainWindow()->show();
		QCoreApplication::processEvents();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void hidingAFloatingEditorTakesItsWindowFrameWithIt()
	{
		QWidget* rack = getGUI()->getControllerRackView();
		QVERIFY(rack != nullptr);
		QWidget* frame = rack->parentWidget();
		QVERIFY(frame != nullptr);  // the SubWindow the floating editor lives in

		QVERIFY(run(QStringLiteral("window.toggle"), {{QStringLiteral("editor"), QStringLiteral("controller_rack")}}).ok);
		QTRY_VERIFY(rack->isVisible());
		// The floating editor floats out of the workspace: it becomes its own top-level window,
		// and SubWindow::floatOut() hides the workspace frame behind it on purpose.
		QVERIFY(rack->window() == rack);
		QVERIFY(!frame->isVisible());

		QVERIFY(run(QStringLiteral("window.toggle"), {{QStringLiteral("editor"), QStringLiteral("controller_rack")}}).ok);
		// Toggling off must take the whole thing off screen: no title bar and frame left behind
		// holding nothing, which is what the user would otherwise be left looking at.
		QTRY_VERIFY(!rack->isVisible());
		QVERIFY(!frame->isVisible());
	}

	//! A tab of the bottom region goes through window.toggle on the real shell (ShellRegionTabs::toggle): picking
	//! the Mixer tab while the Note Editor is open must leave the Mixer open and its tab the selected one.
	void pickingTheMixerTabOpensTheMixerPanel()
	{
		Shell* shell = getGUI()->mainWindow()->shell();
		shell->setView(ShellView::Arrange);
		ShellRegionTabs* tabs = shell->regionTabs(ShellRegion::Bottom);
		QVERIFY(tabs != nullptr);
		QVERIFY(shell->revealPanel(QStringLiteral("detail")));
		QCOMPARE(tabs->currentId(), QStringLiteral("detail"));
		const int index = static_cast<int>(tabs->tabIds().indexOf(QStringLiteral("mixer")));
		QVERIFY(index >= 0);
		tabs->bar()->setCurrentIndex(index);  // what a click on the tab does
		QVERIFY(shell->model().isVisible(QStringLiteral("mixer")));
		QCOMPARE(shell->model().openIn(ShellRegion::Bottom), QStringLiteral("mixer"));
		QCOMPARE(tabs->currentId(), QStringLiteral("mixer"));
	}

	void anUnattendedFirstRunDoesNotRecordTheSetupAsDone()
	{
		// This run never showed the setup dialog (tests run on the offscreen platform, which
		// isUnattendedRun() reports as having no display), so nothing was configured by the
		// user: the next interactive launch must still be offered the dialog.
		QCOMPARE(ConfigManager::inst()->value(QStringLiteral("app"), QStringLiteral("configured")).toInt(), 0);
	}

	void redoShortcutWorksAfterUndoWhileTheEditMenuIsClosed()
	{
		// Edit > Redo is refreshed only when the Edit menu opens, and a disabled QAction does not
		// fire its shortcut. Undoing a step therefore has to refresh Redo, or Ctrl+Shift+Z stays
		// dead until the menu is opened again.
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		journal->clearJournal();
		QAction* undoAction = editAction(QStringLiteral("control.undo"));
		QAction* redoAction = editAction(QStringLiteral("control.redo"));
		QVERIFY(undoAction != nullptr);
		QVERIFY(redoAction != nullptr);

		int undone = 0;
		int redone = 0;
		journal->addJournalAction([&undone]() { ++undone; }, [&redone]() { ++redone; });
		openEditMenu(undoAction);
		QVERIFY(undoAction->isEnabled());
		QVERIFY(!redoAction->isEnabled());  // nothing to redo yet, so the menu greys it out

		pressShortcut(undoAction);
		QCOMPARE(undone, 1);
		QVERIFY(redoAction->isEnabled());

		pressShortcut(redoAction);
		QCOMPARE(redone, 1);
	}

	void undoShortcutWorksForAnEditMadeWhileTheEditMenuIsClosed()
	{
		// The mirror of the Redo case: the menu was last opened with nothing to undo, so Undo was
		// greyed out. An edit that arrives afterwards must make Undo available without the menu
		// being reopened.
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		journal->clearJournal();
		QAction* undoAction = editAction(QStringLiteral("control.undo"));
		QVERIFY(undoAction != nullptr);

		openEditMenu(undoAction);
		QVERIFY(!undoAction->isEnabled());

		int undone = 0;
		journal->addJournalAction([&undone]() { ++undone; }, []() {});
		QVERIFY(undoAction->isEnabled());

		pressShortcut(undoAction);
		QCOMPARE(undone, 1);
	}

	void aStepUnwoundThroughTheJournalRefreshesRedo()
	{
		// control.undo and control.redo unwind the journal directly, not through the menu slots,
		// so the Edit menu's Redo must follow the stack whichever path moved it.
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		journal->clearJournal();
		QAction* redoAction = editAction(QStringLiteral("control.redo"));
		QVERIFY(redoAction != nullptr);

		journal->addJournalAction([]() {}, []() {});
		journal->undo();
		QVERIFY(redoAction->isEnabled());
		journal->redo();
		QVERIFY(!redoAction->isEnabled());
	}

	// gui-1030-main-mixer / -devices / -automation / -pattern-editor: each bottom tab, clicked.
	// gui-1030 (all four): the sweep clicked these tabs right after popping the Inspector out. The floated Inspector
	// then sits over the left of the window and the freed column moves the tab strip left under it, so those clicks
	// landed on the floating window, not on a tab. The tabs themselves answer a click (the four tests below).
	void aFloatedInspectorCoversTheFirstBottomTabsInTheLiveWindow()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->resize(1600, 1000);
		Shell* shell = window->shell();
		shell->setView(ShellView::Arrange);
		QVERIFY(shell->detachPanel(QStringLiteral("inspector")));
		QWidget* floating = shell->floatingWindow(QStringLiteral("inspector"));
		QVERIFY(floating != nullptr);
		ShellRegionTabs* tabs = shell->regionTabs(ShellRegion::Bottom);
		QTRY_VERIFY(tabs->bar()->isVisible());
		QTRY_VERIFY(floating->isVisible());
		const auto covered = [&] {
			int count = 0;
			for (const QString& id : {QStringLiteral("mixer"), QStringLiteral("devices"), QStringLiteral("automation"),
					 QStringLiteral("pattern")})
			{
				const int index = static_cast<int>(tabs->tabIds().indexOf(id));
				const QPoint centre = tabs->bar()->mapToGlobal(tabs->bar()->tabRect(index).center());
				count += index >= 0 && floating->frameGeometry().contains(centre) ? 1 : 0;
			}
			return count;
		};
		QTRY_COMPARE(covered(), 4);  // every one of the four sits under the floated Inspector window
		QVERIFY(shell->attachPanel(QStringLiteral("inspector")));
		QTRY_VERIFY(shell->floatingWindow(QStringLiteral("inspector")) == nullptr);
	}

	void clickingTheMixerTabOpensTheMixer() { clickBottomTab(QStringLiteral("mixer"), QStringLiteral("mixer")); }
	void clickingTheDevicesTabOpensTheDevices() { clickBottomTab(QStringLiteral("devices"), QStringLiteral("devices")); }
	void clickingTheAutomationTabOpensTheAutomation() { clickBottomTab(QStringLiteral("automation"), QStringLiteral("automation")); }
	void clickingThePatternEditorTabOpensThePatternEditor() { clickBottomTab(QStringLiteral("pattern"), QStringLiteral("pattern")); }

private:
	// The Edit menu's undo/redo actions are found by the registry command they declare.
	static QAction* editAction(const QString& command)
	{
		const auto actions = getGUI()->mainWindow()->findChildren<QAction*>(QString(), Qt::FindChildrenRecursively);
		for( QAction* action : actions )
		{
			if( action->data().toString() == command ) { return action; }
		}
		return nullptr;
	}

	// Presses the first key combination the action is bound to on this platform (Ctrl+Y on the offscreen
	// theme, Ctrl+Shift+Z where that is the binding), so the test follows the shortcut the user has.
	static void pressShortcut(QAction* action)
	{
		QVERIFY(!action->shortcut().isEmpty());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		const QKeyCombination combination = action->shortcut()[0];
		const Qt::Key key = combination.key();
		const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
#else
		const int combined = action->shortcut()[0];
		const Qt::Key key = static_cast<Qt::Key>(combined & ~Qt::KeyboardModifierMask);
		const Qt::KeyboardModifiers modifiers = static_cast<Qt::KeyboardModifiers>(combined & Qt::KeyboardModifierMask);
#endif
		QTest::keyClick(getGUI()->mainWindow(), key, modifiers);
	}

	// Opening the Edit menu is what refreshes its Undo/Redo enablement, so open it the same way.
	static void openEditMenu(QAction* action)
	{
		auto* menu = qobject_cast<QMenu*>(action->parent());
		QVERIFY(menu != nullptr);
		QMetaObject::invokeMethod(menu, "aboutToShow");
	}

	QTemporaryDir m_home;
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"