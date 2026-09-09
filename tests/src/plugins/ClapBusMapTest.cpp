/*
 * ClapBusMapTest.cpp - unit tests for the CLAP port to transport mapping
 *
 * Copyright (c) 2026 LMMS contributors
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

#include <QtTest>

#include "ClapBusMap.h"
#include "lmms_constants.h"

namespace lmms::clap
{

namespace
{
auto makePort(PortDirection direction, int channels, bool isMain = false) -> PortDescriptor
{
	PortDescriptor port;
	port.direction = direction;
	port.channelCount = channels;
	port.isMain = isMain;
	port.portType = channels == 1 ? QStringLiteral("mono") : QStringLiteral("stereo");
	return port;
}
} // namespace

class ClapBusMapTest : public QObject
{
	Q_OBJECT

private slots:
	void testStereoPair();
	void testMonoToStereo();
	void testSideChain();
	void testIgnoresEmptyPorts();
	void testEmptyLayoutIsInvalid();
	void testChannelBudgetIsClamped();
	void testPortOrderIsPreserved();
	void testMultiOutputPorts();
};

void ClapBusMapTest::testStereoPair()
{
	const auto layout = mapPorts({
		makePort(PortDirection::Input, 2, true),
		makePort(PortDirection::Output, 2, true),
	});

	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.hasSideChain, false);
	QCOMPARE(layout.inputPortChannels, std::vector<int>{2});
	QCOMPARE(layout.outputPortChannels, std::vector<int>{2});
	QVERIFY(layout.isValid());
}

void ClapBusMapTest::testMonoToStereo()
{
	const auto layout = mapPorts({
		makePort(PortDirection::Input, 1, true),
		makePort(PortDirection::Output, 2, true),
	});

	QCOMPARE(layout.inputs, 1);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.hasSideChain, false);
	QCOMPARE(layout.inputPortChannels, std::vector<int>{1});
	QCOMPARE(layout.outputPortChannels, std::vector<int>{2});
}

void ClapBusMapTest::testSideChain()
{
	const auto layout = mapPorts({
		makePort(PortDirection::Input, 2, true),
		makePort(PortDirection::Input, 1),
		makePort(PortDirection::Output, 2, true),
	});

	QCOMPARE(layout.inputs, 3);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.hasSideChain, true);
	QCOMPARE(layout.inputPortChannels, (std::vector<int>{2, 1}));
}

void ClapBusMapTest::testIgnoresEmptyPorts()
{
	const auto layout = mapPorts({
		makePort(PortDirection::Input, 0),
		makePort(PortDirection::Input, -4),
		makePort(PortDirection::Input, 2, true),
		makePort(PortDirection::Output, 0),
		makePort(PortDirection::Output, 2, true),
	});

	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.inputPortChannels, std::vector<int>{2});
	QCOMPARE(layout.outputPortChannels, std::vector<int>{2});
}

void ClapBusMapTest::testEmptyLayoutIsInvalid()
{
	const auto none = mapPorts({});
	QCOMPARE(none.inputs, 0);
	QCOMPARE(none.outputs, 0);
	QVERIFY(!none.isValid());

	const auto inputOnly = mapPorts({makePort(PortDirection::Input, 2, true)});
	QVERIFY(!inputOnly.isValid());
}

void ClapBusMapTest::testChannelBudgetIsClamped()
{
	const auto layout = mapPorts({
		makePort(PortDirection::Input, 1000, true),
		makePort(PortDirection::Input, 1000),
		makePort(PortDirection::Output, 1000, true),
	});

	QCOMPARE(layout.inputs, static_cast<int>(MaxChannelsPerAudioBuffer));
	QCOMPARE(layout.outputs, static_cast<int>(MaxChannelsPerAudioBuffer));
	// the first port consumes the whole budget, the second one is dropped
	QCOMPARE(layout.inputPortChannels, (std::vector<int>{static_cast<int>(MaxChannelsPerAudioBuffer)}));
}

void ClapBusMapTest::testPortOrderIsPreserved()
{
	const auto layout = mapPorts({
		makePort(PortDirection::Output, 1, true),
		makePort(PortDirection::Output, 2),
		makePort(PortDirection::Input, 2, true),
	});

	QCOMPARE(layout.outputPortChannels, (std::vector<int>{1, 2}));
	QCOMPARE(layout.inputPortChannels, (std::vector<int>{2}));
}

void ClapBusMapTest::testMultiOutputPorts()
{
	const auto layout = mapPorts({
		makePort(PortDirection::Input, 2, true),
		makePort(PortDirection::Output, 2, true),
		makePort(PortDirection::Output, 2),
	});

	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.outputs, 4);
	QCOMPARE(layout.outputPortChannels, (std::vector<int>{2, 2}));
	// only extra *input* ports are side-chains
	QCOMPARE(layout.hasSideChain, false);
}

} // namespace lmms::clap

QTEST_GUILESS_MAIN(lmms::clap::ClapBusMapTest)

#include "ClapBusMapTest.moc"
