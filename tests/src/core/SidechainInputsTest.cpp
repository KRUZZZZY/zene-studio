/*
 * SidechainInputsTest.cpp - a hosted plug-in.s aux bus hears the mixer sidechain
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

#include <QtTest>

#include <array>

#include "AudioBuffer.h"
#include "SidechainInputs.h"

using namespace lmms;

//! Engine audit E9: sidechainInputs() points the aux-bus channels at the sidechain signal and
//! leaves the main channels alone; with no sidechain, or no aux bus, the input is passed through.
class SidechainInputsTest : public QObject
{
	Q_OBJECT

private slots:
	void theAuxBusHearsTheSidechain()
	{
		std::array<float, 4> a{}, b{}, c{}, d{};
		const float* in[] = {a.data(), b.data(), c.data(), d.data()};
		AudioBuffer sidechain(4, 2);
		std::array<const float*, MaxHostedInputChannels> scratch{};
		const float* const* fed = sidechainInputs(in, 4, &sidechain, 2, scratch);
		QCOMPARE(fed[0], in[0]);
		QCOMPARE(fed[1], in[1]);
		QCOMPARE(fed[2], sidechain.group(0).buffer(0));
		QCOMPARE(fed[3], sidechain.group(0).buffer(1));
	}

	void withoutASidechainTheInputPassesThrough()
	{
		std::array<float, 4> a{}, b{};
		const float* in[] = {a.data(), b.data()};
		std::array<const float*, MaxHostedInputChannels> scratch{};
		QCOMPARE(sidechainInputs(in, 2, nullptr, 0, scratch), static_cast<const float* const*>(in));
		AudioBuffer sidechain(4, 2);
		QCOMPARE(sidechainInputs(in, 2, &sidechain, -1, scratch), static_cast<const float* const*>(in));
	}
};

QTEST_GUILESS_MAIN(SidechainInputsTest)
#include "SidechainInputsTest.moc"
