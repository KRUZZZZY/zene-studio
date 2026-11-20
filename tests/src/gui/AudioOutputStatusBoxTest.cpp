/*
 * AudioOutputStatusBoxTest.cpp - Settings > Audio: the running output box and its test tone buttons
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

/*! Q050-183.1, over the real Settings dialog: the box shows what audio.device_list reports, its Play and Stop buttons
 *  start and stop the test tone through the commands without touching the project, and the fallback text names the
 *  device that could not start and why. */

#include <QtTest>

#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "AudioOutputStatusBox.h"
#include "AudioTestTone.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "SetupDialog.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

class AudioOutputStatusBoxTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void playAndStopGoThroughTheCommands()
	{
		SetupDialog dialog(SetupDialog::ConfigTab::AudioSettings);
		dialog.show();
		auto* status = dialog.findChild<QLabel*>(QStringLiteral("audioRunningStatus"));
		auto* play = dialog.findChild<QPushButton*>(QStringLiteral("audioTestTonePlay"));
		auto* stop = dialog.findChild<QPushButton*>(QStringLiteral("audioTestToneStop"));
		QVERIFY(status != nullptr);
		QVERIFY(play != nullptr);
		QVERIFY(stop != nullptr);

		const QString devName = Engine::audioEngine()->audioDevName();
		QVERIFY(status->text().contains(devName));
		QVERIFY(status->text().contains(QStringLiteral("Configured:")));

		Song* song = Engine::getSong();
		const bool playing = song->isPlaying();
		const bool modified = song->isModified();

		QTest::mouseClick(play, Qt::LeftButton);
		QVERIFY(audioTestToneActive());
		QCOMPARE(song->isPlaying(), playing);
		QCOMPARE(song->isModified(), modified);

		QTest::mouseClick(stop, Qt::LeftButton);
		QTRY_VERIFY_WITH_TIMEOUT(!audioTestToneActive(), 5000);
		QCOMPARE(song->isPlaying(), playing);
		QCOMPARE(song->isModified(), modified);
	}

	void describeShowsTheRunningAndConfiguredOutput()
	{
		const QJsonObject list{
			{QStringLiteral("current"), QStringLiteral("ALSA")},
			{QStringLiteral("configured"), QStringLiteral("ALSA")},
			{QStringLiteral("fallback"), false},
		};
		const QString text = AudioOutputStatusBox::describe(list);
		QVERIFY(text.contains(QStringLiteral("Running: ALSA")));
		QVERIFY(text.contains(QStringLiteral("Configured: ALSA")));
		QVERIFY(!text.contains(QStringLiteral("Fallback")));
		QVERIFY(!text.contains(QStringLiteral("Restart")));
	}

	void describeNamesTheFallbackAndItsReason()
	{
		const QJsonObject list{
			{QStringLiteral("current"), QStringLiteral("Dummy (no sound output)")},
			{QStringLiteral("configured"), QStringLiteral("ALSA")},
			{QStringLiteral("requested"), QStringLiteral("ALSA")},
			{QStringLiteral("fallback"), true},
			{QStringLiteral("failure_reason"), QStringLiteral("device busy")},
		};
		const QString text = AudioOutputStatusBox::describe(list);
		QVERIFY2(text.contains(QStringLiteral("Fallback: ALSA could not start - device busy")), qPrintable(text));
	}

	void describeSaysNoReasonGivenWhenThereIsNone()
	{
		const QJsonObject list{
			{QStringLiteral("current"), QStringLiteral("Dummy (no sound output)")},
			{QStringLiteral("configured"), QStringLiteral("ALSA")},
			{QStringLiteral("requested"), QStringLiteral("ALSA")},
			{QStringLiteral("fallback"), true},
			{QStringLiteral("failure_reason"), QString()},
		};
		const QString text = AudioOutputStatusBox::describe(list);
		QVERIFY2(text.contains(QStringLiteral("Fallback: ALSA could not start - no reason given")), qPrintable(text));
	}

	void describeSaysNoneWhenNothingIsConfigured()
	{
		const QJsonObject list{
			{QStringLiteral("current"), QStringLiteral("Dummy (no sound output)")},
			{QStringLiteral("configured"), QString()},
			{QStringLiteral("fallback"), false},
		};
		const QString text = AudioOutputStatusBox::describe(list);
		QVERIFY2(text.contains(QStringLiteral("Configured: (none)")), qPrintable(text));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(AudioOutputStatusBoxTest)
#include "AudioOutputStatusBoxTest.moc"
