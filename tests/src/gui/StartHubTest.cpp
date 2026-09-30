/*
 * StartHubTest.cpp - M3 item 10: the start hub opens projects through the registry
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

#include <QtTest>

#include <QListWidget>
#include <QTemporaryDir>
#include <QToolButton>

#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "StartHub.h"

using namespace lmms;
using namespace lmms::gui;

class StartHubTest : public QObject
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
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aRecentProjectReopensAndEveryControlIsDeclared()
	{
		QTemporaryDir dir;
		const QString path = dir.filePath(QStringLiteral("hub.mmp"));
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("project.new"), {}).ok);
		QVERIFY(Engine::getSong()->projectFileName() != path);

		StartHub hub({path, dir.filePath(QStringLiteral("missing.mmp"))}, {});
		QCOMPARE(hub.recentList()->count(), 2);
		hub.recentList()->setCurrentRow(0);
		hub.openSelectedRecent();
		QCOMPARE(Engine::getSong()->projectFileName(), path);

		// A recent entry whose file has gone is refused, and the hub says why.
		StartHub second({dir.filePath(QStringLiteral("missing.mmp"))}, {});
		second.recentList()->setCurrentRow(0);
		second.openSelectedRecent();
		QVERIFY2(!second.statusText().isEmpty(), "a missing recent project opened silently");

		for (QToolButton* button : hub.findChildren<QToolButton*>())
		{
			QVERIFY2(!button->property("controlCommand").toString().isEmpty(),
				qPrintable(QStringLiteral("an undeclared button: ") + button->text()));
		}
		QTest::mouseClick(hub.newButton(), Qt::LeftButton);
		QVERIFY(Engine::getSong()->projectFileName().isEmpty() || Engine::getSong()->projectFileName() != path);
	}
};

QTEST_MAIN(StartHubTest)
#include "StartHubTest.moc"
