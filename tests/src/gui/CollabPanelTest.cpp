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
/*! 0.5.0 F6: collab.show_panel declares requires: display, so a headless instance refuses it, typed;
 *  with a display it shows the panel beside the REAL main window (GuiApplication: the panel has no
 *  window to sit beside without it), creating it once and showing the same panel on a second call.
 *  The panel reads the idle session: Host is offered, Stop hosting is not. */

#include <QtTest>

#include <QPushButton>
#include <QTemporaryDir>

#include "CollabPanel.h"
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

		registry->setHeadless(true);  // an agent instance: no person to show a panel to
		const ControlResult refused = registry->invoke(QStringLiteral("collab.show_panel"));
		registry->setHeadless(false);  // the offscreen platform is a display (requires: display)
		const ControlResult shown = registry->invoke(QStringLiteral("collab.show_panel"));
		const ControlResult again = registry->invoke(QStringLiteral("collab.show_panel"));
		registry->setHeadless(headless);

		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::Requires);
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

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(CollabPanelTest)
#include "CollabPanelTest.moc"
