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
 *  through), and a refusal is named on the indicator line. The line's text is pinned per state.
 *  Clock Source is the GUI half of Q050-026's source selection: it lists what midi.device_list lists,
 *  choosing an entry drives clock.slave_set, and it never changes the follower's mode by itself. */

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

//! Every entry label of \a menu, comma separated: the evidence for "what this
//! submenu offers" when the assertion below fails.
QString labels(QMenu* menu)
{
	QStringList names;
	for (QAction* action : menu->actions()) { names.append(action->text()); }
	return names.join(QStringLiteral(", "));
}

//! The Clock Source SUBMENU of a freshly built clock menu, or nullptr.
QMenu* sourceMenu(QMenu* clock)
{
	QAction* entry = item(clock, QStringLiteral("Clock Source"));
	return entry != nullptr ? entry->menu() : nullptr;
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
		const auto state = [](bool sending, bool following, bool locked, bool ready = true) {
			return QJsonObject{{QStringLiteral("master"), QJsonObject{{QStringLiteral("enabled"), sending},
					{QStringLiteral("port_ready"), ready}}},
				{QStringLiteral("slave"), QJsonObject{{QStringLiteral("enabled"), following},
					{QStringLiteral("locked"), locked}, {QStringLiteral("tempo_bpm"), 123.44}}}};
		};
		QCOMPARE(midiClockStatusText(state(false, false, false)), QStringLiteral("Clock not sending, not following"));
		QCOMPARE(midiClockStatusText(state(true, true, false)), QStringLiteral("Clock sending, following: no lock"));
		QCOMPARE(midiClockStatusText(state(false, true, true)),
			QStringLiteral("Clock not sending, following: locked at 123.4 BPM"));
	}

	// The master's enabled flag says the clock is on; port_ready says whether anything is
	// subscribed to receive it (MidiClock::masterPortReady). An enabled master with no output
	// routed must not read as "sending" - it emits to nobody.
	void theLineDistinguishesEnabledFromRouted()
	{
		const auto state = [](bool sending, bool ready) {
			return QJsonObject{
				{QStringLiteral("master"), QJsonObject{{QStringLiteral("enabled"), sending},
					{QStringLiteral("port_ready"), ready}}},
				{QStringLiteral("slave"), QJsonObject{{QStringLiteral("enabled"), false}}}};
		};
		QCOMPARE(midiClockStatusText(state(true, true)), QStringLiteral("Clock sending, not following"));
		QVERIFY(midiClockStatusText(state(true, false))
			!= midiClockStatusText(state(true, true)));
		QVERIFY(midiClockStatusText(state(true, false))
			!= midiClockStatusText(state(false, false)));
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

	// Q050-026: choosing a source in the GUI must reach the ENGINE through the
	// registered command (not a private field), must not switch the follower's
	// mode by itself, and must be offered what midi.device_list offers - with no
	// MIDI backend that list is empty, which is why "Any Input" is there alone and
	// why this test pins that shape rather than a device it cannot have.
	void theSourceSubmenuDrivesTheSameCommand()
	{
		QMenu parent;
		QMenu* clock = addMidiClockMenu(&parent);
		QMenu* source = sourceMenu(clock);
		QVERIFY2(source != nullptr, "the clock menu must offer a Clock Source submenu");
		QAction* any = item(source, QStringLiteral("Any Input"));
		QVERIFY2(any != nullptr, qPrintable(QStringLiteral("entries: %1").arg(labels(source))));
		QCOMPARE(clockPart("slave").value(QStringLiteral("enabled")).toBool(), false);
		QCOMPARE(any->isChecked(), true);

		// With no readable port there is exactly one choice, and it is the empty
		// source: the state says so.
		QCOMPARE(source->actions().size(), 1);
		QCOMPARE(clockPart("slave").value(QStringLiteral("source_port")).toString(), QString());

		// Following first, then choosing: the choice is reported back by the engine
		// (an agent or the menu can read the same number) and the MODE is unchanged,
		// because picking a port is not a decision to start following.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("clock.slave_set"),
			{{QStringLiteral("enabled"), true}}).ok);
		any->trigger();
		const QJsonObject slave = clockPart("slave");
		QVERIFY2(slave.value(QStringLiteral("enabled")).toBool(), "choosing a source must not stop following");
		QCOMPARE(slave.value(QStringLiteral("source_port")).toString(), QString());
		// The state line is still the LAST item: the submenu was inserted above it.
		QVERIFY(clock->actions().last()->text().startsWith(QStringLiteral("Clock ")));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(MidiClockMenuTest)
#include "MidiClockMenuTest.moc"
