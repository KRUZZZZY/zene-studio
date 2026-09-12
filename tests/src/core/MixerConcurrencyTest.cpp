/*
 * MixerConcurrencyTest.cpp - regression tests for the graded mixer
 * concurrency and ordering defects (docs/MIXER-CONCURRENCY-FIXES.md, D1..D6).
 *
 * Copyright (c) 2026 Zene Studio developers
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

//! Two kinds of evidence live here.
//!
//! D1, D2(ii) and D2(iii) are *ordering* defects: the single-threaded render
//! logic produces a wrong period. Their slots assert on the rendered audio and
//! on the sidechain tap an effect is handed, and they fail deterministically
//! without the fix, with or without a sanitizer.
//!
//! D3, D4, D5 and D6 are *unsynchronised access* defects: they do not change
//! the output of a correctly scheduled period, so there is nothing to assert
//! on besides "the access is ordered". Each of their slots drives the real
//! writer on a second thread while the real reader runs, then asserts the
//! shared structure is intact; the failure mode without the fix is a data
//! race, reported by -DWANT_DEBUG_TSAN=ON (and, for D6, a crash). They are the
//! before/after evidence for those four defects and are documented as such in
//! docs/MIXER-CONCURRENCY-FIXES.md - they are not silent on the defect, they
//! are loud about it in the sanitizer build.
//!
//! Render-thread contract: AudioEngine::renderNextPeriod() holds m_changeMutex
//! for the whole period (src/core/AudioEngine.cpp). A test that drives
//! Mixer::prepareMasterMix()/masterMix() by hand must hold it too, otherwise
//! the test itself is the contract violation rather than the code.
//! RenderPeriod below carries that contract.

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "PhaseDMixerTestSupport.h"

#include "AudioBus.h"
#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "PlayHandle.h"
#include "SampleFrame.h"

using namespace lmms;
using partd::PeriodHarness;

namespace
{

//! A PlayHandle that owns no buffer: AudioBusHandle::doProcessing() skips it
//! (buffer().data() == nullptr) but it still occupies a slot in
//! m_playHandles, which is all the D6 slot needs.
class SilentPlayHandle : public PlayHandle
{
public:
	SilentPlayHandle() : PlayHandle(Type::NotePlayHandle) {}
	void play(std::span<SampleFrame>) override {}
	bool isFinished() const override { return true; }
	bool isFromTrack(const Track*) const override { return false; }
};

//! Holds AudioEngine's change mutex for as long as it lives: the contract
//! AudioEngine::renderNextPeriod() implements for a whole render period.
//! The mutex is recursive, so the nested requestChangeInModel() calls inside
//! createChannel()/createRoute() are fine on this thread.
class RenderPeriod
{
public:
	RenderPeriod() { Engine::audioEngine()->requestChangeInModel(); }
	~RenderPeriod() { Engine::audioEngine()->doneChangeInModel(); }
	RenderPeriod(const RenderPeriod&) = delete;
	RenderPeriod& operator=(const RenderPeriod&) = delete;
};

float maxAbs(const std::vector<SampleFrame>& buf)
{
	float m = 0.0f;
	for (const SampleFrame& f : buf)
	{
		m = std::max(m, std::max(std::fabs(f[0]), std::fabs(f[1])));
	}
	return m;
}

//! One period through the mixer, honouring the render thread's contract.
void renderPeriod(Mixer* mixer, std::vector<SampleFrame>& out)
{
	RenderPeriod guard;
	zeroSampleFrames(out.data(), Engine::audioEngine()->framesPerPeriod());
	mixer->prepareMasterMix();
	mixer->masterMix(out.data());
}

//! Renders `periods` periods on this thread while `mutate` runs on a second
//! one, then stops it and joins. The renderer drives the loop, so the two
//! are guaranteed to overlap; `mutate` is handed the stop flag and must
//! return promptly when it is set.
template <typename Mutate>
void renderWhileMutating(Mixer* mixer, int periods, Mutate&& mutate)
{
	std::vector<SampleFrame> out(partd::periodFrames());
	std::atomic<bool> stop{false};
	std::thread mutation([&mutate, &stop] { mutate(stop); });

	for (int p = 0; p < periods; ++p)
	{
		renderPeriod(mixer, out);
		std::this_thread::yield();
	}
	stop.store(true, std::memory_order_release);
	mutation.join();
}

//! Runs `mutate` on a second thread while this thread holds AudioEngine's
//! change mutex - a render period in flight - and reports whether either the
//! call returned or `changed()` became true before the period ended. Both are
//! contract violations: a control-thread topology change must not touch the
//! shared graph while the render thread owns it.
template <typename Mutate, typename Changed>
bool escapesTheRenderPeriod(Mutate&& mutate, Changed&& changed)
{
	Engine::audioEngine()->requestChangeInModel();

	std::atomic<bool> started{false};
	std::atomic<bool> finished{false};
	std::thread worker([&mutate, &started, &finished]
	{
		started.store(true, std::memory_order_release);
		mutate();
		finished.store(true, std::memory_order_release);
	});
	while (!started.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
	bool escaped = false;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (finished.load(std::memory_order_acquire) || changed())
		{
			escaped = true;
			break;
		}
		std::this_thread::yield();
	}

	Engine::audioEngine()->doneChangeInModel();
	worker.join();
	return escaped;
}

} // namespace

class MixerConcurrencyTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		partd::initEngine();
	}

	void cleanupTestCase()
	{
		partd::destroyEngine();
	}

	// -----------------------------------------------------------------------
	// D1 - the mute latch must be decided for the whole mixer before any
	// dependency is counted.
	//
	// masterMix() latches each channel's mute state and drains that latch in
	// the same pass. A *muted* channel counts its receivers' dependencies
	// inside that pass, so it reads a higher-indexed receiver's latch before
	// that receiver has been latched for the period: it tests the previous
	// period's value. If the receiver was muted last period and is unmuted
	// now, the muted sender skips it, the receiver's dependency counter stays
	// one short, it is never queued, master is never counted, and the whole
	// period comes out silent - the deterministic one-period dropout.
	// -----------------------------------------------------------------------
	void muteLatchIsDecidedBeforeDependenciesAreCounted()
	{
		Mixer* mixer = Engine::mixer();
		const f_cnt_t fpp = partd::periodFrames();
		const float level = 0.25f;

		mixer->clear();
		while (mixer->numChannels() < 4) { mixer->createChannel(); }

		// Channel 2 has two senders: channel 1 (muted for the whole test - the
		// "muted lower-indexed sender") and channel 3 (live throughout). Only
		// channel 3 carries signal.
		QVERIFY(mixer->createChannelSend(1, 2, 1.0f) != nullptr);
		QVERIFY(mixer->createChannelSend(3, 2, 1.0f) != nullptr);

		PeriodHarness harness(mixer);
		const auto feed = [&harness, fpp, level]
		{
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				harness.in()[f] = SampleFrame{level, level};
			}
			harness.feed(3);
		};

		std::vector<SampleFrame> playing;
		for (int p = 0; p < 4; ++p)
		{
			feed();
			playing = harness.render();
		}
		QVERIFY2(maxAbs(playing) > 0.05f,
			"the 1/3 -> 2 -> master graph is silent before any mute; the test is vacuous");

		// Mute the sender (it stays muted) and the receiver, so the receiver's
		// latch reads true at the start of the transition period.
		mixer->mixerChannel(1)->m_muteModel.setValue(true);
		mixer->mixerChannel(2)->m_muteModel.setValue(true);
		for (int p = 0; p < 3; ++p)
		{
			feed();
			harness.render();
		}

		// The transition: un-mute only the receiver. Channel 3 is still live, so
		// the master must not be silent - unless channel 2 was never counted.
		mixer->mixerChannel(2)->m_muteModel.setValue(false);
		feed();
		const std::vector<SampleFrame> transition = harness.render();
		feed();
		const std::vector<SampleFrame> afterTransition = harness.render();

		partd::evidence("MIXCONC_D1 transition_period_peak=%.6f next_period_peak=%.6f",
			static_cast<double>(maxAbs(transition)),
			static_cast<double>(maxAbs(afterTransition)));

		QVERIFY2(maxAbs(transition) > 1.0e-6f,
			"the period that un-mutes the receiver is silent even though channel 3 is "
			"live: masterMix counted channel 2's dependencies against the previous "
			"period's mute latch, so it was never queued (D1)");
		QVERIFY2(maxAbs(afterTransition) > 0.05f,
			"the graph did not resume on the next period after the un-mute");
	}

	// -----------------------------------------------------------------------
	// D2(ii) - a muted sender must not leave its last sidechain tap in the
	// route's intermediate buffer, or its receiver sums stale audio.
	//
	// Reachability note: a non-deferred route is consumed *and cleared* by its
	// receiver (sumSidechainInputs), so a block can only go stale if the
	// receiver did not run the period the sender wrote it. Here the receiver
	// (channel 1) is muted while the sender (channel 2) is still playing, so
	// the intermediate keeps the sender's last block; the transition then
	// un-mutes the receiver and mutes the sender in the same period. The sender
	// sits at a *higher* index than the receiver on purpose: an unmuted-latch
	// receiver woken by a lower-indexed muted sender is D1's territory, and
	// with the D1 latch defect present the receiver would not be scheduled at
	// all, which would mask this defect instead of exhibiting it.
	// -----------------------------------------------------------------------
	void mutedSenderDoesNotReplayItsStaleSidechainTap()
	{
		Mixer* mixer = Engine::mixer();
		const f_cnt_t fpp = partd::periodFrames();
		const float level = 0.5f;

		mixer->clear();
		while (mixer->numChannels() < 4) { mixer->createChannel(); }

		// 2 sidechains into 1; the route is observation-only and gates.
		MixerSidechainRoute* route =
			mixer->createSidechainSend(2, 1, 1.0f, SidechainTapPoint::PostFader);
		QVERIFY2(route != nullptr, "the sidechain send was not created");
		QVERIFY2(!route->deferred(), "a sidechain send with no cycle back must not be deferred");

		// Channel 3 also feeds channel 1 through a *regular* send, so channel
		// 1's own bus carries signal and its effect chain stays awake (a
		// sleeping effect is not called at all, which would make the assertion
		// vacuous rather than failing).
		QVERIFY(mixer->createChannelSend(3, 1, 1.0f) != nullptr);

		auto* probe = new partd::TapProbeEffect(&mixer->mixerChannel(1)->m_fxChain);
		mixer->mixerChannel(1)->m_fxChain.appendEffect(probe);

		PeriodHarness harness(mixer);
		const auto feed = [&harness, fpp, level]
		{
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				harness.in()[f] = SampleFrame{level, level};
			}
			harness.feed(2);
			harness.feed(3);
		};

		for (int p = 0; p < 5; ++p)
		{
			feed();
			harness.render();
		}
		const float tapBeforeMute = probe->firstL();
		QVERIFY2(std::fabs(tapBeforeMute) > 0.1f,
			"the sidechain tap never reached channel 1 before the mute; the test is vacuous");

		// Leave the sender's block unconsumed: mute the receiver for a while.
		mixer->mixerChannel(1)->m_muteModel.setValue(true);
		for (int p = 0; p < 3; ++p)
		{
			feed();
			harness.render();
		}

		// The transition: un-mute the receiver and mute the sender together.
		probe->reset();
		mixer->mixerChannel(1)->m_muteModel.setValue(false);
		mixer->mixerChannel(2)->m_muteModel.setValue(true);
		feed();
		harness.render();
		const float tapAfterMute = probe->firstL();

		partd::evidence("MIXCONC_D2ii tap_before_mute=%.6f tap_in_transition_period=%.6f "
			"probe_calls=%d",
			static_cast<double>(tapBeforeMute), static_cast<double>(tapAfterMute),
			probe->calls());

		QVERIFY2(probe->calls() > 0,
			"the receiver's effect chain did not run in the transition period");
		QVERIFY2(std::fabs(tapAfterMute) < 1.0e-9f,
			"the muted sender's last sidechain block was replayed to its receiver: "
			"masterMix's muted branch advances the incoming rings but never clears the "
			"outgoing intermediates (D2(ii))");
	}

	// -----------------------------------------------------------------------
	// D2(iii) - a deferred route's receiver reads the snapshot that
	// prepareMasterMix() commits every period, so an intermediate that is never
	// cleared re-loops the same stale block forever.
	// -----------------------------------------------------------------------
	void mutedSenderStopsFeedingADeferredReceiver()
	{
		Mixer* mixer = Engine::mixer();
		const f_cnt_t fpp = partd::periodFrames();
		const float level = 0.5f;

		mixer->clear();
		while (mixer->numChannels() < 3) { mixer->createChannel(); }

		// A regular send 1 -> 2 anchors the ordering; the sidechain 2 -> 1 then
		// closes a cycle through it and must be deferred (spec 5.2).
		QVERIFY(mixer->createChannelSend(1, 2, 1.0f) != nullptr);
		MixerSidechainRoute* route =
			mixer->createSidechainSend(2, 1, 1.0f, SidechainTapPoint::PostFader);
		QVERIFY2(route != nullptr, "the deferred sidechain send was not created");
		QVERIFY2(route->deferred(), "the sidechain send 2 -> 1 closes a cycle and must be deferred");

		auto* probe = new partd::TapProbeEffect(&mixer->mixerChannel(1)->m_fxChain);
		mixer->mixerChannel(1)->m_fxChain.appendEffect(probe);

		PeriodHarness harness(mixer);
		const auto feed = [&harness, fpp, level]
		{
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				harness.in()[f] = SampleFrame{level, level};
			}
			harness.feed(1);
		};

		for (int p = 0; p < 6; ++p)
		{
			feed();
			harness.render();
		}
		const float tapBeforeMute = probe->firstL();
		QVERIFY2(std::fabs(tapBeforeMute) > 0.1f,
			"the deferred sidechain tap never reached channel 1; the test is vacuous");

		// Mute the sender. A deferred route is one period late by design, so the
		// tap may survive the mute period - but it must stop after that.
		mixer->mixerChannel(2)->m_muteModel.setValue(true);
		float tapInMutePeriod = 0.0f;
		float tapLater = 0.0f;
		for (int p = 0; p < 4; ++p)
		{
			probe->reset();
			feed();
			harness.render();
			if (p == 0) { tapInMutePeriod = probe->firstL(); }
			tapLater = probe->firstL();
		}

		partd::evidence("MIXCONC_D2iii tap_before_mute=%.6f tap_in_mute_period=%.6f "
			"tap_three_periods_later=%.6f",
			static_cast<double>(tapBeforeMute), static_cast<double>(tapInMutePeriod),
			static_cast<double>(tapLater));

		// A deferred route is one period late by construction (the receiver
		// reads the snapshot committed at the top of the period), so the mute
		// period legitimately still carries the pre-mute tap. The defect is
		// that the tap *never* stops: without the clear, prepareMasterMix()
		// re-commits the same stale intermediate every period.
		QCOMPARE(std::fabs(tapInMutePeriod) > 0.1f, std::fabs(tapBeforeMute) > 0.1f);
		QVERIFY2(std::fabs(tapLater) < 1.0e-9f,
			"a muted sender kept re-committing its last sidechain block every period, so "
			"the deferred receiver loops stale audio (D2(iii))");
	}

	// -----------------------------------------------------------------------
	// D3, D4 and D5 - a control-thread topology change must not be able to
	// touch the mixer graph while the render thread owns it.
	//
	// AudioEngine::renderNextPeriod() holds m_changeMutex for the whole period
	// for exactly this reason, and every other mixer-topology writer uses the
	// same idiom. createChannel() (D3), moveChannelLeft() (D4) and
	// EffectChain::moveUp()/moveDown() (D5) skipped it, so each can grow,
	// reorder or renumber the graph that the render thread is iterating.
	// This slot holds the period open and asks whether the mutation gets
	// through anyway - deterministically, on a plain build, no sanitizer
	// needed. It is a positive test of the fix and fails without it.
	// -----------------------------------------------------------------------
	void topologyChangesWaitForTheRenderPeriodToEnd()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		while (mixer->numChannels() < 4) { mixer->createChannel(); }

		EffectChain* chain = &mixer->mixerChannel(1)->m_fxChain;
		chain->appendEffect(new partd::FixedGainEffect(chain, 1.0f, 1.0f));
		Effect* middle = new partd::FixedGainEffect(chain, 1.0f, 1.0f);
		chain->appendEffect(middle);
		chain->appendEffect(new partd::FixedGainEffect(chain, 1.0f, 1.0f));

		// D3: the channel vector (and the latency scratch) must not grow.
		const int channelsBefore = static_cast<int>(mixer->numChannels());
		const bool grewWhileRendering = escapesTheRenderPeriod(
			[mixer] { mixer->createChannel(); },
			[mixer, channelsBefore]
			{ return static_cast<int>(mixer->numChannels()) != channelsBefore; });
		QVERIFY2(!grewWhileRendering,
			"createChannel() grew m_mixerChannels while a render period was in flight (D3)");
		QCOMPARE(static_cast<int>(mixer->numChannels()), channelsBefore + 1);

		// D4: the channel order must not change.
		MixerChannel* firstBefore = mixer->mixerChannel(1);
		const bool movedWhileRendering = escapesTheRenderPeriod(
			[mixer] { mixer->moveChannelLeft(2); },
			[mixer, firstBefore] { return mixer->mixerChannel(1) != firstBefore; });
		QVERIFY2(!movedWhileRendering,
			"moveChannelLeft() reordered m_mixerChannels while a render period was in flight (D4)");
		// Deferred to the period boundary, not dropped.
		QVERIFY2(mixer->mixerChannel(1) != firstBefore,
			"the channel move never landed after the render period ended");
		QCOMPARE(mixer->mixerChannel(1)->index(), 1);

		// D5: the effect order must not change. The chain's cached latency is
		// order-independent, so the observable here is that the call returns
		// at all while the period is still open.
		const bool reorderedWhileRendering = escapesTheRenderPeriod(
			[chain, middle] { chain->moveDown(middle); },
			[] { return false; });
		QVERIFY2(!reorderedWhileRendering,
			"EffectChain::moveDown() ran while a render period was in flight (D5)");
	}

	// -----------------------------------------------------------------------
	// D3 - Mixer::createChannel() reallocates m_mixerChannels and the latency
	// scratch with no guard, while the render thread iterates them.
	// -----------------------------------------------------------------------
	void creatingAChannelIsSerialisedWithTheRenderPeriod()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		const int startChannels = static_cast<int>(mixer->numChannels());

		constexpr int kMaxCreated = 150;
		std::atomic<int> created{0};
		renderWhileMutating(mixer, 300, [mixer, &created](const std::atomic<bool>& stop)
		{
			while (!stop.load(std::memory_order_acquire) && created.load() < kMaxCreated)
			{
				mixer->createChannel();
				created.fetch_add(1, std::memory_order_relaxed);
			}
		});

		QVERIFY2(created.load() > 0, "the channel creation never overlapped a render period");
		QCOMPARE(static_cast<int>(mixer->numChannels()), startChannels + created.load());
		for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
		{
			QCOMPARE(mixer->mixerChannel(i)->index(), i);
		}
		partd::evidence("MIXCONC_D3 created=%d channels=%d render_periods=300",
			created.load(), static_cast<int>(mixer->numChannels()));
	}

	// -----------------------------------------------------------------------
	// D4 - moveChannelLeft() swaps two entries and renumbers them with no
	// guard, while the render thread's latch loop and the latency pass read the
	// order and the index.
	// -----------------------------------------------------------------------
	void movingAChannelIsSerialisedWithTheRenderPeriod()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		while (mixer->numChannels() < 8) { mixer->createChannel(); }
		const int channels = static_cast<int>(mixer->numChannels());

		std::atomic<int> moves{0};
		std::atomic<bool> stop{false};
		std::thread mutation([mixer, channels, &moves, &stop]
		{
			while (!stop.load(std::memory_order_acquire))
			{
				for (int i = 2; i <= channels; ++i)
				{
					mixer->moveChannelLeft(i);
					mixer->moveChannelRight(i - 1);
					moves.fetch_add(2, std::memory_order_relaxed);
				}
			}
		});

		std::vector<SampleFrame> out(partd::periodFrames());
		int orderSum = 0;
		for (int p = 0; p < 600; ++p)
		{
			RenderPeriod guard;
			mixer->prepareMasterMix();
			zeroSampleFrames(out.data(), partd::periodFrames());
			mixer->masterMix(out.data());
			// The render thread reads the channel order and each channel's index
			// under the period's change mutex - the exact reads the audit names:
			// masterMix's latch loop walks m_mixerChannels, and
			// updateLatencyCompensation reaches the same state through
			// MixerRoute::senderIndex() -> m_from->index().
			for (int i = 0; i < channels; ++i)
			{
				orderSum += mixer->mixerChannel(i)->index();
			}
			std::this_thread::yield();
		}
		stop.store(true, std::memory_order_release);
		mutation.join();

		QVERIFY2(moves.load() > 0, "no channel move overlapped a render period");
		QVERIFY2(orderSum > 0, "the render loop never read the channel order");
		QCOMPARE(static_cast<int>(mixer->numChannels()), channels);
		for (int i = 0; i < channels; ++i)
		{
			QCOMPARE(mixer->mixerChannel(i)->index(), i);
		}
		partd::evidence("MIXCONC_D4 channels=%d moves=%d render_periods=600",
			channels, moves.load());
	}

	// -----------------------------------------------------------------------
	// D5 - EffectChain::moveUp/moveDown swap vector elements with no guard
	// while the worker iterates m_effects in processAudioBuffer().
	// -----------------------------------------------------------------------
	void reorderingEffectsIsSerialisedWithTheRenderPeriod()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		while (mixer->numChannels() < 8) { mixer->createChannel(); }
		const int channels = static_cast<int>(mixer->numChannels());

		// Three latency-reporting effects per chain. EffectChain has no public
		// effect list, but its cached latency is the sum over the effects - an
		// order-independent, membership-sensitive invariant, which is exactly
		// what a reorder must preserve.
		constexpr int kChainLatency = 11 + 22 + 33;
		std::vector<Effect*> middle;
		for (int c = 1; c < channels; ++c)
		{
			EffectChain* chain = &mixer->mixerChannel(c)->m_fxChain;
			chain->appendEffect(new partd::LatentDelayEffect(chain, 11));
			Effect* mid = new partd::LatentDelayEffect(chain, 22);
			chain->appendEffect(mid);
			chain->appendEffect(new partd::LatentDelayEffect(chain, 33));
			middle.push_back(mid);
		}
		QCOMPARE(mixer->mixerChannel(1)->m_fxChain.latencyFrames(), kChainLatency);

		std::atomic<int> reorders{0};
		renderWhileMutating(mixer, 600,
			[mixer, middle, channels, &reorders](const std::atomic<bool>& stop)
		{
			int round = 0;
			while (!stop.load(std::memory_order_acquire))
			{
				for (int c = 1; c < channels; ++c)
				{
					EffectChain* chain = &mixer->mixerChannel(c)->m_fxChain;
					Effect* mid = middle[static_cast<std::size_t>(c - 1)];
					if (round % 2 == 0) { chain->moveDown(mid); }
					else { chain->moveUp(mid); }
					reorders.fetch_add(1, std::memory_order_relaxed);
				}
				++round;
			}
		});

		QVERIFY2(reorders.load() > 0, "no effect reorder overlapped a render period");
		for (int c = 1; c < channels; ++c)
		{
			QVERIFY2(mixer->mixerChannel(c)->m_fxChain.latencyFrames() == kChainLatency,
				"the chain's effect set changed while the render thread processed it (D5)");
		}
		partd::evidence("MIXCONC_D5 channels=%d chain_latency=%d reorders=%d render_periods=600",
			channels, kChainLatency, reorders.load());
	}

	// -----------------------------------------------------------------------
	// D6 - AudioBusHandle::m_playHandles is appended under m_playHandleLock
	// but a worker range-fors it without the lock.
	// -----------------------------------------------------------------------
	void addingAPlayHandleIsSerialisedWithTheIterator()
	{
		AudioBusHandle handle{QStringLiteral("MixerConcurrencyTest"), false};
		SilentPlayHandle playHandle;

		constexpr int kRounds = 20000;
		std::atomic<bool> mutationDone{false};
		std::thread mutation([&handle, &playHandle, &mutationDone]
		{
			for (int i = 0; i < kRounds; ++i)
			{
				handle.addPlayHandle(&playHandle);
				handle.removePlayHandle(&playHandle);
			}
			mutationDone.store(true, std::memory_order_release);
		});

		int iterations = 0;
		constexpr int kMaxIterations = 2000000;
		while (!mutationDone.load(std::memory_order_acquire) && iterations < kMaxIterations)
		{
			handle.doProcessing();
			++iterations;
		}
		mutation.join();

		QVERIFY2(iterations > 0, "the iterator never ran concurrently with the writer");
		handle.removePlayHandle(&playHandle);
		partd::evidence("MIXCONC_D6 rounds=%d do_processing_iterations=%d", kRounds, iterations);
	}
};

QTEST_GUILESS_MAIN(MixerConcurrencyTest)

#include "MixerConcurrencyTest.moc"
