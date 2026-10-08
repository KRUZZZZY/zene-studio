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
		// The real Ableton Link backend (WF-051) is named in full, as Ableton's Link
		// guidelines ask, with the peer count while it is enabled.
		const QString model = QStringLiteral("ableton-link");
		QCOMPARE(sessionSyncText(QJsonObject{{QStringLiteral("enabled"), false}, {QStringLiteral("model"), model}}),
			QStringLiteral("Ableton Link"));
		QCOMPARE(sessionSyncText(QJsonObject{{QStringLiteral("enabled"), true}, {QStringLiteral("model"), model},
			{QStringLiteral("peer_count"), 3}, {QStringLiteral("session_tempo"), 120.5}}),
			QStringLiteral("Ableton Link: 3 peer(s), 120.5 BPM"));
	}

	//! In a build with the library the toggle says what it joins: Ableton Link, and
	//! the apps on the other end, and whether it is Enabled or Disabled.
	void theActionNamesAbletonLink()
	{
		const QJsonObject state = ControlRegistry::instance()->invoke(QStringLiteral("link.get_state"), QJsonObject{}).result;
		if (state.value(QStringLiteral("model")).toString() != QStringLiteral("ableton-link"))
		{
			QSKIP("this build carries the zene-link-style fallback, not the Ableton Link library");
		}
		QObject owner;
		QAction* sync = makeSessionSyncAction(&owner);
		QCOMPARE(sync->text(), QStringLiteral("Ableton Link"));
		QVERIFY2(sync->toolTip().contains(QStringLiteral("Ableton Live")), qPrintable(sync->toolTip()));
		QVERIFY2(sync->toolTip().contains(QStringLiteral("Disabled")), qPrintable(sync->toolTip()));
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
		const bool ableton = on.value(QStringLiteral("model")).toString() == QStringLiteral("ableton-link");
		const QString label = ableton ? QStringLiteral("Ableton Link") : QStringLiteral("Sync");
		QVERIFY2(sync->text().startsWith(label + QStringLiteral(": ")), qPrintable(sync->text()));
		if (ableton)
		{
			QVERIFY2(sync->toolTip().contains(QStringLiteral("Enabled")), qPrintable(sync->toolTip()));
			QVERIFY2(sync->toolTip().contains(QStringLiteral("connected")), qPrintable(sync->toolTip()));
		}
		sync->trigger();
		QVERIFY(!ControlRegistry::instance()->invoke(QStringLiteral("link.get_state"), QJsonObject{})
			.result.value(QStringLiteral("enabled")).toBool());
		QCOMPARE(sync->text(), label);
	}

	//! A refused toggle lends its message to the tooltip; the next toggle that succeeds has to
	//! give the action's own tooltip back, or the failure follows the user around for good.
	void aRefusedToggleIsForgottenAfterASuccess()
	{
		QObject owner;
		QAction* sync = makeSessionSyncAction(&owner);
		const QString normal = sync->toolTip();
		QVERIFY(!normal.isEmpty());
		sync->setToolTip(QStringLiteral("link.set_enabled refused: no multicast on this host"));
		sync->trigger();  // either way: this may refuse again and re-set the message
		sync->trigger();  // ...and this one hands the tooltip back
		QCOMPARE(sync->toolTip(), normal);
	}

	//! Sync can be switched on by another client while this action is unchecked. The poll runs
	//! whatever the toggle reads, so the action learns of it instead of waiting for a local click.
	void anEnableMadeElsewhereShowsWhileUnchecked()
	{
		QObject owner;
		QAction* sync = makeSessionSyncAction(&owner);
		QVERIFY(!sync->isChecked());
		const ControlResult on = ControlRegistry::instance()->invoke(QStringLiteral("link.set_enabled"),
			{{QStringLiteral("enabled"), true}});
		if (!on.ok
			|| !ControlRegistry::instance()->invoke(QStringLiteral("link.get_state"), QJsonObject{})
				   .result.value(QStringLiteral("enabled")).toBool())
		{
			QSKIP(qPrintable(QStringLiteral("session sync could not start here (no multicast?): ") + on.errorMessage));
		}
		QTRY_VERIFY_WITH_TIMEOUT(sync->isChecked(), 4000);
		QVERIFY2(sync->text().startsWith(QStringLiteral("Sync: ")), qPrintable(sync->text()));
		ControlRegistry::instance()->invoke(QStringLiteral("link.set_enabled"), {{QStringLiteral("enabled"), false}});
	}
};

QTEST_MAIN(SessionSyncActionTest)
#include "SessionSyncActionTest.moc"
