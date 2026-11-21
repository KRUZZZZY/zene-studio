/*
 * CollabPanelTest.cpp - collab.show_panel opens the collaboration panel over the real main window
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
/*! 0.5.0 F6: collab.show_panel shows the panel beside the REAL main window (GuiApplication: the panel has no
 *  window to sit beside without it), creating it once and showing the same panel on a second call.
 *  The panel reads the idle session: Host is offered, Stop hosting is not.
 *  gui-1106: like every other non-modal panel (window.tempo_map, window.undo_history, ...) it declares no
 *  `requires: display` - an agent or offscreen GUI session (the registry's headless flag) still has the
 *  main window, and Edit > Collaborate... discards the command's result, so a refusal there was a menu
 *  that closed and showed nothing. With no main window the handler itself answers `requires`. */

#include <QtTest>

#include <QPushButton>
#include <QLineEdit>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "CollabInvite.h"
#include "CollabPanel.h"
#include "CollabSession.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! The panel's button labelled @a text, or nullptr.
QPushButton* buttonOf(QWidget* panel, const QString& text)
{
	for (QPushButton* button : panel->findChildren<QPushButton*>())
	{
		if (button->text() == text) { return button; }
	}
	return nullptr;
}

} // namespace

class CollabPanelTest : public QObject
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

	void theCommandShowsThePanel()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->show();
		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();

		registry->setHeadless(true);  // an agent / offscreen GUI session: the main window is still there
		const ControlResult headlessShown = registry->invoke(QStringLiteral("collab.show_panel"));
		registry->setHeadless(false);
		const ControlResult shown = registry->invoke(QStringLiteral("collab.show_panel"));
		const ControlResult again = registry->invoke(QStringLiteral("collab.show_panel"));
		registry->setHeadless(headless);

		QVERIFY2(headlessShown.ok, qPrintable(headlessShown.errorMessage));
		QVERIFY(headlessShown.result.value(QStringLiteral("shown")).toBool());
		QVERIFY2(shown.ok, qPrintable(shown.errorMessage));
		QVERIFY(shown.result.value(QStringLiteral("shown")).toBool());
		QVERIFY2(again.ok, qPrintable(again.errorMessage));

		const QList<CollabPanel*> panels = window->findChildren<CollabPanel*>();
		QCOMPARE(panels.size(), 1);  // made on first use, shown again after
		CollabPanel* panel = panels.first();
		QVERIFY(panel->isVisible());
		QCOMPARE(panel->accessibleName(), QStringLiteral("Collaboration panel"));

		QPushButton* host = buttonOf(panel, QStringLiteral("Host..."));
		QPushButton* stop = buttonOf(panel, QStringLiteral("Stop hosting"));
		QVERIFY(host != nullptr && stop != nullptr);
		QVERIFY(host->isEnabled());   // idle: hosting is on offer
		QVERIFY(!stop->isEnabled());  // and there is nothing to stop
		panel->close();
	}

	//! gui-1106: Edit > Collaborate... in an agent / offscreen GUI session opens the panel. The menu runs the
	//! command and discards its result, so a refusal was invisible: the menu closed and nothing appeared.
	void theEditMenuOpensThePanelInAHeadlessSession()
	{
		MainWindow* window = getGUI()->mainWindow();
		for (CollabPanel* existing : window->findChildren<CollabPanel*>()) { existing->close(); }
		QAction* collaborate = nullptr;
		for (QAction* action : window->findChildren<QAction*>())
		{
			if (action->property("controlCommand").toString() == QStringLiteral("collab.show_panel")
				&& action->text() == MainWindow::tr("Collaborate...")) { collaborate = action; }
		}
		QVERIFY2(collaborate != nullptr, "Edit > Collaborate... is not in the main window");
		QVERIFY(collaborate->isEnabled());

		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();
		registry->setHeadless(true);  // what --control-socket or QT_QPA_PLATFORM=offscreen makes it
		collaborate->trigger();
		registry->setHeadless(headless);

		const QList<CollabPanel*> panels = window->findChildren<CollabPanel*>();
		QCOMPARE(panels.size(), 1);
		CollabPanel* panel = panels.first();
		QVERIFY2(panel->isVisible(), "the menu closed but no Collaborate window appeared");
		QCOMPARE(panel->windowTitle(), CollabPanel::tr("Collaborate"));
		QPushButton* host = buttonOf(panel, QStringLiteral("Host..."));
		QPushButton* join = buttonOf(panel, QStringLiteral("Join"));
		QVERIFY(host != nullptr && join != nullptr);
		QVERIFY(host->isEnabled());  // idle session: host and join controls are offered
		panel->close();
	}

	void joinKeepsInviteWhenConnectionFails()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();
		registry->setHeadless(false);
		const ControlResult shown = registry->invoke(QStringLiteral("collab.show_panel"));
		registry->setHeadless(headless);
		QVERIFY2(shown.ok, qPrintable(shown.errorMessage));
		CollabPanel* panel = getGUI()->mainWindow()->findChild<CollabPanel*>();
		QVERIFY(panel != nullptr);
		QLineEdit* inviteField = panel->findChild<QLineEdit*>();
		QPushButton* join = buttonOf(panel, QStringLiteral("Join"));
		QVERIFY(inviteField != nullptr && join != nullptr);

		QTcpServer offlineHost;
		QVERIFY(offlineHost.listen(QHostAddress::LocalHost, 0));
		connect(&offlineHost, &QTcpServer::newConnection, &offlineHost, [&offlineHost] {
			while (offlineHost.hasPendingConnections())
			{
				QTcpSocket* connection = offlineHost.nextPendingConnection();
				connection->abort();
				connection->deleteLater();
			}
		});
		collab::Invite offlineInvite;
		offlineInvite.address = QHostAddress::LocalHost;
		offlineInvite.port = offlineHost.serverPort();
		offlineInvite.key = QByteArray(collab::InviteKeyBytes, 'x');
		const QString inviteText = offlineInvite.encode();
		inviteField->setText(inviteText);

		const auto cleanup = qScopeGuard([panel] {
			if (collab::CollabSession::instance()->mode() == collab::CollabSession::Mode::Joined)
			{
				collab::CollabSession::instance()->leave();
			}
			panel->close();
		});
		join->click();
		QCOMPARE(collab::CollabSession::instance()->state().value(QStringLiteral("peer")).toObject()
			.value(QStringLiteral("state")).toString(), QStringLiteral("connecting"));
		QTRY_COMPARE_WITH_TIMEOUT(collab::CollabSession::instance()->state().value(QStringLiteral("peer")).toObject()
			.value(QStringLiteral("state")).toString(), QStringLiteral("failed"), 5000);
		QCOMPARE(inviteField->text(), inviteText);
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(CollabPanelTest)
#include "CollabPanelTest.moc"
