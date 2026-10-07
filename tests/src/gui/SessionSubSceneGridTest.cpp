/*
 * SessionSubSceneGridTest.cpp - sub-scenes from the clip-launch grid: select, make, launch (WF-064)
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

/*! 0.5.0 L02 (WF-064) over the REAL application (GuiApplication): in the clip launcher's grid, Ctrl+click and
 *  Ctrl+Space select cells without launching them; Sub-scenes > Make a Sub-scene From the Selection makes one from the
 *  selected cells of one scene (one undo step), and a scene's Sub-scenes menu launches - only those columns - and
 *  deletes it, every entry a registered command. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MenuTestSupport.h"
#include "SessionGridView.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::leafActions;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The scene the engine reports column @a track playing, or -1 when it is idle.
int playingScene(int track)
{
	for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result.value(QStringLiteral("columns")).toArray())
	{
		const QJsonObject column = value.toObject();
		if (column.value(QStringLiteral("track")).toInt() == track && column.value(QStringLiteral("phase")).toString() == QLatin1String("playing"))
		{
			return column.value(QStringLiteral("scene")).toInt();
		}
	}
	return -1;
}

QJsonArray subScenes()
{
	return run(QStringLiteral("session.get_state")).result.value(QStringLiteral("subscenes")).toArray();
}

} // namespace

class SessionSubSceneGridTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		guitest::startGui(m_dir);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		getGUI()->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(getGUI()->mainWindow()));
		ControlRegistry::setReady(true);
		while (run(QStringLiteral("track.list")).result.value(QStringLiteral("count")).toInt() < 3)
		{
			QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		}
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 3}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		for (int track = 0; track < 3; ++track)
		{
			QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), track}, {QStringLiteral("scene"), 1},
				{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), QStringLiteral("c%1.wav").arg(track)},
				{QStringLiteral("name"), QStringLiteral("c%1").arg(track)}}).ok);
		}
		QTest::qWait(100);  // the track views are made from queued calls, as the event loop would make them
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("session.stop_all"));
		QTest::qWait(100);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Ctrl+click and Ctrl+Space select (and deselect) a cell, launch nothing, and say so - not by colour alone.
	void selectingACellLaunchesNothing()
	{
		SessionGridView* grid = getGUI()->mainWindow()->clipLauncher();
		grid->refresh();
		QTest::mouseClick(grid->cellButton(0, 1), Qt::LeftButton, Qt::ControlModifier);
		QTest::mouseClick(grid->cellButton(2, 1), Qt::LeftButton, Qt::ControlModifier);
		QVERIFY(grid->isCellSelected(0, 1) && grid->isCellSelected(2, 1) && !grid->isCellSelected(1, 1));
		QTest::qWait(200);
		QCOMPARE(playingScene(0), -1);
		QCOMPARE(playingScene(2), -1);
		QVERIFY2(grid->cellButton(0, 1)->accessibleName().endsWith(QStringLiteral(", selected")), qPrintable(grid->cellButton(0, 1)->accessibleName()));
		QVERIFY(grid->cellButton(0, 1)->styleSheet().contains(QStringLiteral("dotted")));
		QVERIFY(grid->cellButton(1, 1)->styleSheet().isEmpty());
		QVERIFY2(grid->statusLine().contains(QStringLiteral("2 cell")), qPrintable(grid->statusLine()));
		QCOMPARE(grid->selection().value(QStringLiteral("scene")).toInt(), 1);
		QCOMPARE(grid->selection().value(QStringLiteral("tracks")).toArray(), (QJsonArray{0, 2}));

		grid->show();
		QVERIFY(QTest::qWaitForWindowExposed(grid->window()));
		grid->cellButton(1, 0)->setFocus();
		QTest::keyClick(grid->cellButton(1, 0), Qt::Key_Space, Qt::ControlModifier);
		QVERIFY(grid->isCellSelected(1, 0));
		QVERIFY(grid->selection().isEmpty());  // two scenes: no sub-scene to make
		QMenu mixed;
		grid->populateMenu(&mixed, grid->cellButton(0, 1));
		QVERIFY(!menuAction(&mixed, {QStringLiteral("Sub-scenes"), QStringLiteral("Make a Sub-scene From the Selection")})->isEnabled());
		QTest::keyClick(grid->cellButton(1, 0), Qt::Key_Space, Qt::ControlModifier);
		QVERIFY(!grid->isCellSelected(1, 0));
	}

	//! Make one from the selection (one undo step); a scene's menu launches only its columns, then deletes it.
	void theMenusMakeLaunchAndDeleteASubScene()
	{
		SessionGridView* grid = getGUI()->mainWindow()->clipLauncher();
		grid->refresh();
		QMenu cell;
		grid->populateMenu(&cell, grid->cellButton(2, 1));
		for (QAction* action : leafActions(&cell))
		{
			QVERIFY2(ControlRegistry::instance()->hasCommand(action->property("controlCommand").toString()), qPrintable(action->text()));
		}
		QAction* make = menuAction(&cell, {QStringLiteral("Sub-scenes"), QStringLiteral("Make a Sub-scene From the Selection")});
		QVERIFY(make != nullptr && make->isEnabled());
		QCOMPARE(make->property("controlCommand").toString(), QStringLiteral("session.subscene_create"));
		make->trigger();
		QCOMPARE(subScenes().size(), 1);
		const QJsonObject made = subScenes().first().toObject();
		QCOMPARE(made.value(QStringLiteral("scene")).toInt(), 1);
		QCOMPARE(made.value(QStringLiteral("tracks")).toArray(), (QJsonArray{0, 2}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(subScenes().isEmpty());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(subScenes().size(), 1);

		grid->refresh();
		QMenu scene;
		grid->populateMenu(&scene, grid->sceneButton(1));
		QVERIFY(menuAction(&scene, {QStringLiteral("Sub-scenes"), QStringLiteral("Rename Sub-scene 1...")}) != nullptr);
		QAction* launch = menuAction(&scene, {QStringLiteral("Sub-scenes"), QStringLiteral("Launch Sub-scene 1")});
		QVERIFY(launch != nullptr);
		launch->trigger();
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), 1, 3000);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(2), 1, 3000);
		QCOMPARE(playingScene(1), -1);  // not in the sub-scene
		QMenu other;
		grid->populateMenu(&other, grid->sceneButton(0));
		QVERIFY2(menuAction(&other, {QStringLiteral("Sub-scenes"), QStringLiteral("Launch Sub-scene 1")}) == nullptr, "listed under another scene");
		menuAction(&scene, {QStringLiteral("Sub-scenes"), QStringLiteral("Delete Sub-scene 1")})->trigger();
		QVERIFY(subScenes().isEmpty());
		run(QStringLiteral("session.stop_all"));
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), -1, 3000);
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(SessionSubSceneGridTest)
#include "SessionSubSceneGridTest.moc"
