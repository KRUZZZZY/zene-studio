/*
 * LadspaChannelCountsTest.cpp - LADSPA audio-port direction and channel counts
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <QtTest>

#include <QFileInfo>

#include "LadspaManager.h"

using namespace lmms;

class LadspaChannelCountsTest : public QObject
{
	Q_OBJECT

private slots:
	void audioPortsDetermineChannelCounts()
	{
		const auto fixturePath = QString::fromUtf8(LADSPA_CHANNEL_FIXTURE_PATH);
		qputenv("LADSPA_PATH", QFileInfo(fixturePath).absolutePath().toUtf8());

		LadspaManager manager;
		const auto library = QFileInfo(fixturePath).fileName();
		const auto* stereo = manager.getDescription({library, "left_right_stereo"});
		QVERIFY(stereo);
		QCOMPARE(stereo->inputChannels, uint16_t{2});
		QCOMPARE(stereo->outputChannels, uint16_t{2});
		QVERIFY(stereo->type == LadspaPluginType::Transfer);

		const auto* asymmetric = manager.getDescription({library, "input_left_right"});
		QVERIFY(asymmetric);
		QCOMPARE(asymmetric->inputChannels, uint16_t{1});
		QCOMPARE(asymmetric->outputChannels, uint16_t{2});
		QVERIFY(asymmetric->type == LadspaPluginType::Transfer);
	}
};

QTEST_GUILESS_MAIN(LadspaChannelCountsTest)

#include "LadspaChannelCountsTest.moc"
