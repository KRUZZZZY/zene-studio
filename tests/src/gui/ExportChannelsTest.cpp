/*
 * ExportChannelsTest.cpp - the export dialog's Channels row: stereo, or surround for a surround master (0.5.0 F7)
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


/*! 0.5.0 F7 (MX-060): the export dialog offers a surround export exactly when it can be made. On a stereo
 *  master the Channels row has Stereo only and is disabled; on a 5.1 master it adds "Surround 5.1 (6
 *  channels)"; exporting tracks one by one stays stereo. Choosing surround turns the loudness report off
 *  and disables it (a surround export cannot carry one); choosing stereo again gives it back. */

#include <QtTest>

#include <QCheckBox>
#include <QComboBox>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ExportProjectDialog.h"
#include "Mixer.h"
#include "OutputSettings.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QComboBox* channelsOf(QWidget& dialog)
{
	for (QComboBox* box : dialog.findChildren<QComboBox*>())
	{
		if (box->accessibleName() == QStringLiteral("Channels")) { return box; }
	}
	return nullptr;
}

QCheckBox* loudnessOf(QWidget& dialog)
{
	for (QCheckBox* box : dialog.findChildren<QCheckBox*>())
	{
		if (box->text().startsWith(QStringLiteral("Loudness report"))) { return box; }
	}
	return nullptr;
}

bool masterFormat(const char* name)
{
	const QString master = control::channelIdOf(Engine::mixer()->mixerChannel(0));
	return ControlRegistry::instance()->invoke(QStringLiteral("mixer.channel_format"),
		{{QStringLiteral("channel"), master}, {QStringLiteral("format"), QString::fromLatin1(name)}}).ok;
}

} // namespace

class ExportChannelsTest : public QObject
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

	void aStereoMasterOffersStereoOnly()
	{
		QVERIFY(masterFormat("stereo"));
		ExportProjectDialog dialog(QStringLiteral("/tmp/unused.wav"), ExportProjectDialog::Mode::ExportProject);
		QComboBox* channels = channelsOf(dialog);
		QVERIFY(channels != nullptr);
		QCOMPARE(channels->count(), 1);
		QVERIFY(!channels->isEnabled());
		QCOMPARE(channels->currentData().toInt(), static_cast<int>(OutputSettings::ChannelMode::Stereo));
	}

	void aSurroundMasterOffersItsLayout()
	{
		QVERIFY(masterFormat("5.1"));
		ExportProjectDialog dialog(QStringLiteral("/tmp/unused.wav"), ExportProjectDialog::Mode::ExportProject);
		QComboBox* channels = channelsOf(dialog);
		QCheckBox* loudness = loudnessOf(dialog);
		QVERIFY(channels != nullptr && loudness != nullptr);
		QCOMPARE(channels->count(), 2);
		QVERIFY(channels->isEnabled());
		QCOMPARE(channels->currentData().toInt(), static_cast<int>(OutputSettings::ChannelMode::Stereo));  // the default
		QVERIFY2(channels->itemText(1).contains(QStringLiteral("5.1")) && channels->itemText(1).contains(QStringLiteral("6")),
			qPrintable(channels->itemText(1)));
		QVERIFY(loudness->isChecked());

		channels->setCurrentIndex(channels->findData(static_cast<int>(OutputSettings::ChannelMode::Surround)));
		QVERIFY(!loudness->isChecked());
		QVERIFY(!loudness->isEnabled());
		channels->setCurrentIndex(channels->findData(static_cast<int>(OutputSettings::ChannelMode::Stereo)));
		QVERIFY(loudness->isEnabled());

		ExportProjectDialog tracks(QStringLiteral("/tmp/unused"), ExportProjectDialog::Mode::ExportTracks);
		QCOMPARE(channelsOf(tracks)->count(), 1);  // tracks one by one stay stereo
		QVERIFY(masterFormat("stereo"));
	}
};

QTEST_MAIN(ExportChannelsTest)
#include "ExportChannelsTest.moc"
