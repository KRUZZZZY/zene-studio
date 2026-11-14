/*
 * ControlRoomTest.cpp - the monitor section: finite trims and dim values cannot overflow the gain
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
/*! Luna cluster new-p12-59: setState() replaced non-finite dB values but let finite ones through;
 *  a trim of 10000 dB made linear() return an infinite gain and apply() wrote infinities into
 *  the output. Trims clamp to -24..12 dB and dim_db to -60..0 dB - the ranges
 *  monitor.control_room takes - before the gain is published. */

#include <QtTest>

#include <array>
#include <cmath>

#include "Engine.h"
#include "ControlRoom.h"
#include "SampleFrame.h"

using namespace lmms;

class ControlRoomTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void theControlRoomClampsExtremeFiniteGains()
	{
		const controlroom::State previous = controlroom::state();
		controlroom::State bad = previous;
		bad.speaker = 0;
		bad.trimsDb[0] = 10000.f;   // finite, but 10^(10000/20) overflows a float gain
		bad.trimsDb[1] = -10000.f;
		bad.dim = true;
		bad.dimDb = 10000.f;        // above the supported 0 dB ceiling
		bad.mono = false;
		bad.cut = false;
		controlroom::setState(bad);
		const controlroom::State settled = controlroom::state();
		QVERIFY(settled.trimsDb[0] <= 12.f);    // the monitor.control_room range, not infinity
		QVERIFY(settled.trimsDb[1] >= -24.f);
		QVERIFY(settled.dimDb <= 0.f && settled.dimDb >= -60.f);
		std::array<SampleFrame, 4> heard;
		heard.fill(SampleFrame(1.f, 0.5f));
		controlroom::apply(heard.data(), 4);  // must not turn samples into infinities
		QVERIFY(std::isfinite(heard[0][0]));
		QVERIFY(std::isfinite(heard[0][1]));
		bad.dimDb = -10000.f;                // below the supported -60 dB floor
		controlroom::setState(bad);
		const controlroom::State dimmed = controlroom::state();
		QVERIFY(dimmed.dimDb >= -60.f);
		heard.fill(SampleFrame(1.f, 0.5f));
		controlroom::apply(heard.data(), 4);
		QVERIFY(std::isfinite(heard[0][0]));
		controlroom::setState(previous);
	}
};

QTEST_MAIN(ControlRoomTest)
#include "ControlRoomTest.moc"
