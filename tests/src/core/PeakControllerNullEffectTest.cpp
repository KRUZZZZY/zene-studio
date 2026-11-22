/*
 * PeakControllerNullEffectTest.cpp - the null-effect regression for
 *                                     PeakController's default argument
 *                                     (cluster new-p9-340).
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include "AudioEngine.h"
#include "Controller.h"
#include "Engine.h"
#include "LmmsTypes.h"
#include "PeakController.h"
#include "Song.h"

using namespace lmms;

//! PeakController's constructor accepts a null PeakControllerEffect* by
//! default. This binary pins that supported state: construction must not
//! dereference the effect, and each period must fill the value buffer with
//! silence (the null branch of updateValueBuffer).
class PeakControllerNullEffectTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::getSong() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! The finding's exact case: only the parent argument. A null effect must
	//! not crash construction, and the first period must be silent.
	void defaultNullEffectConstructsAndIsSilent()
	{
		PeakController controller(Engine::getSong());
		Controller::triggerFrameCounter();
		QCOMPARE(controller.currentValue(0), 0.0f);
	}

	//! The same null state through the explicit argument, across several
	//! periods and at the last frame of each period, so the null branch is
	//! exercised after the buffer has been handed out and re-requested.
	void explicitNullEffectStaysSilentAcrossPeriods()
	{
		PeakController controller(Engine::getSong(), nullptr);
		const f_cnt_t lastFrame = Engine::audioEngine()->framesPerPeriod() - 1;
		for (int period = 0; period < 4; ++period)
		{
			Controller::triggerFrameCounter();
			QCOMPARE(controller.currentValue(0), 0.0f);
			QCOMPARE(controller.currentValue(lastFrame), 0.0f);
		}
	}
};

QTEST_MAIN(PeakControllerNullEffectTest)
#include "PeakControllerNullEffectTest.moc"
