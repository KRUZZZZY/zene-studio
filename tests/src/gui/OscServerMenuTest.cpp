/*
 * OscServerMenuTest.cpp - Customise > OSC Control Server drives osc.* (Q050-205)
 *                                       reversibility contract (0.5.0 F6)
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

/*! The GUI entry point of the OSC server (Q050-205): Customise > OSC Control Server > Start... opens the
 *  command's own form (address, port, scope), Run starts the server through osc.start; State and Stop run
 *  osc.get_state and osc.stop. The test clicks the real actions and the form's Run button. */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QMenu>
#include <QPushButton>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MenuTestSupport.h"
#include "OscServer.h"
#include "ShellCommandMenus.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

class OscServerMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		osc::OscServer::stop();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theMenuStartsReadsAndStopsTheServerThroughItsCommands()
	{
		QMenu menu;
		ShellCommandMenus::populateCustomise(&menu);
		const QStringList base{QStringLiteral("OSC Control Server")};
		QAction* start = menuAction(&menu, base + QStringList{QStringLiteral("Start...")});
		QAction* state = menuAction(&menu, base + QStringList{QStringLiteral("State")});
		QAction* stop = menuAction(&menu, base + QStringList{QStringLiteral("Stop")});
		QVERIFY2(start != nullptr && state != nullptr && stop != nullptr, "Customise > OSC Control Server is incomplete");
		QCOMPARE(start->property("controlCommand").toString(), QStringLiteral("osc.start"));
		QCOMPARE(state->property("controlCommand").toString(), QStringLiteral("osc.get_state"));
		QCOMPARE(stop->property("controlCommand").toString(), QStringLiteral("osc.stop"));
		QVERIFY(start->isEnabled());
		QVERIFY(osc::OscServer::instance() == nullptr);

		start->trigger();
		CommandForm* form = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((form = [] {
			for (QWidget* widget : QApplication::topLevelWidgets())
			{
				if (auto* candidate = qobject_cast<CommandForm*>(widget); candidate != nullptr && candidate->isVisible())
				{
					return candidate;
				}
			}
			return static_cast<CommandForm*>(nullptr);
		}()) != nullptr, 3000);
		QVERIFY(form->fields().contains(QStringLiteral("port")));
		QVERIFY(form->fields().contains(QStringLiteral("address")));
		QVERIFY(form->setField(QStringLiteral("port"), 0));  // any free port: the test must not collide
		QVERIFY(osc::OscServer::instance() == nullptr);  // the form alone opens nothing
		QPushButton* runButton = nullptr;
		for (QPushButton* button : form->findChildren<QPushButton*>())
		{
			if (button->text().remove(QLatin1Char('&')) == QStringLiteral("Run")) { runButton = button; }
		}
		QVERIFY(runButton != nullptr);
		QTest::mouseClick(runButton, Qt::LeftButton);
		QTRY_VERIFY_WITH_TIMEOUT(osc::OscServer::instance() != nullptr, 3000);
		const QJsonObject running = osc::OscServer::instance()->state();
		QCOMPARE(running.value(QStringLiteral("address")).toString(), QStringLiteral("127.0.0.1"));
		QCOMPARE(running.value(QStringLiteral("scope")).toString(), QStringLiteral("remote"));
		QVERIFY(running.value(QStringLiteral("port")).toInt() > 0);
		form->close();

		state->trigger();  // a read: answered, nothing changes
		QVERIFY(osc::OscServer::instance() != nullptr);
		stop->trigger();
		QVERIFY(osc::OscServer::instance() == nullptr);
		const ControlResult after = ControlRegistry::instance()->invoke(QStringLiteral("osc.get_state"));
		QCOMPARE(after.result.value(QStringLiteral("running")).toBool(), false);
	}
};

QTEST_MAIN(OscServerMenuTest)
#include "OscServerMenuTest.moc"
