/*
 * MixerLatencyTextTest.cpp - a mixer strip's tooltip names its delay compensation
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

/*! The text: a strip with no latency is just its name; an effect chain's latency and the input's
 *  alignment point are named in frames and milliseconds; only the master carries the mix total. */

#include <QtTest>

#include "MixerLatencyText.h"

using namespace lmms::gui;

class MixerLatencyTextTest : public QObject
{
	Q_OBJECT

private slots:
	void theTooltipNamesTheLatency()
	{
		QCOMPARE(mixerLatencyText(QStringLiteral("Drums"), 0, 0, -1, 44100), QStringLiteral("Drums"));
		const QString bus = mixerLatencyText(QStringLiteral("Bus"), 441, 882, -1, 44100);
		QVERIFY(bus.startsWith(QStringLiteral("Bus\n")));
		QVERIFY2(bus.contains(QStringLiteral("441 frames (10.0 ms)")), qPrintable(bus));
		QVERIFY2(bus.contains(QStringLiteral("882 frames (20.0 ms)")), qPrintable(bus));
		QVERIFY(!bus.contains(QStringLiteral("Mix latency")));
		const QString master = mixerLatencyText(QStringLiteral("Master"), 0, 0, 1323, 44100);
		QVERIFY2(master.contains(QStringLiteral("Mix latency, compensated: 1323 frames (30.0 ms)")), qPrintable(master));
	}
};

QTEST_GUILESS_MAIN(MixerLatencyTextTest)
#include "MixerLatencyTextTest.moc"
