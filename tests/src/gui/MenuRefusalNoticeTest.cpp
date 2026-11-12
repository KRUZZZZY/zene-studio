/*! A menu action whose command the registry refuses must say so (cluster gui-1030-menu-edit-collaborate).
 *
 *  Edit > Collaborate... dispatches collab.show_panel, which declares `requires: display` and so is
 *  refused in an unattended instance (ControlRegistry::checkRequires).  dispatchShellCommand threw the
 *  ControlResult away: the menu closed and nothing - no panel, no reason - was left for the user.
 *
 *  Held here: the real Collaborate action's refusal is said in the main window's status bar (never a
 *  modal box: an unattended instance cannot answer one); the sibling dispatch of any other refused or
 *  unknown command is said the same way; and the paths that work stay quiet.
 */

#include <QtTest>

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlWindowCommands.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"

using namespace lmms;
using namespace lmms::gui;

class MenuRefusalNoticeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		guitest::startGui(m_dir);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The finding: an unattended instance refuses collab.show_panel (requires: display); the click
	//! has to leave the reason where it can be read.
	void aRefusedCollaborateClickIsSaidInTheStatusBar()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->show();
		QAction* collaborate = menuActionFor(QStringLiteral("collab.show_panel"));
		QVERIFY(collaborate != nullptr);
		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();
		registry->setHeadless(true);
		window->statusBar()->clearMessage();

		collaborate->trigger();

		registry->setHeadless(headless);
		const QString message = window->statusBar()->currentMessage();
		QVERIFY2(message.contains(QStringLiteral("collab.show_panel")), qPrintable(message));
		QVERIFY2(message.contains(QStringLiteral("display")), qPrintable(message));
		QVERIFY(visiblePanel(window) == nullptr);
	}

	//! The path that works stays quiet: the panel opens and no failure is claimed.
	void anAttendedCollaborateClickShowsThePanelQuietly()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->show();
		QAction* collaborate = menuActionFor(QStringLiteral("collab.show_panel"));
		QVERIFY(collaborate != nullptr);
		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();
		registry->setHeadless(false);  // the offscreen platform is a display (requires: display)
		window->statusBar()->clearMessage();

		collaborate->trigger();

		registry->setHeadless(headless);
		QVERIFY2(window->statusBar()->currentMessage().isEmpty(), qPrintable(window->statusBar()->currentMessage()));
		QWidget* panel = visiblePanel(window);
		QVERIFY(panel != nullptr);
		panel->close();
	}

	//! The sibling case: the one dispatch every menu action shares says why any command did not run.
	void anyRefusedDispatchIsSaid()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->statusBar()->clearMessage();

		dispatchShellCommand(QStringLiteral("no.such_command"));

		const QString message = window->statusBar()->currentMessage();
		QVERIFY2(message.contains(QStringLiteral("no.such_command")), qPrintable(message));
	}

private:
	//! The collaboration panel when it is on screen (found by its accessible name, as CollabPanelTest does).
	static QWidget* visiblePanel(MainWindow* window)
	{
		for (QWidget* child : window->findChildren<QWidget*>())
		{
			if (child->isVisible() && child->accessibleName() == QStringLiteral("Collaboration panel")) { return child; }
		}
		return nullptr;
	}

	//! The menu entry itself - found through the command it declares, not by its label.
	QAction* menuActionFor(const QString& commandId) const
	{
		const QList<QAction*> menus = getGUI()->mainWindow()->menuBar()->actions();
		for (QAction* menuAction : menus)
		{
			QMenu* menu = menuAction->menu();
			if (menu == nullptr) { continue; }
			const QList<QAction*> entries = menu->actions();
			for (QAction* entry : entries)
			{
				if (entry->property("controlCommand").toString() == commandId) { return entry; }
			}
		}
		return nullptr;
	}

	QTemporaryDir m_dir;
};

QTEST_MAIN(MenuRefusalNoticeTest)
#include "MenuRefusalNoticeTest.moc"
