/*
 * LadspaChannelCountsTest.cpp - LADSPA audio-port direction and channel counts
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <QtTest>

#include <QFileInfo>

#include <limits>

#include "LadspaBase.h"
#include "LadspaManager.h"

using namespace lmms;

class LadspaChannelCountsTest : public QObject
{
	Q_OBJECT

private slots:
	// LadspaControls kept the control total in a 16-bit ch_cnt_t, so 65536 controls read back as 0
	// and the global stereo link skipped them. The count type must hold 65536 and keep every value.
	void controlCountTypeKeepsEveryControlPastTheSixteenBitLimit()
	{
		QVERIFY(std::numeric_limits<control_count_t>::max() >= 65536);
		QCOMPARE(static_cast<int>(controlCountOf(0)), 0);
		QCOMPARE(static_cast<int>(controlCountOf(65535)), 65535);
		QCOMPARE(static_cast<int>(controlCountOf(65536)), 65536);
		QCOMPARE(static_cast<int>(controlCountOf(65537)), 65537);
	}

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
