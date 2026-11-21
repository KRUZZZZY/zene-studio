/*
 * TouchWorkspaceTest.cpp - Q050-235: the touch workspace's launcher tap targets
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
/*! Q050-235 (touch workspace, stage 1/3): the workspace is an appearance field read once at start,
 *  and in it the launcher's cells, scene buttons and stop buttons - Stop All included - have real hit
 *  rectangles of at least 44 x 44 Qt logical pixels at 100% zoom, not just larger artwork. A press near
 *  a cell's corner still launches it, which is what makes the rectangle the target; the touch workspace
 *  does NOT turn accessible mode on, and choosing another workspace still applies from the next start.
 *  The process starts with ui/workspace="touch" in its config, exactly as a restart after the choice.
 */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>
#include <utility>

#include "Appearance.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "MenuTestSupport.h"
#include "SessionGridView.h"
#include "WorkspaceMenus.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A launcher button's real hit rectangle: the size it is laid out at (never its artwork's).
bool reachesTouchMinimum(const QWidget* button)
{
	return button != nullptr && button->width() >= appearance::touchTargetSide
		&& button->height() >= appearance::touchTargetSide;
}

//! The scene engine column @a track is playing, or -1 when it is idle (session.get_state's own reading).
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

} // namespace

class TouchWorkspaceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("HOME", m_home.path().toLocal8Bit());
		qputenv("ZENE_UI_SCALE_FILE", m_home.filePath(QStringLiteral("scale")).toLocal8Bit());
		// The restart after the choice: the config already carries the touch workspace before anything reads it.
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("workspace"), QStringLiteral("touch"));
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
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The started workspace is touch, read once; it is not accessible mode, and it asks for 44 logical pixels.
	void theTouchWorkspaceIsTheStartedOne()
	{
		QCOMPARE(appearance::options().workspace, QStringLiteral("touch"));
		QCOMPARE(appearance::startedWith().workspace, QStringLiteral("touch"));
		QVERIFY(tokens::touchWorkspace());
		QCOMPARE(appearance::options().scale, 1.0);  // 100% global zoom
		QVERIFY(tokens::tapTargetSize() >= appearance::touchTargetSide);
		// Separate preferences: the touch workspace changes no palette, motion or density.
		QVERIFY(!tokens::accessibleMode());
		QVERIFY(!appearance::options().accessible);
	}

	//! Every launcher cell, scene button and stop button - Stop All too - is at least 44 x 44.
	void launcherCellsAndSceneStopActionsReachTheTouchMinimum()
	{
		SessionGridView view;
		view.resize(1024, 768);
		view.show();
		QVERIFY(QTest::qWaitForWindowExposed(&view));
		view.refresh();
		QTRY_VERIFY(view.cellButton(0, 0) != nullptr && view.cellButton(0, 0)->isVisible());
		for (int track = 0; track < 2; ++track)
		{
			for (int scene = 0; scene < 2; ++scene)
			{
				QToolButton* cell = view.cellButton(track, scene);
				QVERIFY2(reachesTouchMinimum(cell), qPrintable(QStringLiteral("cell %1,%2 is %3x%4")
					.arg(track).arg(scene).arg(cell->width()).arg(cell->height())));
			}
			QToolButton* stop = view.stopButton(track);
			QVERIFY2(reachesTouchMinimum(stop), qPrintable(QStringLiteral("stop %1 is %2x%3")
				.arg(track).arg(stop->width()).arg(stop->height())));
		}
		for (int scene = 0; scene < 2; ++scene)
		{
			QToolButton* launch = view.sceneButton(scene);
			QVERIFY2(reachesTouchMinimum(launch), qPrintable(QStringLiteral("scene %1 is %2x%3")
				.arg(scene).arg(launch->width()).arg(launch->height())));
		}
		QVERIFY2(reachesTouchMinimum(view.stopAllButton()), qPrintable(QStringLiteral("stop all is %1x%2")
			.arg(view.stopAllButton()->width()).arg(view.stopAllButton()->height())));
	}

	//! The rectangle is the target: a press near a cell's top-left corner launches it, not only its centre.
	void aPressNearTheCellCornerLaunchesIt()
	{
		SessionGridView view;
		view.resize(1024, 768);
		view.show();
		QVERIFY(QTest::qWaitForWindowExposed(&view));
		view.refresh();
		QToolButton* cell = view.cellButton(0, 0);
		QVERIFY(cell != nullptr && cell->width() >= appearance::touchTargetSide);
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);
		QTest::mouseClick(cell, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
		QTRY_VERIFY_WITH_TIMEOUT(cell->isChecked(), 3000);
		// And a scene button's corner launches its row, while a stop button's corner stops the column.
		QTest::mouseClick(view.sceneButton(0), Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), 0, 3000);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(1), 0, 3000);
		QTest::mouseClick(view.stopButton(0), Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), -1, 3000);
		QCOMPARE(playingScene(1), 0);
	}

	//! The GUI entry point: Customise > Theme > Workspace lists the choices and runs the same command;
	//! one undo puts the prior choice back, and the change is reported for the next start.
	void theMenuChoosesTheWorkspaceThroughTheCommand()
	{
		QMenu menu;
		populateCustomiseExtras(&menu);
		QAction* standard = menuAction(&menu, {QStringLiteral("Theme"), QStringLiteral("Workspace"),
			QStringLiteral("standard")});
		QAction* touch = menuAction(&menu, {QStringLiteral("Theme"), QStringLiteral("Workspace"), QStringLiteral("touch")});
		QVERIFY2(standard != nullptr && touch != nullptr, "Customise > Theme > Workspace should offer both choices");
		QCOMPARE(standard->property("controlCommand").toString(), QStringLiteral("window.appearance"));
		QCOMPARE(touch->property("controlCommand").toString(), QStringLiteral("window.appearance"));

		standard->trigger();
		QCOMPARE(appearance::options().workspace, QStringLiteral("standard"));
		QCOMPARE(appearance::startedWith().workspace, QStringLiteral("touch"));  // this run still draws touch
		QVERIFY(tokens::touchWorkspace());
		const ControlResult read = run(QStringLiteral("window.appearance"));
		QVERIFY2(read.ok, qPrintable(read.errorMessage));
		QCOMPARE(read.result.value(QStringLiteral("workspace")).toString(), QStringLiteral("standard"));
		QVERIFY(read.result.value(QStringLiteral("restart_pending")).toBool());

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(appearance::options().workspace, QStringLiteral("touch"));
		// And the choice was written to the user's config now, not only at quit.
		QCOMPARE(ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("workspace")), QStringLiteral("touch"));
	}

	//! 'touch' the density value keeps meaning accessible mode; it does not silently become the workspace.
	void theOldTouchDensityStillMeansAccessibleMode()
	{
		const ControlResult set = run(QStringLiteral("window.appearance"), {{QStringLiteral("density"), QStringLiteral("touch")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QVERIFY(appearance::options().accessible);
		QCOMPARE(appearance::options().workspace, QStringLiteral("touch"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!appearance::options().accessible);
		QCOMPARE(appearance::options().workspace, QStringLiteral("touch"));
	}

	//! A workspace name outside the list is refused typed, as the schema promises.
	void anUnknownWorkspaceIsRefused()
	{
		const ControlResult refused = run(QStringLiteral("window.appearance"), {{QStringLiteral("workspace"), QStringLiteral("tablet")}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(appearance::options().workspace, QStringLiteral("touch"));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(TouchWorkspaceTest)
#include "TouchWorkspaceTest.moc"
