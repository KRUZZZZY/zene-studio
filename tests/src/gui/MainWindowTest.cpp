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

#include <QWidget>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControllerRackView.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
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

	void anUnattendedFirstRunDoesNotRecordTheSetupAsDone()
	{
		// This run never showed the setup dialog (tests run on the offscreen platform, which
		// isUnattendedRun() reports as having no display), so nothing was configured by the
		// user: the next interactive launch must still be offered the dialog.
		QCOMPARE(ConfigManager::inst()->value(QStringLiteral("app"), QStringLiteral("configured")).toInt(), 0);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"