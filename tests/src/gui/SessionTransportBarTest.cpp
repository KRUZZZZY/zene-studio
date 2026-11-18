/*
 * SessionTransportBarTest.cpp - the clip launcher's Follow Actions / arrangement-record row
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

/*! Each control runs its command and the row reads the engine back: Follow Actions sets the
 *  session-wide switch (session.follow_get_state follows), Record to arrangement arms the recorder
 *  (session.arrangement_record_status follows), a change made over the socket shows on refresh, and
 *  Land is disabled while nothing is recorded. */

#include <QtTest>

#include <QJsonObject>
#include <QLabel>
#include <QToolButton>

#include "ControlRegistry.h"
#include "Engine.h"
#include "SessionArrangementRecorder.h"
#include "SessionTransportBar.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QToolButton* controlFor(SessionTransportBar& bar, const char* command)
{
	for (QToolButton* b : bar.findChildren<QToolButton*>())
	{
		if (b->property("controlCommand").toString() == QLatin1String(command)) { return b; }
	}
	return nullptr;
}

QJsonObject state(const char* command)
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), QJsonObject{}).result;
}

} // namespace

class SessionTransportBarTest : public QObject
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
		ControlRegistry::instance()->invoke(QStringLiteral("session.arrangement_record_arm"), {{QStringLiteral("armed"), false}});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theRowRunsItsCommandsAndFollowsTheEngine()
	{
		SessionTransportBar bar;
		QToolButton* follow = controlFor(bar, "session.set_follow_actions");
		QToolButton* record = controlFor(bar, "session.arrangement_record_arm");
		QToolButton* land = controlFor(bar, "session.arrangement_record_land");
		QVERIFY(follow && record && land && controlFor(bar, "session.back_to_arrangement"));

		const bool followBefore = state("session.follow_get_state").value(QStringLiteral("follow_actions_enabled")).toBool();
		QCOMPARE(follow->isChecked(), followBefore);
		follow->click();
		QCOMPARE(state("session.follow_get_state").value(QStringLiteral("follow_actions_enabled")).toBool(), !followBefore);

		QVERIFY(!record->isChecked());
		record->click();
		QVERIFY(state("session.arrangement_record_status").value(QStringLiteral("armed")).toBool());
		QVERIFY(!land->isEnabled());  // nothing recorded yet

		// Disarmed over the socket: the row shows it on its next refresh.
		ControlRegistry::instance()->invoke(QStringLiteral("session.arrangement_record_arm"), {{QStringLiteral("armed"), false}});
		bar.refresh();
		QVERIFY(!record->isChecked());
		follow->click();  // back to where it was
		QCOMPARE(state("session.follow_get_state").value(QStringLiteral("follow_actions_enabled")).toBool(), followBefore);
	}

	//! Land refuses while a recorded launch has not stopped, and says why. The row threw that
	//! message away, so the button that did nothing at all was the whole of what the user saw.
	void aRefusedButtonSaysWhy()
	{
		SessionTransportBar bar;
		QToolButton* record = controlFor(bar, "session.arrangement_record_arm");
		QToolButton* land = controlFor(bar, "session.arrangement_record_land");
		QVERIFY(record && land);
		record->click();  // arm, so the recorder takes what is pushed below
		QVERIFY(state("session.arrangement_record_status").value(QStringLiteral("armed")).toBool());
		// A launch with no stop yet: the audio thread would push this while performing.
		QVERIFY(Engine::getSong()->sessionScheduler().arrangementRecorder().recordStart(0, 0, 0));
		bar.refresh();
		QVERIFY(land->isEnabled());  // something is recorded now
		land->click();

		QStringList shown;
		for (QLabel* label : bar.findChildren<QLabel*>())
		{
			if (!label->text().isEmpty()) { shown << label->text(); }
		}
		QVERIFY2(shown.join(QStringLiteral(" | ")).contains(QStringLiteral("have not stopped yet")),
			qPrintable(shown.join(QStringLiteral(" | "))));
	}
};

QTEST_MAIN(SessionTransportBarTest)
#include "SessionTransportBarTest.moc"
