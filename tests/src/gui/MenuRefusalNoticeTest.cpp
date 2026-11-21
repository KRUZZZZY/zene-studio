/*! A menu action whose command the registry refuses must say so (cluster gui-1030-menu-edit-collaborate).
 *
 *  dispatchShellCommand threw the ControlResult away: the menu closed and nothing - no panel, no
 *  reason - was left for the user. It now says a refusal in the main window's status bar (never a
 *  modal box: an unattended instance cannot answer one).
 *
 *  Held here: a refused dispatch from a menu action is said; the sibling dispatch of any other refused
 *  or unknown command is said the same way; and the paths that work stay quiet.
 *
 *  Collaborate's own contract differs along the train, and this test follows the build instead of
 *  freezing one line's answer. Before gui-1106 `collab.show_panel` declares `requires: display`, so an
 *  unattended click is refused and the reason must be reported; gui-1106 removes that declaration (the
 *  panel is non-modal), so on a line carrying it - this target - the same click opens the panel and
 *  says nothing. Either way a dropped failure is caught: the two refusal slots below use commands that
 *  refuse on every line (`app.online_help` declares `display` and `human`) and an unknown id.
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

	//! The finding, on a line where `collab.show_panel` still declares `requires: display` (this target):
	//! an unattended click is refused and the click has to leave the reason where it can be read. On a
	//! line that removed the declaration (gui-1106) the same click opens the panel and says nothing -
	//! the branch below holds that half, so this file never claims a refusal the build does not make.
	void aCollaborateClickOnAnUnattendedInstanceIsAccountedFor()
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
		const ControlCommand* panel = registry->command(QStringLiteral("collab.show_panel"));
		QVERIFY(panel != nullptr);
		if (panel->requiresDecl.contains(QStringLiteral("display")))
		{
			QVERIFY2(message.contains(QStringLiteral("collab.show_panel")), qPrintable(message));
			QVERIFY2(message.contains(QStringLiteral("display")), qPrintable(message));
			QVERIFY(visiblePanel(window) == nullptr);
			return;
		}
		// gui-1106: no display requirement - the panel is non-modal and opens even here.
		QVERIFY2(message.isEmpty(), qPrintable(message));
		QWidget* shown = visiblePanel(window);
		QVERIFY2(shown != nullptr, "the menu closed but no Collaborate window appeared");
		shown->close();
	}

	//! Every line refuses this one: `app.online_help` declares BOTH `display` and `human` (the menu entry
	//! calls the implementation directly, because a human requirement refuses every caller). With the
	//! registry unattended the command refuses, and the shared dispatch has to report it - this is the
	//! regression for the dropped result, independent of Collaborate's contract.
	void aRefusedOnlineHelpDispatchIsSaid()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->show();
		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();
		registry->setHeadless(true);
		window->statusBar()->clearMessage();

		dispatchShellCommand(QStringLiteral("app.online_help"));

		registry->setHeadless(headless);
		const QString message = window->statusBar()->currentMessage();
		QVERIFY2(message.contains(QStringLiteral("app.online_help")), qPrintable(message));
		QVERIFY2(message.contains(QStringLiteral("requires")), qPrintable(message));
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
