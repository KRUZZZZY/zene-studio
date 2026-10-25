/*
 * AutomationPreFirstValueTest.cpp - the pre-first-point contract of an
 *                                   automation clip (Q050-037)
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
 * Free Software Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

// Q050-037: an automation clip whose FIRST node sits later than the clip's own
// start used to read 0 before that node - whichever value the user authored the
// parameter to hold - so a start at the beginning (and every return to it) slammed
// the control to its minimum. The contract this file pins:
//
//   * before the first node the clip reads the authored parameter's INITIAL value
//     (inverse-scaled, the domain the time map stores) - `valueAt()` and, for the
//     audio path, the published ramp read the SAME value, one contract;
//   * a point the author DID draw at the clip's start is the explicit start value:
//     it is the first node, the map serves it, and it wins over the parameter's
//     initial value;
//   * a start, a stop and a return to the start report the same value every time;
//   * each target of a multi-target clip holds ITS OWN initial value on the
//     per-tick apply pass and on the ramp `Song::buildAutomationRamps` publishes;
//   * the whole thing survives a project save/reload, with the interpolation at
//     and after the first node unchanged.
//

#include <QtTest>

#include <cmath>

#include <QFile>
#include <QTemporaryDir>

#include "AutomationClip.h"
#include "AutomationLatency.h"
#include "AutomationRamp.h"
#include "AutomationTrack.h"
#include "AutomatableModel.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "PhaseDMixerTestSupport.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

using namespace lmms;

namespace
{

constexpr float kParamInit = 0.72f;   // the authored initial value of every model here
constexpr float kCurveStart = 0.1f;   // the first node's value, at tick 50
constexpr float kCurveEnd = 0.9f;     // the second node's value, at tick 200
partd::LatentDelayEffect* g_projectLoadEffect = nullptr;

void injectProjectLoadLatency(Mixer* mixer)
{
	if (g_projectLoadEffect != nullptr) { return; }
	EffectChain& chain = mixer->mixerChannel(0)->m_fxChain;
	g_projectLoadEffect = new partd::LatentDelayEffect(&chain, 64);
	chain.appendEffect(g_projectLoadEffect);
}

//! Builds a clip whose curve BEGINS at tick 50: the tick-0 node
//! AutomationClip::addObject seeds is removed, so the pre-first region is real.
AutomationClip* laterFirstNodeClip(AutomationTrack& track, AutomatableModel& model,
	float startValue = kCurveStart, float endValue = kCurveEnd)
{
	auto* clip = new AutomationClip(&track);
	clip->setProgressionType(AutomationClip::ProgressionType::Linear);
	clip->movePosition(0);
	clip->addObject(&model);                     // seeds the tick-0 node with the model's value
	clip->removeNode(TimePos(0));                // ... which the user takes away
	clip->putValue(TimePos(50), startValue, false, true);
	clip->putValue(TimePos(200), endValue, false, true);
	return clip;
}

//! True when the clip really begins at tick 50 (the shape the tests are about).
bool firstNodeIsAtTick50(const AutomationClip* clip)
{
	return clip->hasAutomation() && clip->getTimeMap().constBegin().key() == 50;
}

//! Stops, starts the song at @a tick, renders ONE block and reports the control's value.
float playOneBlockAt(Song* song, const FloatModel& model, int tick)
{
	song->stop();
	song->playSong();
	song->getTimeline().setTicks(tick);
	AutomatableModel::incrementPeriodCounter();
	song->processNextBuffer();
	return model.value();
}

//! The value the time map stores for @a model's authored initial value.
float storedInitialValue(const AutomatableModel& model)
{
	return model.inverseScaledValue(model.initValue<float>());
}

} // namespace


class AutomationPreFirstValueTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The tests here drive the periods themselves; a real device would advance on its own.
		Engine::audioEngine()->audioDev()->stopProcessing();
		QVERIFY(m_dir.isValid());
		m_project = m_dir.filePath(QStringLiteral("pre-first-roundtrip.mmp"));
	}

	void cleanupTestCase() { Engine::destroy(); }

	void loadedProjectPublishesPdcBeforeAutomationIsStamped()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		AutomationLatency& latency = AutomationLatency::instance();
		latency.setEnabled(false);
		auto* track = Track::create(Track::Type::Automation, song);
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		clip->addObject(&Engine::mixer()->mixerChannel(0)->m_volumeModel);
		latency.setEnabled(true);
		const QString project = m_dir.filePath(QStringLiteral("loaded-latency.mmp"));
		const bool saved = song->saveProjectFile(project);

		Mixer::setLatencyUpdateProbeForTest(injectProjectLoadLatency);
		if (saved) { song->loadProject(project); }
		Mixer::setLatencyUpdateProbeForTest(nullptr);

		bool loadedTarget = false;
		int stampedFrames = 0;
		for (Track* songTrack : song->tracks())
		{
			for (Clip* songClip : songTrack->getClips())
			{
				auto* automation = dynamic_cast<AutomationClip*>(songClip);
				if (automation == nullptr) { continue; }
				const auto targets = automation->objectsSnapshot();
				loadedTarget = !targets.empty()
					&& targets.front() == &Engine::mixer()->mixerChannel(0)->m_volumeModel;
				if (loadedTarget) { stampedFrames = automation->latencyFrames(); }
			}
		}

		if (g_projectLoadEffect != nullptr)
		{
			EffectChain& chain = Engine::mixer()->mixerChannel(0)->m_fxChain;
			chain.removeEffect(g_projectLoadEffect);
			delete g_projectLoadEffect;
			g_projectLoadEffect = nullptr;
		}
		latency.setEnabled(false);
		song->clearProject();

		QVERIFY(saved);
		QVERIFY(loadedTarget);
		QCOMPARE(stampedFrames, 64);
	}

	//! Acceptance 1 (the default, no explicit start point): times before the
	//! first node read the authored parameter's initial value, not zero - and
	//! the interpolation at and after the first node is what it always was.
	void beforeTheFirstNodeReadsTheParameterInitialValue()
	{
		Song* song = Engine::getSong();
		FloatModel model{kParamInit, 0.0f, 1.0f, 0.001f};
		QCOMPARE(storedInitialValue(model), kParamInit);
		AutomationTrack track(song);
		AutomationClip* clip = laterFirstNodeClip(track, model);
		QVERIFY(firstNodeIsAtTick50(clip));

		QCOMPARE(clip->valueAt(TimePos(0)), kParamInit);
		QCOMPARE(clip->valueAt(TimePos(25)), kParamInit);
		QCOMPARE(clip->valueAt(TimePos(49)), kParamInit);

		// The same read reports WHERE it was: the playback passes use this flag
		// to apply each target's own initial value without a second clip lock.
		bool beforeFirst = false;
		QCOMPARE(clip->valueAt(TimePos(49), &beforeFirst), kParamInit);
		QVERIFY(beforeFirst);
		beforeFirst = true;
		QCOMPARE(clip->valueAt(TimePos(50), &beforeFirst), kCurveStart);
		QVERIFY(!beforeFirst);

		// The playback read answers for the song's ticks: the parameter's initial
		// value again, through the same map the per-tick apply pass walks.
		QCOMPARE(song->automatedValuesAt(TimePos(10)).value(&model), kParamInit);
		QCOMPARE(song->automatedValuesAt(TimePos(0)).value(&model), kParamInit);

		// AT and after the first node the curve is untouched: the node's out
		// value, the straight line between the two nodes, and the hold past
		// the last node.
		QCOMPARE(clip->valueAt(TimePos(50)), kCurveStart);
		const float midpoint = clip->valueAt(TimePos(125));
		const float expectedMidpoint = kCurveStart + 0.5f * (kCurveEnd - kCurveStart);
		QVERIFY2(std::fabs(midpoint - expectedMidpoint) < 1.0e-5f,
			qPrintable(QStringLiteral("the curve's midpoint reads %1, the interpolation expected %2")
				.arg(midpoint, 0, 'g', 6).arg(expectedMidpoint, 0, 'g', 6)));
		QCOMPARE(clip->valueAt(TimePos(300)), kCurveEnd);
		// The song read agrees there too.
		QCOMPARE(song->automatedValuesAt(TimePos(300)).value(&model), kCurveEnd);
	}

	//! Acceptance 1 (the explicit start value, "when present"): a point the
	//! author drew at the clip's start is the FIRST node - the map serves it,
	//! and it wins over the parameter's initial value. Q050-037 changes nothing
	//! about that case; this pins that the new default cannot wrongly take over.
	void aPointAtTheClipStartIsTheExplicitStartValue()
	{
		Song* song = Engine::getSong();
		FloatModel model{kParamInit, 0.0f, 1.0f, 0.001f};
		AutomationTrack track(song);
		AutomationClip* clip = laterFirstNodeClip(track, model);
		QVERIFY(firstNodeIsAtTick50(clip));
		// The explicit start value: a node at the clip's own first tick.
		clip->putValue(TimePos(0), kCurveStart, false, true);

		QCOMPARE(clip->valueAt(TimePos(0)), kCurveStart);
		QCOMPARE(song->automatedValuesAt(TimePos(0)).value(&model), kCurveStart);
		// And the curve that starts there keeps its nodes.
		QCOMPARE(clip->valueAt(TimePos(200)), kCurveEnd);
	}

	//! Review round 2: a RELATIVE (trim) clip is out of scope. Its value is an
	//! offset from the range's middle, so before its first node it keeps reading
	//! 0 (an offset of -0.5 on a 0..1 range), NOT the parameter's initial value.
	//! The model's initial value (0.8) is deliberately not the range midpoint, so
	//! a pre-first read of it would move the trim offset from -0.5 to +0.3.
	void aRelativeClipKeepsItsOldPreFirstTrimOffset()
	{
		Song* song = Engine::getSong();
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		const double framesPerTick = static_cast<double>(Engine::framesPerTick());
		constexpr float init = 0.8f;
		FloatModel model{init, 0.0f, 1.0f, 0.001f};
		AutomationTrack track(song);
		AutomationClip* clip = laterFirstNodeClip(track, model);
		QVERIFY(firstNodeIsAtTick50(clip));
		clip->setRelative(true);

		// The clip's own read: 0, and it does not claim to be a pre-first parameter read.
		bool beforeFirst = true;
		QCOMPARE(clip->valueAt(TimePos(10), &beforeFirst), 0.0f);
		QVERIFY(!beforeFirst);
		QCOMPARE(clip->valueAt(TimePos(0)), 0.0f);

		// What the trim does to the parameter: base 0.8 + (0 - 0.5) = 0.3, as before.
		const float trimmed = song->automatedValuesAt(TimePos(10)).value(&model);
		QVERIFY2(std::fabs(trimmed - 0.3f) < 1.0e-5f,
			qPrintable(QStringLiteral("the pre-first trim lands on %1, expected 0.3").arg(trimmed, 0, 'g', 6)));

		// The ramp of a relative clip is unchanged too: 0 before the first node.
		AutomationRamp ramp;
		f_cnt_t firstNodeFrame = 99;
		clip->writeBlockRamp(ramp, clip->startPosition(), frames, framesPerTick, 0, &model, &firstNodeFrame);
		QCOMPARE(firstNodeFrame, f_cnt_t{0});
		QVERIFY(ramp.knotCount() > 0);
		for (f_cnt_t f = 0; f < frames; ++f) { QCOMPARE(ramp.valueAt(f), 0.0f); }

		// At and after the first node nothing moved.
		QCOMPARE(clip->valueAt(TimePos(50)), kCurveStart);
		QCOMPARE(clip->valueAt(TimePos(300)), kCurveEnd);
	}

	//! The audio path reads the SAME pre-first value the map read does, for
	//! both a stepped (Discrete) and a moving (Linear) curve - one contract -
	//! and the ramp AT the first node is what it always was.
	void theAudioRampHoldsTheSamePreFirstValue()
	{
		Song* song = Engine::getSong();
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		const double framesPerTick = static_cast<double>(Engine::framesPerTick());
		QVERIFY2(framesPerTick > 2.0, "the test needs a block shorter than one tick");

		for (const auto type : {AutomationClip::ProgressionType::Discrete,
			AutomationClip::ProgressionType::Linear})
		{
			FloatModel model{kParamInit, 0.0f, 1.0f, 0.001f};
			AutomationTrack track(song);
			AutomationClip* clip = laterFirstNodeClip(track, model);
			QVERIFY(firstNodeIsAtTick50(clip));
			clip->setProgressionType(type);

			AutomationRamp ramp;
			// A block at the clip's own start: whatever the block's own length,
			// its every frame sits before tick 50, where the first node lives.
			clip->writeBlockRamp(ramp, clip->startPosition(), frames, framesPerTick, 0);
			QVERIFY2(50.0 * framesPerTick > static_cast<double>(frames),
				qPrintable(QStringLiteral("the block reaches the first node")));
			QVERIFY(ramp.knotCount() > 0);
			QCOMPARE(ramp.refusals(), std::uint32_t{0});
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				QVERIFY2(ramp.valueAt(f) == kParamInit,
					qPrintable(QStringLiteral("frame %1 of the ramp reads %2, not the parameter's "
						"initial value %3").arg(f).arg(ramp.valueAt(f), 0, 'g', 6)
						.arg(kParamInit, 0, 'g', 6)));
			}

			// The same block read as a ramp starting AT the first node: the
			// curve's value there is the node's - interpolation after the
			// first point is unchanged.
			AutomationRamp onNode;
			clip->writeBlockRamp(onNode, TimePos(50), frames, framesPerTick, 0);
			QCOMPARE(onNode.valueAt(0), kCurveStart);
		}
	}

	//! Two targets of one clip whose authored initial values differ: each holds
	//! ITS OWN before the first node on the per-tick apply pass, and the ramps
	//! Song::buildAutomationRamps actually publishes to each model do too. The
	//! second half is the review's point: it exercises the per-target selection
	//! and publication branch in Song, not only the ramp builder directly.
	void eachTargetHoldsItsOwnInitialValueThroughSongPublish()
	{
		Song* song = Engine::getSong();
		constexpr float otherInit = 0.35f;
		FloatModel first{kParamInit, 0.0f, 1.0f, 0.001f};
		FloatModel second{otherInit, 0.0f, 1.0f, 0.001f};
		AutomationTrack track(song);
		AutomationClip* clip = laterFirstNodeClip(track, first);
		QVERIFY(firstNodeIsAtTick50(clip));
		QVERIFY(clip->addObject(&second));

		// The per-tick apply pass.
		const auto atStart = song->automatedValuesAt(TimePos(10));
		QCOMPARE(atStart.value(&first), kParamInit);
		QCOMPARE(atStart.value(&second), otherInit);
		// Past the first node both follow the one curve.
		QCOMPARE(song->automatedValuesAt(TimePos(300)).value(&second), kCurveEnd);

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		const double framesPerTick = static_cast<double>(Engine::framesPerTick());
		QVERIFY2(50.0 * framesPerTick > static_cast<double>(frames),
			"the whole block must lie before the first node");

		// The audio path, through the engine entry that publishes the ramps.
		AutomatableModel::incrementPeriodCounter();
		song->buildAutomationRamps(song->tracks(), TimePos(0), frames, 0);
		const AutomationRamp* firstRamp = first.automationRamp();
		const AutomationRamp* secondRamp = second.automationRamp();
		QVERIFY2(firstRamp != nullptr, "Song published no ramp to the first target");
		QVERIFY2(secondRamp != nullptr, "Song published no ramp to the second target");
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			QVERIFY2(firstRamp->valueAt(f) == kParamInit,
				qPrintable(QStringLiteral("first target frame %1 reads %2, not %3")
					.arg(f).arg(firstRamp->valueAt(f), 0, 'g', 6).arg(kParamInit, 0, 'g', 6)));
			QVERIFY2(secondRamp->valueAt(f) == otherInit,
				qPrintable(QStringLiteral("second target frame %1 reads %2, not %3")
					.arg(f).arg(secondRamp->valueAt(f), 0, 'g', 6).arg(otherInit, 0, 'g', 6)));
		}

		// A block AT the first node publishes the one curve to both targets:
		// the per-target prefix override must not disturb the shared curve.
		AutomatableModel::incrementPeriodCounter();
		song->buildAutomationRamps(song->tracks(), TimePos(50), frames, 0);
		firstRamp = first.automationRamp();
		secondRamp = second.automationRamp();
		QVERIFY(firstRamp != nullptr);
		QVERIFY(secondRamp != nullptr);
		QCOMPARE(firstRamp->valueAt(0), kCurveStart);
		QCOMPARE(secondRamp->valueAt(0), kCurveStart);

		// A block that STRADDLES the first node: the prefix override must stop
		// exactly at the first frame on or after the node, leaving the shared
		// curve from there on untouched. Tick 49 plus 300 frames starts the
		// block just before tick 50, so the node falls inside the block.
		constexpr int kFrameOffsetInTick = 300;
		const double nodeFrame = (50.0 - 49.0) * framesPerTick
			- static_cast<double>(kFrameOffsetInTick);
		QVERIFY2(nodeFrame > 0.0 && nodeFrame < static_cast<double>(frames),
			"the fixture must put the first node inside the block");
		const f_cnt_t firstNodeFrame = static_cast<f_cnt_t>(std::ceil(nodeFrame));
		QVERIFY(firstNodeFrame > 0 && firstNodeFrame < frames);

		AutomatableModel::incrementPeriodCounter();
		song->buildAutomationRamps(song->tracks(), TimePos(49), frames, kFrameOffsetInTick);
		firstRamp = first.automationRamp();
		secondRamp = second.automationRamp();
		QVERIFY(firstRamp != nullptr);
		QVERIFY(secondRamp != nullptr);
		for (f_cnt_t f = 0; f < firstNodeFrame; ++f)
		{
			QVERIFY2(firstRamp->valueAt(f) == kParamInit,
				qPrintable(QStringLiteral("straddle: first target frame %1 reads %2, not %3")
					.arg(f).arg(firstRamp->valueAt(f), 0, 'g', 6).arg(kParamInit, 0, 'g', 6)));
			QVERIFY2(secondRamp->valueAt(f) == otherInit,
				qPrintable(QStringLiteral("straddle: second target frame %1 reads %2, not %3")
					.arg(f).arg(secondRamp->valueAt(f), 0, 'g', 6).arg(otherInit, 0, 'g', 6)));
		}
		// From the first frame on/after the node both targets share the curve.
		// The node frame is the first sample at/after tick 50, so it sits just
		// past the node's exact position (the frame grid is finer than a tick).
		QVERIFY2(std::fabs(firstRamp->valueAt(firstNodeFrame) - kCurveStart) < 1.0e-3f,
			qPrintable(QStringLiteral("straddle: first frame after the node reads %1, not the curve's %2")
				.arg(firstRamp->valueAt(firstNodeFrame), 0, 'g', 8).arg(kCurveStart, 0, 'g', 8)));
		QVERIFY2(std::fabs(secondRamp->valueAt(firstNodeFrame) - kCurveStart) < 1.0e-3f,
			qPrintable(QStringLiteral("straddle: second target at the node frame reads %1, not the curve's %2")
				.arg(secondRamp->valueAt(firstNodeFrame), 0, 'g', 8).arg(kCurveStart, 0, 'g', 8)));
		for (f_cnt_t f = firstNodeFrame; f < frames; ++f)
		{
			QVERIFY2(firstRamp->valueAt(f) == secondRamp->valueAt(f),
				qPrintable(QStringLiteral("straddle: targets disagree at frame %1 after the node")
					.arg(f)));
		}
	}

	//! Acceptance 2: start before the first node, stop, seek and return to the
	//! start - playback reports the same value each time.
	void playbackReportsTheSameValueAtEveryReturnToStart()
	{
		Song* song = Engine::getSong();
		FloatModel model{kParamInit, 0.0f, 1.0f, 0.001f};
		AutomationTrack track(song);
		AutomationClip* clip = laterFirstNodeClip(track, model);
		QVERIFY(firstNodeIsAtTick50(clip));

		// FIRST start at tick 0: the parameter lands on its own initial value, even
		// from a stale one the automation read has to correct.
		model.setValue(0.0f);
		const float firstStart = playOneBlockAt(song, model, 0);
		QCOMPARE(firstStart, kParamInit);

		// SEEK into the curve and play: the control moves onto the curve, which is
		// what makes the return-to-start below non-vacuous.
		const float intoCurve = playOneBlockAt(song, model, 150);
		QVERIFY2(intoCurve > kCurveStart && intoCurve < kCurveEnd,
			qPrintable(QStringLiteral("the seek into the curve reports %1").arg(intoCurve, 0, 'g', 6)));

		// RETURN TO START: the same value the first start reported, every time.
		for (int i = 0; i < 2; ++i)
		{
			const float again = playOneBlockAt(song, model, 0);
			QVERIFY2(again == firstStart,
				qPrintable(QStringLiteral("return %1 to the start reads %2, the first start reported %3")
					.arg(i).arg(again, 0, 'g', 6).arg(firstStart, 0, 'g', 6)));
			// A seek that stays BEFORE the first node reads it too.
			QCOMPARE(playOneBlockAt(song, model, 20), firstStart);
		}
		song->stop();
	}

	//! Acceptance 3: the whole project round trip. A clip the user moved the
	//! first node of keeps its pre-first contract through a save and a reload,
	//! and the interpolation at and after the first node is bit-identical.
	void saveAndReloadKeepsThePreFirstContract()
	{
		Song* song = Engine::getSong();
		float preFirstBefore = 0.0f;
		float atNodeBefore = 0.0f;
		float interpolatedBefore = 0.0f;
		float heldBefore = 0.0f;

		{
			AutomationTrack track(song);
			// A model the PROJECT owns (the master volume, authored initial value 100): a
			// local model would be gone on reload and the clip would have no target.
			AutomatableModel& model = song->masterVolumeModel();
			AutomationClip* clip = laterFirstNodeClip(track, model, 20.0f, 180.0f);
			QVERIFY(firstNodeIsAtTick50(clip));
			preFirstBefore = song->automatedValuesAt(TimePos(0)).value(&model);
			atNodeBefore = clip->valueAt(TimePos(50));
			interpolatedBefore = clip->valueAt(TimePos(125));
			heldBefore = clip->valueAt(TimePos(300));
			QVERIFY(song->saveProjectFile(m_project));
		}  // the track and the clip go with their scope, before the reload

		QVERIFY(QFile::exists(m_project));
		song->loadProject(m_project);

		AutomationClip* reloaded = nullptr;
		AutomatableModel* reloadedModel = nullptr;
		for (Track* t : song->tracks())
		{
			if (reloaded == nullptr && t->type() == Track::Type::Automation && !t->getClips().empty())
			{
				reloaded = dynamic_cast<AutomationClip*>(t->getClips()[0]);
				if (reloaded != nullptr) { reloadedModel = const_cast<AutomatableModel*>(reloaded->firstObject()); }
			}
		}
		QVERIFY(reloaded != nullptr);
		QVERIFY(reloadedModel != nullptr);
		const int firstTick = reloaded->getTimeMap().constBegin().key();
		QVERIFY2(firstTick == 50, qPrintable(QString::number(firstTick)));

		// The pre-first contract is stable: the reloaded model's authored initial
		// value, exactly what the pre-save read reported.
		QCOMPARE(reloaded->valueAt(TimePos(0)), preFirstBefore);
		QCOMPARE(song->automatedValuesAt(TimePos(0)).value(reloadedModel), preFirstBefore);
		QCOMPARE(preFirstBefore, 100.0f);
		QCOMPARE(reloadedModel, static_cast<AutomatableModel*>(&song->masterVolumeModel()));
		// ... and the interpolation at and after the first node is untouched.
		QCOMPARE(reloaded->valueAt(TimePos(50)), atNodeBefore);
		QCOMPARE(reloaded->valueAt(TimePos(125)), interpolatedBefore);
		QCOMPARE(reloaded->valueAt(TimePos(300)), heldBefore);
	}

private:
	QString m_project;
	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(AutomationPreFirstValueTest)
#include "AutomationPreFirstValueTest.moc"
