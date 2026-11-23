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
		// The mono downmix of both planes (0.25 + 0.9) plus the averaged master (0.5): 1.65.
		QVERIFY(qAbs(outputs.monoSample(3, master) - 1.65f) < 1e-5f);  // the last frame of the period is in range
		QVERIFY2(outputs.monoSample(4, master) == 0.f, "one past the period reads channel 2 or the master");
		QVERIFY2(outputs.sample(0, 4, master) == 0.f, "one past the period reads channel 2 or the master");
		QVERIFY(outputs.monoSample(-1, master) == 0.f);
		QVERIFY(outputs.sample(0, -1, master) == 0.f);
	}

	void stereoRouteAtChannelLimitAddsNothing()
	{
		// A width-2 route at the last channel has no second channel to go to: the whole route is dropped,
		// not its left half alone. The bus holds MaxChannels channels, so MaxChannels - 1 is the last one.
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> src(4, SampleFrame(0.5f, 0.75f));
		outputs.add(HardwareOutputBus::MaxChannels - 1, 2, src.data(), 4, 1.f);
		outputs.swap();
		const SampleFrame zero(0.f, 0.f);
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			QVERIFY2(outputs.sample(HardwareOutputBus::MaxChannels - 1, f, zero) == 0.f,
				"a stereo route past the last channel left its left half on the bus");
		}
	}

	void stereoRouteAtLastFullPairStillPlays()
	{
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> src(4, SampleFrame(0.25f, 0.75f));
		outputs.add(HardwareOutputBus::MaxChannels - 2, 2, src.data(), 4, 1.f);
		outputs.swap();
		const SampleFrame zero(0.f, 0.f);
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			QVERIFY2(outputs.sample(HardwareOutputBus::MaxChannels - 2, f, zero) == 0.25f, "left half of the last full pair is missing");
			QVERIFY2(outputs.sample(HardwareOutputBus::MaxChannels - 1, f, zero) == 0.75f, "right half of the last full pair is missing");
		}
	}

	void monoRouteAtLastChannelStillPlays()
	{
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> src(4, SampleFrame(0.5f, 0.7f));
		outputs.add(HardwareOutputBus::MaxChannels - 1, 1, src.data(), 4, 1.f);
		outputs.swap();
		const SampleFrame zero(0.f, 0.f);
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			// Width 1 writes the average of the two sides: 0.5 * (0.5 + 0.7) = 0.6.
			QVERIFY2(qAbs(outputs.sample(HardwareOutputBus::MaxChannels - 1, f, zero) - 0.6f) < 1e-5f,
				"a mono route on the last channel is not written");
		}
	}

	void monoOutputDownmixesBothSidesOfAStereoRoute()
	{
		// A one-channel device has one output, so a stereo route's two planes must be folded into
		// it, not just the left one. 0.5 * (0.25 + 0.75) = 0.5.
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> src(4, SampleFrame(0.25f, 0.75f));
		outputs.add(0, 2, src.data(), 4, 1.f);
		outputs.setMasterPlacement({-1, 2});
		outputs.swap();
		const SampleFrame master(0.f, 0.f);
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			QVERIFY2(qAbs(outputs.monoSample(f, master) - 0.5f) < 1e-5f,
				"the mono output drops the right side of a stereo route");
		}
	}

	void monoOutputKeepsAMonoRouteAtItsLevel()
	{
		// A width-1 route writes the average of the pair into its one plane. Folding it into mono
		// must use that level as it stands, not halve it again: 0.5 * (0.5 + 0.7) = 0.6.
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> src(4, SampleFrame(0.5f, 0.7f));
		outputs.add(0, 1, src.data(), 4, 1.f);
		outputs.setMasterPlacement({-1, 2});
		outputs.swap();
		const SampleFrame master(0.f, 0.f);
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			QVERIFY2(qAbs(outputs.monoSample(f, master) - 0.6f) < 1e-5f,
				"a mono route is not kept at its intended level");
		}
	}

	void monoOutputFoldsEveryRoutedChannel()
	{
		// The bus can carry routes beyond the first pair; a mono device plays them all. Each route
		// contributes its own downmix: 0.5 * (0.2 + 0.4) + 0.5 * (0.6 + 0.8) + 0.5 = 1.5.
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> first(4, SampleFrame(0.2f, 0.4f));
		const std::vector<SampleFrame> second(4, SampleFrame(0.6f, 0.8f));
		const std::vector<float> side(4, 0.5f);
		outputs.add(0, 2, first.data(), 4, 1.f);
		outputs.add(2, 2, second.data(), 4, 1.f);
		outputs.addPlane(4, side.data(), 4, 1.f);
		outputs.setMasterPlacement({-1, 2});
		outputs.swap();
		const SampleFrame master(0.f, 0.f);
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			QVERIFY2(qAbs(outputs.monoSample(f, master) - 1.5f) < 1e-5f,
				"a route past the first pair is missing from the mono output");
		}
	}

	void monoOutputAddsTheMasterOnlyOnItsPlacement()
	{
		// The master's own mono downmix is unchanged: averaged when it sits on the first channel,
		// absent when it is routed elsewhere.
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> src(4, SampleFrame(0.25f, 0.75f));
		outputs.add(0, 2, src.data(), 4, 1.f);
		const SampleFrame master(0.4f, 0.6f);
		outputs.swap();
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			QVERIFY2(qAbs(outputs.monoSample(f, master) - 1.0f) < 1e-5f,
				"the master on 1/2 is not averaged into the mono output");
		}
		HardwareOutputBus moved;
		moved.init(4);
		moved.add(0, 2, src.data(), 4, 1.f);
		moved.setMasterPlacement({4, 2});
		moved.swap();
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			QVERIFY2(qAbs(moved.monoSample(f, master) - 0.5f) < 1e-5f,
				"a master routed away from the first channel leaks into the mono output");
		}
	}

	void monoDownmixOfADroppedRouteStaysEmpty()
	{
		// A route that does not fit the bus is dropped whole: it must not reach the mono downmix
		// either.
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> src(4, SampleFrame(0.5f, 0.75f));
		outputs.add(HardwareOutputBus::MaxChannels - 1, 2, src.data(), 4, 1.f);
		outputs.setMasterPlacement({-1, 2});
		outputs.swap();
		const SampleFrame zero(0.f, 0.f);
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			QVERIFY2(outputs.monoSample(f, zero) == 0.f,
				"a dropped route left its downmix on the mono output");
		}
	}

	void monoDownmixClearsOnSwap()
	{
		// The downmix is per period like the planes: a second swap leaves no residue from the first.
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> src(4, SampleFrame(0.25f, 0.75f));
		outputs.add(0, 2, src.data(), 4, 1.f);
		outputs.setMasterPlacement({-1, 2});
		outputs.swap();
		const SampleFrame master(0.f, 0.f);
		QVERIFY(qAbs(outputs.monoSample(0, master) - 0.5f) < 1e-5f);
		outputs.swap();  // nothing was written to this period
		QVERIFY2(outputs.monoSample(0, master) == 0.f, "the previous period's downmix stayed on the mono output");
	}

	void droppedRouteLeavesNoResidue()
	{
		// The first route is dropped at the limit; its left half must not stay on channel 127 to be added
		// into the second route's right half.
		HardwareOutputBus outputs;
		outputs.init(4);
		const std::vector<SampleFrame> dropped(4, SampleFrame(0.5f, 0.5f));
		const std::vector<SampleFrame> kept(4, SampleFrame(0.25f, 0.75f));
		outputs.add(HardwareOutputBus::MaxChannels - 1, 2, dropped.data(), 4, 1.f);
		outputs.add(HardwareOutputBus::MaxChannels - 2, 2, kept.data(), 4, 1.f);
		outputs.swap();
		const SampleFrame zero(0.f, 0.f);
		for (f_cnt_t f = 0; f < 4; ++f)
		{
			QVERIFY2(outputs.sample(HardwareOutputBus::MaxChannels - 2, f, zero) == 0.25f, "the kept route's left half is wrong");
			QVERIFY2(outputs.sample(HardwareOutputBus::MaxChannels - 1, f, zero) == 0.75f, "residue of the dropped route is added into the kept route");
		}
	}
};

QTEST_MAIN(HardwareOutputBusTest)
#include "HardwareOutputBusTest.moc"
