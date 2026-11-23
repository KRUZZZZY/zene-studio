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

#include <atomic>
#include <memory>
#include <vector>

#include "AudioBus.h"
#include "AudioEngine.h"
#include "Controller.h"
#include "Effect.h"
#include "Engine.h"
#include "LmmsTypes.h"
#include "PeakController.h"
#include "PeakControllerEffect.h"
#include "Song.h"

using namespace lmms;

namespace
{

// The GUI thread's attack change, injected at the point where the render thread has
// just computed the period's coefficients (cluster new-p20-95). The real slot is the
// signal the effect's attack model emits through a Qt::DirectConnection.
PeakControllerEffect* s_probeEffect = nullptr;
float s_probeAttack = 0.0f;
bool s_probeFired = false;

void changeAttackInsidePublication(PeakController*)
{
	s_probeFired = true;
	s_probeEffect->attackModel()->setValue(s_probeAttack);
}

} // namespace

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

	//! cluster new-p20-95: the coefficient dirty flag crosses threads. updateCoeffs()
	//! runs on the thread that owns the attack/decay models - the GUI, through the
	//! Qt::DirectConnection the constructor installs - and updateValueBuffer() consumes
	//! it on the render thread. A plain bool there is a data race on every attack or
	//! decay change made while playback runs, so it has to be an atomic handoff.
	void coeffDirtyFlagIsAtomicHandoff()
	{
		PeakController controller(Engine::getSong());
		const bool readable = [](auto& c) {
			return requires { c.m_coeffNeedsUpdate.load(std::memory_order_acquire); };
		}(controller);
		const bool claimable = [](auto& c) {
			return requires { c.m_coeffNeedsUpdate.exchange(false, std::memory_order_acq_rel); };
		}(controller);
		const bool writable = [](auto& c) {
			return requires { c.m_coeffNeedsUpdate.store(true, std::memory_order_release); };
		}(controller);
		QVERIFY2(readable, "m_coeffNeedsUpdate is a plain field the render thread reads unsynchronised");
		QVERIFY2(claimable, "m_coeffNeedsUpdate is a plain field the render thread cannot claim");
		QVERIFY2(writable, "m_coeffNeedsUpdate is a plain field the GUI thread writes unsynchronised");
		QVERIFY2(std::atomic<bool>::is_always_lock_free, "the handoff must not lock on the render thread");
	}

	//! cluster new-p20-95, the finding's exact interleaving: the GUI changes the attack
	//! while the render thread is publishing the period's coefficients - made
	//! deterministic by the publication seam instead of by timing. The change must
	//! survive to the following period. Before the fix the flag was published first and
	//! cleared after, so a change arriving in that window was overwritten and the
	//! coefficients stayed stale until the next attack/decay change.
	void attackChangeDuringPublicationIsNotLost()
	{
		auto effect = std::make_unique<PeakControllerEffect>(nullptr, nullptr);
		PeakController* const controller = effect->controller();
		QVERIFY(controller != nullptr);
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);
		measureSilence(*effect, frames);
		QCOMPARE(effect->lastSample(), 0.5f);

		// Period 1 publishes the coefficients for the models as they stand.
		Controller::triggerFrameCounter();
		const float firstPeriod = controller->currentValue(0);
		QVERIFY2(firstPeriod > 0.0f, qPrintable(QStringLiteral("period 1 ramped to %1").arg(firstPeriod)));

		// The GUI thread asks for the coefficients again, as a dataChanged slot does.
		effect->attackModel()->setValue(0.002f);

		// Period 2 starts the ramp over, and the GUI's real change - a slow attack -
		// lands inside its publication window.
		controller->m_currentSample = 0.0f;
		s_probeEffect = effect.get();
		s_probeAttack = 0.9f;
		s_probeFired = false;
		PeakController::setCoeffPublishProbeForTest(&changeAttackInsidePublication);
		Controller::triggerFrameCounter();
		(void)controller->currentValue(0);
		PeakController::setCoeffPublishProbeForTest(nullptr);
		QVERIFY2(s_probeFired, "the publication seam did not run: period 2 published no coefficients");

		// Period 3 must ramp with the attack the GUI asked for. Its coefficient is
		// 1 - 2^(-0.3 * (1 - 0.9) * 44100 / rate) ~ 0.021, so the first sample is about
		// 0.5 * 0.021 = 0.010; the stale coefficient for 0.002 is about 0.187, which
		// would give 0.094 instead.
		controller->m_currentSample = 0.0f;
		Controller::triggerFrameCounter();
		const float thirdPeriod = controller->currentValue(0);
		QVERIFY2(thirdPeriod > 0.0f, qPrintable(QStringLiteral("period 3 ramped to %1").arg(thirdPeriod)));
		QVERIFY2(thirdPeriod < 0.03f,
			qPrintable(QStringLiteral("the attack change made during publication was lost: period 3 began at %1, "
				"which is the stale coefficient for the previous attack").arg(thirdPeriod)));
		QVERIFY2(!static_cast<bool>(controller->m_coeffNeedsUpdate),
			"the dirty flag stayed set after the recalculation was published");
	}

private:
	//! Feeds one silent block through the effect, so its output holds the base value
	//! (0.5) and the controller ramps towards it with the attack coefficient.
	static void measureSilence(Effect& effect, f_cnt_t frames)
	{
		std::vector<SampleFrame> storage(static_cast<std::size_t>(frames));
		for (SampleFrame& frame : storage)
		{
			frame[0] = 0.0f;
			frame[1] = 0.0f;
		}
		SampleFrame* busData[1] = { storage.data() };
		AudioBus bus{busData, 1, frames};
		effect.processAudioBuffer(bus);
	}
};

QTEST_MAIN(PeakControllerNullEffectTest)
#include "PeakControllerNullEffectTest.moc"
