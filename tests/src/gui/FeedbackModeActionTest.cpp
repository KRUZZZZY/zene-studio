/*
 * FeedbackModeActionTest.cpp - Allow feedback sends enters and leaves the cycle-permitted submode
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

/*! The item enters the submode (feedback.enable) - after which the mixer accepts a send that closes a
 *  loop - and leaving it asks first, naming the loop-closing sends it deletes: a No keeps the mode and
 *  the send, a Yes runs feedback.disable, the send is gone and the refresh callback ran. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>

#include "ControlRegistry.h"
#include "Engine.h"
#include "FeedbackModeAction.h"
#include "Mixer.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QJsonObject state()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("feedback.get_state"), QJsonObject{}).result;
}

} // namespace

class FeedbackModeActionTest : public QObject
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
		ControlRegistry::instance()->invoke(QStringLiteral("feedback.disable"), QJsonObject{});
		setFeedbackModeQuestion({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theSubmodeIsEnteredAndLeftFromTheMenu()
	{
		int refreshed = 0;
		QMenu menu;
		QAction* allow = addFeedbackModeAction(&menu, [&refreshed] { ++refreshed; });
		QVERIFY(!allow->isChecked());
		allow->trigger();
		QVERIFY(state().value(QStringLiteral("enabled")).toBool());
		QCOMPARE(refreshed, 1);

		Mixer* mixer = Engine::mixer();
		const int a = mixer->createChannel();
		const int b = mixer->createChannel();
		QVERIFY(mixer->createChannelSend(a, b) != nullptr);
		QVERIFY(mixer->isInfiniteLoop(b, a));
		QVERIFY(mixer->createChannelSend(b, a) != nullptr);  // the loop-closing send the mode permits
		QCOMPARE(state().value(QStringLiteral("feedback_routes")).toArray().size(), 1);

		QString asked;
		setFeedbackModeQuestion([&asked](const QString& question) { asked = question; return false; });
		QVERIFY(allow->isChecked());
		allow->trigger();
		QVERIFY(asked.contains(QStringLiteral("1 loop-closing send")));
		QVERIFY(state().value(QStringLiteral("enabled")).toBool());
		QCOMPARE(state().value(QStringLiteral("feedback_routes")).toArray().size(), 1);

		QMenu again;
		QAction* allowAgain = addFeedbackModeAction(&again, [&refreshed] { ++refreshed; });
		QVERIFY(allowAgain->isChecked());
		setFeedbackModeQuestion([](const QString&) { return true; });
		allowAgain->trigger();
		QVERIFY(!state().value(QStringLiteral("enabled")).toBool());
		QVERIFY(mixer->channelSendModel(b, a) == nullptr);
		QCOMPARE(refreshed, 2);
	}
};

QTEST_MAIN(FeedbackModeActionTest)
#include "FeedbackModeActionTest.moc"
