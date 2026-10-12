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

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUrl>

#include "ControlRegistry.h"
#include "Engine.h"
#include "MenuTestSupport.h"
#include "SessionGridCells.h"
#include "SessionGridView.h"
#include "Song.h"
#include "Track.h"

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
	//! takes a pattern by name from the Set Pattern menu.
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
		QAction* picker = menuAction(&cell, {QStringLiteral("Set Pattern")});
		QVERIFY(picker != nullptr);
		QCOMPARE(picker->menu()->actions().size(), patterns.size());
		if (patterns.isEmpty()) { return; }  // a project with no pattern: the menu is empty and disabled
		picker->menu()->actions().first()->trigger();
		QCOMPARE(slotState(1, 1).value(QStringLiteral("type")).toString(), QStringLiteral("midi"));
		QCOMPARE(slotState(1, 1).value(QStringLiteral("pattern")).toInt(), patterns.first().toObject().value(QStringLiteral("pattern")).toInt());
		QVERIFY(run(QStringLiteral("session.clear_slot"), {{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 1}}).ok);
	}

	//! 0.5.0 theme 14: a scene's state is its row's - queued over playing over stopped over empty - and a cell's
	//! play mark says its column's phase by its shape.
	void theSceneStateIsItsRowsAndTheMarksAreShapes()
	{
		using sessiongrid::SceneState;
		const std::vector<QString> names{QStringLiteral("kick"), QString(), QStringLiteral("bass"), QString()};  // 2 x 2
		const std::vector<QString> idle{QString(), QString()};
		QCOMPARE(sessiongrid::sceneState(0, 2, names, {-1, -1}, idle), SceneState::Stopped);
		QCOMPARE(sessiongrid::sceneState(1, 2, names, {-1, -1}, idle), SceneState::Empty);
		QCOMPARE(sessiongrid::sceneState(0, 2, names, {0, -1}, {QStringLiteral("playing"), QString()}), SceneState::Playing);
		QCOMPARE(sessiongrid::sceneState(0, 2, names, {0, 0}, {QStringLiteral("playing"), QStringLiteral("launching")}),
			SceneState::Queued);
		QCOMPARE(sessiongrid::sceneState(2, 2, names, {-1, -1}, idle), SceneState::Empty);
		QCOMPARE(sessiongrid::sceneState(-1, 2, names, {-1, -1}, {QStringLiteral("playing"), QString()}), SceneState::Empty);
		QCOMPARE(sessiongrid::sceneState(0, 0, names, {0, -1}, {QStringLiteral("playing"), QString()}), SceneState::Empty);
		QVERIFY(sessiongrid::cellMark(QString(), false).isEmpty());  // an empty cell has nothing to launch
		QVERIFY(sessiongrid::cellMark(QString(), true) != sessiongrid::cellMark(QStringLiteral("playing"), true));
		QVERIFY(sessiongrid::cellMark(QStringLiteral("launching"), true) != sessiongrid::cellMark(QStringLiteral("playing"), true));
		const int songTracks = static_cast<int>(Engine::getSong()->tracks().size());
		QCOMPARE(sessiongrid::columnTitle(songTracks), QStringLiteral("Track %1").arg(songTracks + 1));  // no such track
	}

	//! 0.5.0 theme 14: the columns are headed by their tracks' names; the scene buttons and the filled cells carry a
	//! play mark and say their state; a cell in a playing column says what launching it replaces.
	void theColumnsAreNamedAndTheScenesShowTheirState()
	{
		Track* first = Engine::getSong()->tracks().front();
		const QString kept = first->name();
		first->setName(QStringLiteral("Drums"));
		SessionGridView view;
		view.refresh();
		QCOMPARE(view.columnHeader(0)->accessibleName(), QStringLiteral("Track 1: Drums"));
		QVERIFY(view.columnHeader(0)->text().startsWith(QStringLiteral("Dr")));
		QVERIFY(view.columnHeader(1)->accessibleName().startsWith(QStringLiteral("Track 2: ")));
		QCOMPARE(view.cellButton(0, 0)->focusPolicy(), Qt::StrongFocus);
		QCOMPARE(view.columnHeader(0)->focusPolicy(), Qt::NoFocus);  // a header is read, not pressed

		QVERIFY(view.sceneButton(0)->accessibleName().endsWith(QStringLiteral("stopped")));
		QVERIFY(view.sceneButton(1)->accessibleName().endsWith(QStringLiteral("empty")));
		QVERIFY2(view.sceneButton(1)->font().italic(), "an empty scene is not told apart but by its words");
		QVERIFY(view.cellButton(0, 0)->text().startsWith(sessiongrid::cellMark(QString(), true)));
		QVERIFY(view.cellButton(1, 1)->text().isEmpty());

		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 1},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), QStringLiteral("snare.wav")},
			{QStringLiteral("name"), QStringLiteral("snare")}}).ok);
		QTest::mouseClick(view.sceneButton(0), Qt::LeftButton);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), 0, 3000);
		QTRY_VERIFY_WITH_TIMEOUT(view.sceneButton(0)->accessibleName().endsWith(QStringLiteral("playing")), 3000);
		QVERIFY(view.sceneButton(0)->text().startsWith(sessiongrid::sceneMark(sessiongrid::SceneState::Playing)));
		QVERIFY2(view.cellButton(0, 1)->toolTip().contains(QStringLiteral("kick")), qPrintable(view.cellButton(0, 1)->toolTip()));
		QCOMPARE(view.cellButton(0, 1)->accessibleDescription(), view.cellButton(0, 1)->toolTip());

		run(QStringLiteral("session.stop_all"));
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), -1, 3000);
		QVERIFY(run(QStringLiteral("session.clear_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 1}}).ok);
		first->setName(kept);
	}

	//! 0.5.0 theme 14: an audio file dragged over a filled cell shows the replacement before the drop; the drop is
	//! one undo step, says what it replaced, and Undo Replace puts the clip back.
	void aDropOnAFilledCellShowsTheReplacementAndUndoes()
	{
		QTemporaryDir dir;
		const QString file = dir.filePath(QStringLiteral("snare.wav"));
		QFile audio(file);
		QVERIFY(audio.open(QIODevice::WriteOnly));
		audio.close();
		SessionGridView view;
		view.refresh();
		QToolButton* cell = view.cellButton(0, 0);  // holds kick
		QMimeData mime;
		mime.setUrls({QUrl::fromLocalFile(file)});

		QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(cell, &enter);
		QVERIFY(enter.isAccepted());
		QCOMPARE(view.dropPreviewCell(), 0);
		QVERIFY2(cell->text().contains(QStringLiteral("Replace kick")), qPrintable(cell->text()));
		QVERIFY2(view.statusLine().contains(QStringLiteral("replace kick")), qPrintable(view.statusLine()));
		view.refresh();  // the timer's redraw keeps the preview
		QVERIFY(cell->text().contains(QStringLiteral("Replace kick")));
		QDragLeaveEvent leave;
		QCoreApplication::sendEvent(cell, &leave);
		QCOMPARE(view.dropPreviewCell(), -1);
		QVERIFY(!cell->text().contains(QStringLiteral("Replace")));

		QDragEnterEvent again(QPoint(4, 4), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(cell, &again);
		QDropEvent drop(QPointF(4, 4), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(cell, &drop);
		QVERIFY(slotState(0, 0).value(QStringLiteral("source")).toString().endsWith(QStringLiteral("snare.wav")));
		QCOMPARE(slotState(0, 0).value(QStringLiteral("name")).toString(), QStringLiteral("snare"));
		QVERIFY2(view.statusLine().contains(QStringLiteral("Replaced kick")), qPrintable(view.statusLine()));
		QVERIFY(!view.undoReplaceButton()->isHidden());

		QTest::mouseClick(view.undoReplaceButton(), Qt::LeftButton);  // one step puts kick back
		QCOMPARE(slotState(0, 0).value(QStringLiteral("name")).toString(), QStringLiteral("kick"));
		QCOMPARE(slotState(0, 0).value(QStringLiteral("source")).toString(), QStringLiteral("kick.wav"));
		QVERIFY(view.undoReplaceButton()->isHidden());
	}
};

QTEST_MAIN(SessionGridViewTest)
#include "SessionGridViewTest.moc"
