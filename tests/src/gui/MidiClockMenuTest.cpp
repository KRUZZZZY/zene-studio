/*
 * MidiClockMenuTest.cpp - Edit > MIDI Clock drives clock.master_set / clock.slave_set
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

/*! The submenu's items follow clock.get_state: Follow External Clock enables the slave and Follow
 *  Its Tempo is offered only while it follows; Send Clock's check mark is the master's enabled flag
 *  whether the switch was accepted or refused (a headless engine may have no MIDI client to send
 *  through), and a refusal is named on the indicator line. The line's text is pinned per state. */

#include <QtTest>

#include <QAction>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiClockMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QJsonObject clockPart(const char* part)
{
	return ControlRegistry::instance()->invoke(QStringLiteral("clock.get_state"), QJsonObject{})
		.result.value(QLatin1String(part)).toObject();
}

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

} // namespace

class MidiClockMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("clock.slave_set"), {{QStringLiteral("enabled"), false}});
		ControlRegistry::instance()->invoke(QStringLiteral("clock.master_set"), {{QStringLiteral("enabled"), false}});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theLineNamesTheState()
	{
		const auto state = [](bool sending, bool following, bool locked) {
			return QJsonObject{{QStringLiteral("master"), QJsonObject{{QStringLiteral("enabled"), sending}}},
				{QStringLiteral("slave"), QJsonObject{{QStringLiteral("enabled"), following},
					{QStringLiteral("locked"), locked}, {QStringLiteral("tempo_bpm"), 123.44}}}};
		};
		QCOMPARE(midiClockStatusText(state(false, false, false)), QStringLiteral("Clock not sending, not following"));
		QCOMPARE(midiClockStatusText(state(true, true, false)), QStringLiteral("Clock sending, following: no lock"));
		QCOMPARE(midiClockStatusText(state(false, true, true)),
			QStringLiteral("Clock not sending, following: locked at 123.4 BPM"));
	}

	void followingIsSwitchedFromTheMenu()
	{
		QMenu parent;
		QMenu* clock = addMidiClockMenu(&parent);
		QAction* follow = item(clock, QStringLiteral("Follow External Clock"));
		QAction* followTempo = item(clock, QStringLiteral("Follow Its Tempo"));
		QVERIFY(follow != nullptr && followTempo != nullptr);
		QVERIFY(!follow->isChecked());
		QVERIFY(!followTempo->isEnabled());

		follow->trigger();
		QVERIFY(clockPart("slave").value(QStringLiteral("enabled")).toBool());
		QVERIFY(follow->isChecked());
		QVERIFY(followTempo->isEnabled());
		QVERIFY(clock->actions().last()->text().startsWith(QStringLiteral("Clock not sending, following")));

		followTempo->trigger();
		QCOMPARE(followTempo->isChecked(), clockPart("slave").value(QStringLiteral("follow_tempo")).toBool());
		QVERIFY(followTempo->isChecked());

		follow->trigger();
		QVERIFY(!clockPart("slave").value(QStringLiteral("enabled")).toBool());
		QVERIFY(!follow->isChecked());
		QVERIFY(!followTempo->isEnabled());
	}

	void sendClockShowsTheMasterAcceptedOrRefused()
	{
		QMenu parent;
		QMenu* clock = addMidiClockMenu(&parent);
		QAction* send = item(clock, QStringLiteral("Send Clock"));
		QVERIFY(send != nullptr);
		send->trigger();
		const bool sending = clockPart("master").value(QStringLiteral("enabled")).toBool();
		QCOMPARE(send->isChecked(), sending);
		const QString line = clock->actions().last()->text();
		QVERIFY2(sending ? line.startsWith(QStringLiteral("Clock sending")) : line.startsWith(QStringLiteral("Refused: ")),
			qPrintable(line));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(MidiClockMenuTest)
#include "MidiClockMenuTest.moc"
