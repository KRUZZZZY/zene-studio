/*
 * SessionGridViewTest.cpp - R5.3: the clip-launch grid, driven like a person drives it
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

/*! The plan's acceptance for R5.3: launch, stop and scene-launch FROM THE GRID produce the
 *  socket-visible state the commands produce - the engine's own per-column reading in
 *  session.get_state - and every cell is focusable and announced. The engine is the real
 *  one (Engine::init(true) runs the Dummy device), so a launch here is a real launch.
 */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QToolButton>

#include "ControlRegistry.h"
#include "Engine.h"
#include "MenuTestSupport.h"
#include "SessionGridView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The scene the engine reports column @a track on, or -1 when it is idle.
int playingScene(int track)
{
	for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result
			.value(QStringLiteral("columns")).toArray())
	{
		const QJsonObject column = value.toObject();
		if (column.value(QStringLiteral("track")).toInt() == track
			&& column.value(QStringLiteral("phase")).toString() == QLatin1String("playing"))
		{
			return column.value(QStringLiteral("scene")).toInt();
		}
	}
	return -1;
}

QJsonObject slotState(int track, int scene)
{
	for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result.value(QStringLiteral("slots")).toArray())
	{
		const QJsonObject slot = value.toObject();
		if (slot.value(QStringLiteral("track")).toInt() == track && slot.value(QStringLiteral("scene")).toInt() == scene) { return slot; }
	}
	return {};
}

QJsonObject sceneState(int scene)
{
	return run(QStringLiteral("session.get_state")).result.value(QStringLiteral("scenes")).toArray().at(scene).toObject();
}

} // namespace

using lmms::test::leafActions;
using lmms::test::menuAction;

class SessionGridViewTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		while (run(QStringLiteral("track.list")).result.value(QStringLiteral("count")).toInt() < 2)
		{
			QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		}
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		for (const auto& [track, name] : {std::pair{0, QStringLiteral("kick")}, std::pair{1, QStringLiteral("bass")}})
		{
			QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), track}, {QStringLiteral("scene"), 0},
				{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), name + QStringLiteral(".wav")},
				{QStringLiteral("name"), name}}).ok);
		}
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("session.stop_all"));
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void everyCellIsFocusableAndAnnounced()
	{
		SessionGridView view;
		view.refresh();
		QVERIFY(view.cellButton(0, 0)->text().contains(QStringLiteral("kick")));
		QCOMPARE(view.cellButton(0, 0)->accessibleName(), QStringLiteral("Track 1, scene 1: kick"));
		QCOMPARE(view.cellButton(1, 1)->accessibleName(), QStringLiteral("Track 2, scene 2: empty"));
		for (int track = 0; track < 2; ++track)
		{
			for (int scene = 0; scene < 2; ++scene)
			{
				QCOMPARE(view.cellButton(track, scene)->focusPolicy(), Qt::StrongFocus);
				QCOMPARE(view.cellButton(track, scene)->property("controlCommand").toString(),
					QStringLiteral("session.launch_slot"));
			}
		}
		QCOMPARE(view.sceneButton(0)->property("controlCommand").toString(), QStringLiteral("session.launch_scene"));
		QCOMPARE(view.stopAllButton()->property("controlCommand").toString(), QStringLiteral("session.stop_all"));
	}

	void theArrowKeysMoveBetweenCells()
	{
		SessionGridView view;
		view.show();
		QVERIFY(QTest::qWaitForWindowExposed(&view));
		view.cellButton(0, 0)->setFocus();
		QTest::keyClick(view.cellButton(0, 0), Qt::Key_Down);
		QTRY_VERIFY(view.cellButton(0, 1)->hasFocus());
		QTest::keyClick(view.cellButton(0, 1), Qt::Key_Right);
		QTRY_VERIFY(view.cellButton(1, 1)->hasFocus());
	}

	void theGridLaunchesAndStopsWhatTheSocketReports()
	{
		SessionGridView view;
		view.refresh();
		QTest::mouseClick(view.sceneButton(0), Qt::LeftButton);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), 0, 3000);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(1), 0, 3000);
		QTRY_VERIFY_WITH_TIMEOUT(view.cellButton(0, 0)->isChecked(), 3000);
		QVERIFY(view.cellButton(0, 0)->accessibleName().endsWith(QStringLiteral("playing")));

		QTest::mouseClick(view.stopButton(0), Qt::LeftButton);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), -1, 3000);
		QCOMPARE(playingScene(1), 0);

		// Enter on a focused cell launches it, as a click does.
		view.show();
		QVERIFY(QTest::qWaitForWindowExposed(&view));
		view.cellButton(0, 0)->setFocus();
		QTest::keyClick(view.cellButton(0, 0), Qt::Key_Return);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), 0, 3000);

		QTest::mouseClick(view.stopAllButton(), Qt::LeftButton);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), -1, 3000);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(1), -1, 3000);
		QTRY_VERIFY_WITH_TIMEOUT(!view.cellButton(0, 0)->isChecked(), 3000);
	}

	/*! 0.5.0 W1 (WF-054/058/059/062): the right-click menus edit the launcher through the
	 *  session.* commands - every entry names a registered command, a pick from a list writes
	 *  what session.get_state then reports, and an entry with nothing to act on is disabled. */
	void theMenusEditTheLauncherThroughTheRegistry()
	{
		SessionGridView view;
		view.refresh();
		for (QToolButton* button : {view.cellButton(0, 0), view.sceneButton(0), static_cast<QToolButton*>(nullptr)})
		{
			QMenu menu;
			view.populateMenu(&menu, button);
			QVERIFY(!leafActions(&menu).isEmpty());
			for (QAction* action : leafActions(&menu))
			{
				const QString id = action->property("controlCommand").toString();
				QVERIFY2(ControlRegistry::instance()->hasCommand(id), qPrintable(action->text() + QStringLiteral(" -> ") + id));
			}
		}

		QMenu cell;
		view.populateMenu(&cell, view.cellButton(0, 0));
		menuAction(&cell, {QStringLiteral("Launch Mode"), QStringLiteral("gate")})->trigger();
		QCOMPARE(slotState(0, 0).value(QStringLiteral("mode")).toString(), QStringLiteral("gate"));
		menuAction(&cell, {QStringLiteral("Launch Mode"), QStringLiteral("trigger")})->trigger();
		QCOMPARE(slotState(0, 0).value(QStringLiteral("mode")).toString(), QStringLiteral("trigger"));
		QVERIFY(menuAction(&cell, {QStringLiteral("Clear Cell")})->isEnabled());
		QMenu empty;
		view.populateMenu(&empty, view.cellButton(1, 1));
		QVERIFY2(!menuAction(&empty, {QStringLiteral("Clear Cell")})->isEnabled(), "an empty cell offered Clear");

		QMenu scene;
		view.populateMenu(&scene, view.sceneButton(1));
		QVERIFY(!menuAction(&scene, {QStringLiteral("Tempo Override Off")})->isEnabled());
		QVERIFY(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 1}, {QStringLiteral("tempo"), 150}}).ok);
		view.refresh();
		QMenu overridden;
		view.populateMenu(&overridden, view.sceneButton(1));
		QAction* off = menuAction(&overridden, {QStringLiteral("Tempo Override Off")});
		QVERIFY(off->isEnabled());
		off->trigger();
		QCOMPARE(sceneState(1).value(QStringLiteral("tempo_enabled")).toBool(), false);

		QMenu grid;
		view.populateMenu(&grid, nullptr);
		menuAction(&grid, {QStringLiteral("Launch Quantisation"), QStringLiteral("bar")})->trigger();
		QCOMPARE(run(QStringLiteral("session.get_state")).result.value(QStringLiteral("quantisation")).toString(), QStringLiteral("bar"));
		menuAction(&grid, {QStringLiteral("Launch Quantisation"), QStringLiteral("none")})->trigger();
		QCOMPARE(run(QStringLiteral("session.get_state")).result.value(QStringLiteral("quantisation")).toString(), QStringLiteral("none"));
		QCOMPARE(view.gridButton()->text(), QStringLiteral("Grid..."));
	}

	//! WF-062 / WF-054 (review W1-1 #5, #8): a scene has a colour the grid paints, and a cell
	//! takes a pattern by name from the Play Pattern menu.
	void aSceneColourIsPaintedAndACellTakesAPatternByName()
	{
		const ControlResult coloured = run(QStringLiteral("session.set_scene"),
			{{QStringLiteral("scene"), 0}, {QStringLiteral("color"), QStringLiteral("#00aaff")}});
		QVERIFY2(coloured.ok, qPrintable(coloured.errorMessage));
		QCOMPARE(sceneState(0).value(QStringLiteral("color")).toString(), QStringLiteral("#00aaff"));
		QCOMPARE(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 0}, {QStringLiteral("color"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		SessionGridView view;
		view.refresh();
		QVERIFY(view.sceneButton(0)->styleSheet().contains(QStringLiteral("#00aaff")));
		QVERIFY(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 0}, {QStringLiteral("color"), QString()}}).ok);
		view.refresh();
		QVERIFY(view.sceneButton(0)->styleSheet().isEmpty());

		const QJsonArray patterns = run(QStringLiteral("session.pattern_list")).result.value(QStringLiteral("patterns")).toArray();
		QMenu cell;
		view.populateMenu(&cell, view.cellButton(1, 1));
		QAction* picker = menuAction(&cell, {QStringLiteral("Play Pattern")});
		QVERIFY(picker != nullptr);
		QCOMPARE(picker->menu()->actions().size(), patterns.size());
		if (patterns.isEmpty()) { return; }  // a project with no pattern: the menu is empty and disabled
		picker->menu()->actions().first()->trigger();
		QCOMPARE(slotState(1, 1).value(QStringLiteral("type")).toString(), QStringLiteral("midi"));
		QCOMPARE(slotState(1, 1).value(QStringLiteral("pattern")).toInt(), patterns.first().toObject().value(QStringLiteral("pattern")).toInt());
		QVERIFY(run(QStringLiteral("session.clear_slot"), {{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 1}}).ok);
	}
};

QTEST_MAIN(SessionGridViewTest)
#include "SessionGridViewTest.moc"
