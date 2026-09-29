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
#include <QToolButton>

#include "ControlRegistry.h"
#include "Engine.h"
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

} // namespace

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
};

QTEST_MAIN(SessionGridViewTest)
#include "SessionGridViewTest.moc"
