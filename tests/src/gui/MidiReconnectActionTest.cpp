/*
 * MidiReconnectActionTest.cpp - Edit > MIDI Controller Reconnect arms and names what it holds
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

/*! Over a throwaway config file (the arm switch is persisted): the item arms and disarms
 *  re-connection through midi.reconnect_arm - midi.reconnect_status follows - and its text reads the
 *  status: plain with nothing bound, "(N bound, M live, K lost)" otherwise. */

#include <QtTest>

#include <QAction>
#include <QFile>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiReconnectAction.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

bool armed()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("midi.reconnect_status"), QJsonObject{})
		.result.value(QStringLiteral("enabled")).toBool();
}

} // namespace

class MidiReconnectActionTest : public QObject
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
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theTextNamesWhatIsBound()
	{
		QCOMPARE(midiReconnectText(QJsonObject{{QStringLiteral("assignment_count"), 0}}), QStringLiteral("MIDI Controller Reconnect"));
		QCOMPARE(midiReconnectText(QJsonObject{{QStringLiteral("assignment_count"), 3}, {QStringLiteral("live_count"), 2},
			{QStringLiteral("lost_count"), 1}}), QStringLiteral("MIDI Controller Reconnect (3 bound, 2 live, 1 lost)"));
	}

	void theItemArmsAndDisarms()
	{
		QMenu menu;
		QAction* reconnect = addMidiReconnectAction(&menu);
		const bool before = armed();
		QCOMPARE(reconnect->isChecked(), before);
		reconnect->trigger();
		QCOMPARE(armed(), !before);
		QCOMPARE(reconnect->isChecked(), !before);
		reconnect->trigger();
		QCOMPARE(armed(), before);
	}

	// The arm switch persists through saveConfigFile(). A config the product cannot
	// parse must survive that save byte-for-byte: the mode still toggles in memory,
	// and the file keeps what the user had.
	void armSwitchDoesNotOverwriteAMalformedConfig()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("zene.xml"));
		const QByteArray malformed = "<zene version=\"1.0.99\" configversion=\"3\"><midi\n";
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			QCOMPARE(file.write(malformed), malformed.size());
		}

		ConfigManager::inst()->loadConfigFile(path);
		QMenu menu;
		QAction* reconnect = addMidiReconnectAction(&menu);
		const bool before = armed();
		reconnect->trigger();
		QCOMPARE(armed(), !before);
		reconnect->trigger();
		QCOMPARE(armed(), before);

		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), malformed);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(MidiReconnectActionTest)
#include "MidiReconnectActionTest.moc"
