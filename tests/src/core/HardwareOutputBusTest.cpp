/*
 * HardwareOutputBusTest.cpp - the hardware bus reads only the frames of its period
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

#include <vector>

#include "HardwareOutputBus.h"
#include "SampleFrame.h"

using namespace lmms;

class HardwareOutputBusTest : public QObject
{
	Q_OBJECT

private slots:
	void outOfRangeFrameReadsAsSilence()
	{
		// Four frames a period; channel 1 at 0.25, channel 2 at 0.9, so a read past the end of
		// channel 1's plane lands on channel 2 and cannot pass for silence.
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<float> first(4, 0.25f);
		const std::vector<float> second(4, 0.9f);
		outputs.addPlane(0, first.data(), 4);
		outputs.addPlane(1, second.data(), 4);
		outputs.swap();  // the written period is the one fed
		const SampleFrame master(0.5f, 0.5f);
		QVERIFY(outputs.monoSample(3, master) == 0.75f);  // the last frame of the period is in range
		QVERIFY2(outputs.monoSample(4, master) == 0.f, "one past the period reads channel 2 or the master");
		QVERIFY2(outputs.sample(0, 4, master) == 0.f, "one past the period reads channel 2 or the master");
		QVERIFY(outputs.monoSample(-1, master) == 0.f);
		QVERIFY(outputs.sample(0, -1, master) == 0.f);
	}
};

QTEST_MAIN(HardwareOutputBusTest)
#include "HardwareOutputBusTest.moc"
