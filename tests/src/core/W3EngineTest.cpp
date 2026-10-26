/*
 * W3EngineTest.cpp - latency-compensated automation, post-fader inserts, receives, pre-fades
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
/*! 0.5.0 W3 engine lane: the offset arithmetic of latency-compensated automation and a curve read where the
 *  latent audio is, opt-in and undoable (MX-160); post-fader inserts skipped while empty, baking the fader
 *  when not, addressed as ch-<n>/post and saved only when they hold devices (MX-082); chain.receive's
 *  refusals, and the send and device it makes, undone in one step (MX-063); the pre-fade envelope, and a trimmed
 *  clip that starts sounding its hidden audio before its start, one undo step (ED-179). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QSemaphore>

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include "AutomationClip.h"
#include "AutomationLatency.h"
#include "AutomationRamp.h"
#include "AutomationTrack.h"
#include "AutomatableModel.h"
#include "ClipEdits.h"
#include "ClipPreFades.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerPostInserts.h"
#include "PhaseDMixerTestSupport.h"
#include "ProjectJournal.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

AutomationClip* g_objectsReadClip = nullptr;
AutomatableModel* g_objectsReadTarget = nullptr;

void removeTargetAfterObjectsUnlock(const AutomationClip* clip)
{
	if (clip == g_objectsReadClip) { const_cast<AutomationClip*>(clip)->removeObject(g_objectsReadTarget); }
}

MixerChannel* channel(int index) { return Engine::mixer()->mixerChannel(index); }

QString ch(int index) { return control::channelIdOf(channel(index)); }

//! Stops refresh while it is calculating the clip behind this effect, after the earlier clip was visited.
class RefreshGateEffect final : public partd::LatentDelayEffect
{
public:
	using partd::LatentDelayEffect::LatentDelayEffect;

	int latencyFrames() const override
	{
		if (m_blockNext.exchange(false, std::memory_order_relaxed))
		{
			m_entered.release();
			m_resume.acquire();
		}
		return partd::LatentDelayEffect::latencyFrames();
	}

	void arm() const { m_blockNext.store(true, std::memory_order_relaxed); }
	bool waitUntilEntered() const { return m_entered.tryAcquire(1, 3000); }
	void resume() const { m_resume.release(); }

private:
	mutable std::atomic<bool> m_blockNext{false};
	mutable QSemaphore m_entered;
	mutable QSemaphore m_resume;
};

//! The value the per-tick read gives @a model at song tick @a tick.
float readAt(const AutomatableModel* model, int tick)
{
	return Engine::getSong()->automatedValuesAt(TimePos(tick)).value(const_cast<AutomatableModel*>(model), -1.f);
}

//! The built-in effect named @a name loaded onto @a target through plugin.load: its fx-<n>, or empty.
QString loadBuiltin(const QString& target, const QString& name)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
			.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject entry = device.toObject();
		if (entry.value(QStringLiteral("name")).toString() != name || !entry.value(QStringLiteral("loadable")).toBool()) { continue; }
		return run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), entry.value(QStringLiteral("id"))}})
			.result.value(QStringLiteral("id")).toString();
	}
	return QString();
}

//! Turns the journal off for one scope: a restore that is not an undo step, as Song::loadProject does for a project
//! load (Song.cpp, setJournalling(false) before the mixer loads).
class JournalOff
{
public:
	JournalOff() : m_journal(Engine::projectJournal()), m_was(m_journal->isJournalling()) { m_journal->setJournalling(false); }
	~JournalOff() { m_journal->setJournalling(m_was); }
	JournalOff(const JournalOff&) = delete;
	JournalOff& operator=(const JournalOff&) = delete;

private:
	ProjectJournal* m_journal;
	bool m_was;
};

//! A one-second 440 Hz tone on @a track at tick @a at.
SampleClip* toneClip(Track* track, tick_t at)
{
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	std::vector<SampleFrame> frames;
	for (int i = 0; i < rate; ++i)
	{
		const auto v = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * i / rate));
		frames.emplace_back(v, v);
	}
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(at)));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(frames.data(), frames.size(), rate));
	clip->movePosition(TimePos(at));
	return clip;
}

} // namespace

class W3EngineTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		partd::initEngine();  // the device stopped: the test sets and reads the PDC values itself
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		partd::destroyEngine();
	}

	void theOffsetArithmetic()
	{
		QCOMPARE(AutomationLatency::ticksFor(0, 100.0), 0);
		QCOMPARE(AutomationLatency::ticksFor(449, 100.0), 4);
		QCOMPARE(AutomationLatency::ticksFor(451, 100.0), 5);
		TimePos start(20);
		int inTick = 30;
		QVERIFY(AutomationLatency::shiftFrames(1000, 100.0, &start, &inTick));  // 2030 - 1000 = 1030 frames
		QCOMPARE(start.getTicks(), tick_t{10});
		QCOMPARE(inTick, 30);
		start = TimePos(3);
		inTick = 0;
		QVERIFY(!AutomationLatency::shiftFrames(400, 100.0, &start, &inTick));  // before the song's start
		start = TimePos(50);
		inTick = 7;
		QVERIFY(AutomationLatency::shiftFrames(0, 100.0, &start, &inTick));  // no latency: untouched
		QCOMPARE(start.getTicks(), tick_t{50});
		QCOMPARE(inTick, 7);
		start = TimePos(100);
		inTick = 12;
		QVERIFY(AutomationLatency::shiftFrames(777, 91.875, &start, &inTick));  // a fractional tick keeps the frame
		const double moved = start.getTicks() * 91.875 + inTick;
		QVERIFY2(std::abs(moved - (100 * 91.875 + 12 - 777)) < 1.0, qPrintable(QString::number(moved)));
	}

	void automationReadsWhereTheLatentAudioIs()
	{
		EffectChain& chain = channel(1)->m_fxChain;
		auto* latent = new partd::LatentDelayEffect(&chain, 4000);
		auto* after = new partd::FixedGainEffect(&chain, 1.f, 1.f);
		chain.appendEffect(latent);
		chain.appendEffect(after);
		channel(1)->setInputLatencyFrames(100);
		auto* probe = new FloatModel(0.f, 0.f, 1.f, 0.001f, after, QStringLiteral("Probe"));
		QCOMPARE(AutomationLatency::upstreamFrames(probe), 4100);  // the channel's alignment plus the device before
		QCOMPARE(AutomationLatency::upstreamFrames(&channel(1)->m_volumeModel), 4100);  // the fader: after both devices

		auto* track = Track::create(Track::Type::Automation, Engine::getSong());
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		clip->addObject(probe);
		clip->setProgressionType(AutomationClip::ProgressionType::Linear);
		clip->putValue(TimePos(0), 0.f, false);
		clip->putValue(TimePos(192), 1.f, false);
		const float drawn = readAt(probe, 96);

		QVERIFY(!AutomationLatency::instance().shouldPersist());
		const ControlResult on = run(QStringLiteral("automation.latency_compensation"), {{QStringLiteral("enabled"), true}});
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		QCOMPARE(clip->latencyFrames(), 4100);
		QVERIFY(on.result.value(QStringLiteral("compensated_clips")).toInt() >= 1);
		const int lag = AutomationLatency::ticksFor(4100, Engine::framesPerTick());
		QVERIFY(lag >= 1);
		const float heard = readAt(probe, 96);
		QVERIFY(AutomationLatency::instance().shouldPersist());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!AutomationLatency::instance().enabled());
		QCOMPARE(readAt(probe, 96), drawn);  // off: read where it is drawn, as before
		QCOMPARE(heard, readAt(probe, 96 - lag));  // on: the curve as it was lag ticks earlier

		delete track;
		chain.removeEffect(latent);
		chain.removeEffect(after);
		delete latent;
		delete after;  // and the probe with it
		channel(1)->setInputLatencyFrames(0);
	}

	void latencyRefreshKeepsOneReadOnPublishedClipValues()
	{
		AutomationLatency& latency = AutomationLatency::instance();
		latency.setEnabled(false);

		EffectChain& chain = channel(1)->m_fxChain;
		channel(1)->setInputLatencyFrames(0);
		auto* firstEffect = new RefreshGateEffect(&chain, 1, 1);
		auto* secondEffect = new partd::FixedGainEffect(&chain, 1.f, 1.f);
		chain.appendEffect(firstEffect);
		chain.appendEffect(secondEffect);

		auto* firstModel = new FloatModel(0.f, 0.f, 1.f, 0.001f, firstEffect, QStringLiteral("First"));
		auto* secondModel = new FloatModel(0.f, 0.f, 1.f, 0.001f, secondEffect, QStringLiteral("Second"));
		auto* track = Track::create(Track::Type::Automation, Engine::getSong());
		auto* firstClip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		auto* secondClip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(1)));
		firstClip->addObject(firstModel);
		secondClip->addObject(secondModel);
		for (AutomationClip* clip : {firstClip, secondClip})
		{
			clip->setProgressionType(AutomationClip::ProgressionType::Linear);
			clip->putValue(TimePos(0), 0.f, false);
			clip->putValue(TimePos(800), 1.f, false);
		}

		latency.setEnabled(true);
		const AutomatedValueMap before = Engine::getSong()->automatedValuesAt(TimePos(200));
		const float firstBefore = before.value(firstModel, -1.f);
		const float secondBefore = before.value(secondModel, -1.f);
		channel(1)->setInputLatencyFrames(static_cast<int>(std::ceil(Engine::framesPerTick() * 40.0)));
		firstEffect->arm();
		std::thread refresher([&latency] { latency.refresh(); });
		const bool refreshPausedBetweenClips = firstEffect->waitUntilEntered();
		const AutomatedValueMap during = Engine::getSong()->automatedValuesAt(TimePos(200));
		const float firstDuring = during.value(firstModel, -1.f);
		const float secondDuring = during.value(secondModel, -1.f);
		firstEffect->resume();
		refresher.join();

		latency.setEnabled(false);
		channel(1)->setInputLatencyFrames(0);
		delete track;
		chain.removeEffect(firstEffect);
		chain.removeEffect(secondEffect);
		delete firstEffect;
		delete secondEffect;

		QVERIFY(refreshPausedBetweenClips);
		QVERIFY(firstBefore >= 0.f && secondBefore >= 0.f);
		QCOMPARE(firstDuring, firstBefore);
		QCOMPARE(secondDuring, secondBefore);
	}

	void latencyRefreshUsesTargetStateCapturedUnderItsLock()
	{
		AutomationLatency& latency = AutomationLatency::instance();
		latency.setEnabled(false);
		EffectChain& chain = channel(1)->m_fxChain;
		channel(1)->setInputLatencyFrames(0);
		auto* latent = new partd::LatentDelayEffect(&chain, 64);
		auto* target = new partd::FixedGainEffect(&chain, 1.f, 1.f);
		chain.appendEffect(latent);
		chain.appendEffect(target);
		auto* model = new FloatModel(0.f, 0.f, 1.f, 0.001f, target, QStringLiteral("Probe"));
		auto* track = Track::create(Track::Type::Automation, Engine::getSong());
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		clip->addObject(model);
		QCOMPARE(AutomationLatency::upstreamFrames(model), 64);

		g_objectsReadClip = clip;
		g_objectsReadTarget = model;
		AutomationClip::setObjectsReadProbeForTest(removeTargetAfterObjectsUnlock);
		latency.setEnabled(true);
		const int stampedFrames = clip->latencyFrames();
		AutomationClip::setObjectsReadProbeForTest(nullptr);
		g_objectsReadClip = nullptr;
		g_objectsReadTarget = nullptr;

		latency.setEnabled(false);
		delete track;
		chain.removeEffect(target);
		chain.removeEffect(latent);
		delete target;
		delete latent;
		channel(1)->setInputLatencyFrames(0);

		QCOMPARE(stampedFrames, 64);
	}

	void mixedLatencyTargetsAreRejectedUntilCompatible()
	{
		AutomationLatency& latency = AutomationLatency::instance();
		latency.setEnabled(false);
		EffectChain& chain = channel(1)->m_fxChain;
		channel(1)->setInputLatencyFrames(0);
		auto* latent = new partd::LatentDelayEffect(&chain, 64);
		auto* after = new partd::FixedGainEffect(&chain, 1.f, 1.f);
		chain.appendEffect(latent);
		chain.appendEffect(after);
		auto* afterModel = new FloatModel(0.f, 0.f, 1.f, 0.001f, after, QStringLiteral("After"));
		auto* latentModel = new FloatModel(0.f, 0.f, 1.f, 0.001f, latent, QStringLiteral("Before"));
		QCOMPARE(AutomationLatency::upstreamFrames(afterModel), 64);
		QCOMPARE(AutomationLatency::upstreamFrames(latentModel), 0);

		auto* track = Track::create(Track::Type::Automation, Engine::getSong());
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		clip->addObject(afterModel);
		clip->addObject(latentModel);
		latency.setEnabled(true);
		const auto absoluteValues = Engine::getSong()->automatedValuesAt(TimePos(0));
		const bool absoluteRejected = !absoluteValues.contains(afterModel) && !absoluteValues.contains(latentModel);

		clip->setRelative(true);
		const auto relativeValues = Engine::getSong()->automatedValuesAt(TimePos(0));
		const bool relativeRejected = !relativeValues.contains(afterModel) && !relativeValues.contains(latentModel);

		clip->setSampleAccurate(true);
		AutomatableModel::incrementPeriodCounter();
		Engine::getSong()->buildAutomationRamps(Engine::getSong()->tracks(), TimePos(0), 256, 0);
		const bool rampRejected = afterModel->automationRamp() == nullptr && latentModel->automationRamp() == nullptr;

		clip->setSampleAccurate(false);
		clip->setRelative(false);
		const bool targetRemoved = clip->removeObject(latentModel);
		latency.refresh();
		const int compatibleFrames = clip->latencyFrames();
		const auto compatibleValues = Engine::getSong()->automatedValuesAt(TimePos(0));
		const bool compatibleResumed = compatibleValues.contains(afterModel) && !compatibleValues.contains(latentModel);

		latency.setEnabled(false);
		delete track;
		chain.removeEffect(after);
		chain.removeEffect(latent);
		delete after;
		delete latent;
		channel(1)->setInputLatencyFrames(0);

		QVERIFY(absoluteRejected);
		QVERIFY(relativeRejected);
		QVERIFY(rampRejected);
		QVERIFY(targetRemoved);
		QCOMPARE(compatibleFrames, 64);
		QVERIFY(compatibleResumed);
	}

	void discreteAutomationCompensationKeepsItsFrameOffset()
	{
		constexpr int latencyFrames = 64;
		EffectChain& chain = channel(1)->m_fxChain;
		channel(1)->setInputLatencyFrames(0);
		auto* latent = new partd::LatentDelayEffect(&chain, latencyFrames);
		auto* target = new partd::FixedGainEffect(&chain, 1.f, 1.f);
		chain.appendEffect(latent);
		chain.appendEffect(target);

		Song* song = Engine::getSong();
		auto* track = Track::create(Track::Type::Automation, song);
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		auto* model = new FloatModel(0.f, 0.f, 1.f, 0.001f, target, QStringLiteral("Probe"));
		clip->addObject(model);
		clip->putValue(TimePos(0), 0.f, false);
		clip->putValue(TimePos(8), 1.f, false);
		clip->setProgressionType(AutomationClip::ProgressionType::Discrete);

		AutomationLatency::instance().setEnabled(true);
		QCOMPARE(clip->latencyFrames(), latencyFrames);
		const double framesPerTick = Engine::framesPerTick();
		const int frameOffset = static_cast<int>(std::floor(framesPerTick)) - latencyFrames;
		const int expectedStepFrame = static_cast<int>(std::ceil(framesPerTick - frameOffset + latencyFrames));
		AutomatableModel::incrementPeriodCounter();
		song->buildAutomationRamps(song->tracks(), TimePos(7), 256, frameOffset);

		const AutomationRamp* ramp = model->automationRamp();
		const bool hasRamp = ramp != nullptr;
		const float before = hasRamp ? ramp->valueAt(expectedStepFrame - 1) : -1.f;
		const float atStep = hasRamp ? ramp->valueAt(expectedStepFrame) : -1.f;
		AutomationLatency::instance().setEnabled(false);
		delete track;
		chain.removeEffect(target);
		chain.removeEffect(latent);
		delete target;
		delete latent;
		channel(1)->setInputLatencyFrames(0);

		QVERIFY2(hasRamp, "a compensated Discrete clip did not publish its frame-shifted ramp");
		QCOMPARE(before, 0.f);
		QCOMPARE(atStep, 1.f);
	}

	void postFaderInsertsBakeTheFaderOnlyWhenTheyHoldDevices()
	{
		MixerChannel* strip = channel(2);
		const f_cnt_t frames = partd::periodFrames();
		for (f_cnt_t f = 0; f < frames; ++f) { strip->m_buffer[f] = SampleFrame(1.f, 1.f); }
		strip->updatePostFaderBuffer(0.5f, frames);
		QCOMPARE(strip->m_postInserts.process(*strip, 0.5f, frames), 0.5f);  // empty: skipped
		QVERIFY(!strip->m_postInserts.baked());
		QCOMPARE(strip->m_buffer[3][0], 1.f);
		QCOMPARE(postinserts::faderValue(*strip, nullptr), strip->m_volumeModel.value());
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixerchannel"));
		strip->m_postInserts.save(doc, element);
		QVERIFY(element.firstChildElement(QStringLiteral("postinserts")).isNull());

		auto* half = new partd::FixedGainEffect(&strip->m_postInserts.chain(), 0.5f, 0.5f);
		strip->m_postInserts.chain().appendEffect(half);
		QCOMPARE(strip->m_postInserts.process(*strip, 0.5f, frames), 1.f);  // the meters now read the baked buffer
		QVERIFY(strip->m_postInserts.baked());
		QCOMPARE(strip->m_buffer[3][0], 0.25f);  // 1 x fader 0.5 x the post-fader device 0.5
		QCOMPARE(strip->m_postInserts.preFader()[3][0], 1.f);  // the pre-fader sends keep the pre-fader signal
		QCOMPARE(postinserts::faderValue(*strip, nullptr), 1.f);
		QVERIFY(postinserts::faderBuffer(*strip, nullptr) == nullptr);
		strip->m_postInserts.chain().removeEffect(half);
		delete half;
		strip->m_postInserts.process(*strip, 0.5f, frames);
		QVERIFY(!strip->m_postInserts.baked());
	}

	void postFaderInsertsAreAddressedAndSavedWithTheChannel()
	{
		QCOMPARE(run(QStringLiteral("mixer.post_inserts"), {{QStringLiteral("channel"), QStringLiteral("ch-9999")}}).errorKind,
			ControlErrorKind::NotFound);
		const ControlResult empty = run(QStringLiteral("mixer.post_inserts"), {{QStringLiteral("channel"), ch(2)}});
		QVERIFY2(empty.ok, qPrintable(empty.errorMessage));
		const QString target = empty.result.value(QStringLiteral("target")).toString();
		QCOMPARE(target, ch(2) + QStringLiteral("/post"));
		const QString plugin = loadBuiltin(target, QStringLiteral("amplifier"));
		if (plugin.isEmpty()) { QSKIP("this build loads no amplifier module"); }
		QCOMPARE(channel(2)->m_postInserts.chain().effects().size(), std::size_t{1});
		QVERIFY(channel(2)->m_fxChain.effects().empty());
		QCOMPARE(run(QStringLiteral("mixer.post_inserts"), {{QStringLiteral("channel"), ch(2)}}).result
			.value(QStringLiteral("devices")).toArray().size(), 1);
		QVERIFY(run(QStringLiteral("plugin.bypass"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("bypass"), true}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixerchannel"));
		channel(2)->m_postInserts.save(doc, element);
		QVERIFY(!element.firstChildElement(QStringLiteral("postinserts")).isNull());
		{
			// A project load restores a channel's post inserts with the journal off (Song::loadProject, then
			// Mixer::loadSettings -> PostFaderInserts::load, Mixer.cpp:2639), so the restore is not an undo step.
			// With the journal on, the empty load below runs EffectChain::clear (EffectChain.cpp:443-457), whose
			// enabled-model write pushes a step no command recorded, and this test's second control.undo would
			// take that back instead of the plugin.load.
			const quint64 top = Engine::projectJournal()->topStepSerial();
			{
				const JournalOff restoring;
				channel(3)->m_postInserts.load(element);
				QCOMPARE(channel(3)->m_postInserts.chain().effects().size(), std::size_t{1});
				channel(3)->m_postInserts.load(doc.createElement(QStringLiteral("mixerchannel")));  // absent: emptied
			}
			QCOMPARE(Engine::projectJournal()->topStepSerial(), top);  // the restore made no undo step
		}
		QVERIFY(channel(3)->m_postInserts.chain().effects().empty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the plugin.load (the bypass was undone above)
		QVERIFY(channel(2)->m_postInserts.chain().effects().empty());
	}

	void aChannelReceivesAnotherInsideItsChain()
	{
		QCOMPARE(run(QStringLiteral("chain.receive"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("from"), ch(1)}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("chain.receive"), {{QStringLiteral("target"), QStringLiteral("trk-1")}, {QStringLiteral("from"), ch(1)}}).errorKind,
			ControlErrorKind::InvalidArgs);
		const std::size_t devices = channel(3)->m_fxChain.effects().size();
		const ControlResult made = run(QStringLiteral("chain.receive"), {{QStringLiteral("target"), ch(3)}, {QStringLiteral("from"), ch(1)},
			{QStringLiteral("level"), 0.5}});
		if (!made.ok && made.errorKind == ControlErrorKind::NotFound) { QSKIP("this build has no Receiver module"); }
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QVERIFY(made.result.value(QStringLiteral("send_created")).toBool());
		QVERIFY(Engine::mixer()->channelSidechainSend(1, 3) != nullptr);
		QCOMPARE(channel(3)->m_fxChain.effects().size(), devices + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(Engine::mixer()->channelSidechainSend(1, 3) == nullptr);
		QCOMPARE(channel(3)->m_fxChain.effects().size(), devices);
	}

	void thePreFadeEnvelope()
	{
		const PreFadeSpan span{1000, 100, 200, 0, 0};
		const auto gain = [](const PreFadeSpan& s, std::int64_t at) { return preFadedGainAt(s, at, FadeShape::Linear, FadeShape::Linear); };
		QVERIFY(gain(span, -100) == 0.f);  // the ramp's first frame, before the clip
		QCOMPARE(gain(span, -50), 0.5f);
		QCOMPARE(gain(span, 0), 1.f);  // a pre-fade alone ends on the clip's first frame
		QCOMPARE(gain(span, 999), 1.f);
		QCOMPARE(gain(span, 1100), 0.5f);  // after the clip's end
		QVERIFY(gain(span, 1200) == 0.f);
		const PreFadeSpan across{1000, 100, 0, 100, 0};
		QCOMPARE(gain(across, 0), 0.5f);  // with the clip's own fade-in: one ramp across its start
	}

	void aTrimmedClipSoundsItsHiddenAudioBeforeItsStart()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		SampleClip* clip = toneClip(track, 1920);
		const auto half = static_cast<f_cnt_t>(clip->sample().sampleSize() / 2);
		clip->setSampleWindow(SampleWindow(half, clip->sample().sampleSize()));  // the first half second hidden
		QVERIFY(prefades::spanStart(*clip) == clip->startPosition());  // no pre-fade: the clip's own span and window
		QCOMPARE(prefades::window(*clip, clip->startPosition()).sourceIn, clip->sourceFrameAt(clip->startPosition()));
		const QString id = control::clipIdOf(clip);
		const ControlResult set = run(QStringLiteral("clip.pre_fade"), {{QStringLiteral("clip"), id}, {QStringLiteral("fade_in"), 24},
			{QStringLiteral("fade_out"), 24}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("lead_ticks")).toInt(), 24);
		QCOMPARE(prefades::spanStart(*clip).getTicks(), clip->startPosition().getTicks() - 24);
		const SampleWindow early = prefades::window(*clip, prefades::spanStart(*clip));
		QVERIFY(early.sourceIn < half);
		{
			SamplePlayHandle handle(clip, early);
			std::vector<SampleFrame> out(partd::periodFrames());
			handle.play(out);
			QVERIFY(out[0][0] == 0.f);  // the fade-in's first frame
			bool sounds = false;
			for (int period = 1; period < 8; ++period)  // still inside the pre-fade: the clip starts 24 ticks later
			{
				handle.play(out);
				for (const SampleFrame& frame : out) { sounds = sounds || frame[0] != 0.f; }
			}
			QVERIFY2(sounds, "the hidden audio before the clip's start did not sound");
		}
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->clipEdits().preFadeInTicks, 0);
		QVERIFY(prefades::spanStart(*clip) == clip->startPosition());
		auto* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		Clip* midi = instrument->createClip(TimePos(0));
		QCOMPARE(run(QStringLiteral("clip.pre_fade"), {{QStringLiteral("clip"), control::clipIdOf(midi)}, {QStringLiteral("fade_in"), 24}}).errorKind,
			ControlErrorKind::InvalidArgs);
		delete instrument;
		delete track;
	}

};

QTEST_MAIN(W3EngineTest)
#include "W3EngineTest.moc"
