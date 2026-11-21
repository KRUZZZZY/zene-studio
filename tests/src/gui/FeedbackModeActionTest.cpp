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
#include "Song.h"

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
		QVERIFY(allow->isChecked());  // a declined disable leaves feedback on, so the item stays ticked

		QMenu again;
		QAction* allowAgain = addFeedbackModeAction(&again, [&refreshed] { ++refreshed; });
		QVERIFY(allowAgain->isChecked());
		setFeedbackModeQuestion([](const QString&) { return true; });
		allowAgain->trigger();
		QVERIFY(!state().value(QStringLiteral("enabled")).toBool());
		QVERIFY(mixer->channelSendModel(b, a) == nullptr);
		QCOMPARE(refreshed, 2);
	}

	// The state text must describe the state that is actually in force: with the submode
	// off, pdc_suspended is false, so the reply must not claim compensation is SUSPENDED.
	void theSuspensionTextFollowsTheSubmode()
	{
		Song* song = Engine::getSong();
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("feedback.enable"), QJsonObject{}).ok);
		const QJsonObject on = state();
		QVERIFY(on.value(QStringLiteral("pdc_suspended")).toBool());
		QVERIFY(on.value(QStringLiteral("suspension")).toString().contains(QStringLiteral("SUSPENDED")));

		song->noteProjectSaved(QStringLiteral("feedback-clean.mmpz"));
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("feedback.disable"), QJsonObject{}).ok);
		const QJsonObject off = state();
		QVERIFY(!off.value(QStringLiteral("enabled")).toBool());
		QVERIFY(!off.value(QStringLiteral("pdc_suspended")).toBool());
		QVERIFY(!off.value(QStringLiteral("suspension")).toString().contains(QStringLiteral("SUSPENDED")));
		QVERIFY(!song->isModified());  // disabling an empty submode removes no saved route
	}

	// The inverse feedback.disable advertises must be one command that restores the WHOLE edit:
	// the mode AND the loop-closing sends the command removed. feedback.enable only turns the
	// mode back on, so a caller replaying the advertised inverse would lose the routes.
	void theDisableInverseIsTheCommandThatRestoresTheSends()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QVERIFY(registry->invoke(QStringLiteral("feedback.enable"), QJsonObject{}).ok);
		Mixer* mixer = Engine::mixer();
		const int a = mixer->createChannel();
		const int b = mixer->createChannel();
		QVERIFY(mixer->createChannelSend(a, b) != nullptr);
		QVERIFY(mixer->createChannelSend(b, a) != nullptr);
		QCOMPARE(state().value(QStringLiteral("feedback_routes")).toArray().size(), 1);

		Song* song = Engine::getSong();
		song->noteProjectSaved(QStringLiteral("feedback-clean.mmpz"));
		QVERIFY(!song->isModified());
		const ControlResult off = registry->invoke(QStringLiteral("feedback.disable"), QJsonObject{});
		QVERIFY2(off.ok, qPrintable(off.errorMessage));
		QCOMPARE(off.result.value(QStringLiteral("removed")).toInt(), 1);
		QVERIFY(song->isModified());
		// The registry strips the private __transaction key from the wire result, so the
		// advertised inverse is read back from the recorded transaction.
		QJsonObject inverse;
		const QJsonArray records = registry->invoke(QStringLiteral("control.transactions"), QJsonObject{})
			.result.value(QStringLiteral("transactions")).toArray();
		for (const QJsonValue& record : records)
		{
			const QJsonObject entry = record.toObject();
			if (entry.value(QStringLiteral("command")).toString() == QLatin1String("feedback.disable"))
			{
				inverse = entry.value(QStringLiteral("inverse")).toObject();
			}
		}
		QVERIFY2(!inverse.isEmpty(), "feedback.disable recorded no inverse");
		QCOMPARE(inverse.value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));

		// Replaying exactly the advertised inverse has to bring both back.
		QVERIFY(registry->invoke(inverse.value(QStringLiteral("op")).toString(),
			inverse.value(QStringLiteral("args")).toObject()).ok);
		QVERIFY(state().value(QStringLiteral("enabled")).toBool());
		QVERIFY2(mixer->channelSendModel(b, a) != nullptr,
			"the advertised inverse restored the mode but not the removed loop-closing send");
	}
};

QTEST_MAIN(FeedbackModeActionTest)
#include "FeedbackModeActionTest.moc"
