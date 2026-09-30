/*
 * SessionSyncActionTest.cpp - the transport's Sync toggle runs link.set_enabled and shows the session
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

/*! The Sync action: toggling it runs link.set_enabled - what link.get_state then reports - and
 *  its text reads the session (peers, tempo) while enabled and says plain "Sync" while not. */

#include <QtTest>

#include <QAction>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "Engine.h"
#include "SessionSyncAction.h"

using namespace lmms;
using namespace lmms::gui;

class SessionSyncActionTest : public QObject
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
		ControlRegistry::instance()->invoke(QStringLiteral("link.set_enabled"), {{QStringLiteral("enabled"), false}});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theTextReadsTheSession()
	{
		QCOMPARE(sessionSyncText(QJsonObject{{QStringLiteral("enabled"), false}}), QStringLiteral("Sync"));
		QCOMPARE(sessionSyncText(QJsonObject{{QStringLiteral("enabled"), true}, {QStringLiteral("peer_count"), 2},
			{QStringLiteral("session_tempo"), 128.0}}), QStringLiteral("Sync: 2 peer(s), 128.0 BPM"));
	}

	void togglingRunsTheCommand()
	{
		QObject owner;
		QAction* sync = makeSessionSyncAction(&owner);
		QVERIFY(!sync->isChecked());
		sync->trigger();
		const QJsonObject on = ControlRegistry::instance()->invoke(QStringLiteral("link.get_state"), QJsonObject{}).result;
		if (!on.value(QStringLiteral("enabled")).toBool())
		{
			QSKIP(qPrintable(QStringLiteral("session sync could not start here (no multicast?): ") + sync->toolTip()));
		}
		QVERIFY(sync->isChecked());
		QVERIFY2(sync->text().startsWith(QStringLiteral("Sync: ")), qPrintable(sync->text()));
		sync->trigger();
		QVERIFY(!ControlRegistry::instance()->invoke(QStringLiteral("link.get_state"), QJsonObject{})
			.result.value(QStringLiteral("enabled")).toBool());
		QCOMPARE(sync->text(), QStringLiteral("Sync"));
	}
};

QTEST_MAIN(SessionSyncActionTest)
#include "SessionSyncActionTest.moc"
