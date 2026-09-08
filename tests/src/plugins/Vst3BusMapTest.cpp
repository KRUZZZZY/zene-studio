/*
 * Vst3BusMapTest.cpp - unit tests for the VST3 bus/channel mapping
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

#include "Vst3BusMap.h"

namespace lmms::vst3
{

class Vst3BusMapTest : public QObject
{
	Q_OBJECT

private slots:
	void testStereoEffect();
	void testMonoToStereo();
	void testInactiveBusesIgnored();
	void testSideChain();
	void testSecondInputBusIsSideChain();
	void testZeroChannelBusIgnored();
	void testQuad();
};

namespace
{

auto bus(BusDescriptor::Direction direction, BusDescriptor::Type type, int channels, bool active = true) -> BusDescriptor
{
	BusDescriptor descriptor;
	descriptor.direction = direction;
	descriptor.type = type;
	descriptor.channelCount = channels;
	descriptor.active = active;
	return descriptor;
}

constexpr auto Input = BusDescriptor::Direction::Input;
constexpr auto Output = BusDescriptor::Direction::Output;
constexpr auto Main = BusDescriptor::Type::Main;
constexpr auto Aux = BusDescriptor::Type::Aux;

} // namespace

void Vst3BusMapTest::testStereoEffect()
{
	const auto layout = mapBuses({bus(Input, Main, 2), bus(Output, Main, 2)});
	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.hasSideChain, false);
	QCOMPARE(layout.inputBusChannels.size(), std::size_t{1});
	QCOMPARE(layout.outputBusChannels.size(), std::size_t{1});
	QCOMPARE(layout.inputBusChannels[0], 2);
	QCOMPARE(layout.outputBusChannels[0], 2);
}

void Vst3BusMapTest::testMonoToStereo()
{
	const auto layout = mapBuses({bus(Input, Main, 1), bus(Output, Main, 2)});
	QCOMPARE(layout.inputs, 1);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.hasSideChain, false);
}

void Vst3BusMapTest::testInactiveBusesIgnored()
{
	const auto layout = mapBuses({bus(Input, Main, 2), bus(Input, Aux, 2, false), bus(Output, Main, 2)});
	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.hasSideChain, false);
}

void Vst3BusMapTest::testSideChain()
{
	const auto layout = mapBuses({bus(Input, Main, 2), bus(Input, Aux, 2), bus(Output, Main, 2)});
	QCOMPARE(layout.inputs, 4);
	QCOMPARE(layout.hasSideChain, true);
	QCOMPARE(layout.inputBusChannels.size(), std::size_t{2});
	QCOMPARE(layout.inputBusChannels[1], 2);
}

void Vst3BusMapTest::testSecondInputBusIsSideChain()
{
	// A second main input bus is still a side chain for LMMS' transport.
	const auto layout = mapBuses({bus(Input, Main, 2), bus(Input, Main, 2), bus(Output, Main, 2)});
	QCOMPARE(layout.inputs, 4);
	QCOMPARE(layout.hasSideChain, true);
}

void Vst3BusMapTest::testZeroChannelBusIgnored()
{
	const auto layout = mapBuses({bus(Input, Main, 2), bus(Input, Aux, 0), bus(Output, Main, 0)});
	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.outputs, 0);
	QCOMPARE(layout.outputBusChannels.empty(), true);
}

void Vst3BusMapTest::testQuad()
{
	const auto layout = mapBuses({bus(Input, Main, 4), bus(Output, Main, 4)});
	QCOMPARE(layout.inputs, 4);
	QCOMPARE(layout.outputs, 4);
	QCOMPARE(layout.inputBusChannels[0], 4);
}

} // namespace lmms::vst3

QTEST_GUILESS_MAIN(lmms::vst3::Vst3BusMapTest)

#include "Vst3BusMapTest.moc"
