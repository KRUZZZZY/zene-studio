/*
 * PdcMixerTest.cpp - plugin delay compensation (task #605)
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

//! Plugin delay compensation acceptance tests (task #605).
//!
//! The known-latency plugin is `LatentDelayEffect` (PhaseDMixerTestSupport.h):
//! a deterministic integer stereo delay that reports its latency through
//! `Effect::latencyFrames()`. Its `reportedLatencyFrames` knob lets the *same
//! DSP* under-report, which is exactly the pre-PDC situation and therefore the
//! causal control: the only difference between a cancelling render and a combed
//! render is whether the host knows the latency.
//!
//! Every measurement is printed as a flushed PDC_EVIDENCE line.

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "PhaseDMixerTestSupport.h"

#include "AudioBus.h"
#include "AudioBusHandle.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "LatencyCompensation.h"
#include "Mixer.h"
#include "Plugin.h"
#include "SampleFrame.h"

using namespace lmms;
using namespace partd;

namespace
{

//! Prime delay, deliberately not a multiple of any plausible block size.
constexpr int kLatency = 257;
constexpr int kPeriods = 14;

EffectChain* chainOf(Mixer* mixer, int channel)
{
	return &mixer->mixerChannel(channel)->m_fxChain;
}

//! Deterministic stereo signal indexed by absolute frame number.
float signalAt(int frame)
{
	return 0.5f * hashSignal(0x9e3779b9u + static_cast<std::uint32_t>(frame));
}

SampleFrame frameAt(int frame)
{
	return SampleFrame{signalAt(frame), signalAt(frame + 7919)};
}

void feedSignal(PeriodHarness& harness, int channel, int startFrame)
{
	for (f_cnt_t f = 0; f < harness.fpp(); ++f)
	{
		harness.in()[f] = frameAt(startFrame + static_cast<int>(f));
	}
	harness.feed(channel);
}

//! Feeds the deterministic signal into every channel in \p channels and renders
//! \p periods periods of the master output.
std::vector<SampleFrame> renderSignal(Mixer* mixer, const std::vector<int>& channels, int periods)
{
	PeriodHarness harness(mixer);
	std::vector<SampleFrame> out;
	out.reserve(static_cast<std::size_t>(periods) * harness.fpp());
	for (int p = 0; p < periods; ++p)
	{
		const int startFrame = p * static_cast<int>(harness.fpp());
		for (int channel : channels)
		{
			feedSignal(harness, channel, startFrame);
		}
		const std::vector<SampleFrame>& period = harness.render();
		out.insert(out.end(), period.begin(), period.end());
	}
	return out;
}

double rmsLinear(const std::vector<SampleFrame>& buf, int from, int to)
{
	double sum = 0.0;
	int frames = 0;
	for (int i = from; i < to; ++i)
	{
		sum += 0.5 * (static_cast<double>(buf[i][0]) * buf[i][0]
			+ static_cast<double>(buf[i][1]) * buf[i][1]);
		++frames;
	}
	return std::sqrt(sum / std::max(frames, 1));
}

double rmsDb(const std::vector<SampleFrame>& buf, int from, int to)
{
	return toDb(rmsLinear(buf, from, to));
}

//! RMS of (buf - gain * signal(i - shift)).
double residualRms(const std::vector<SampleFrame>& buf, int from, int to, int shift, double gain)
{
	double sum = 0.0;
	int frames = 0;
	for (int i = from; i < to; ++i)
	{
		const SampleFrame ref = frameAt(i - shift);
		const double dl = static_cast<double>(buf[i][0]) - gain * static_cast<double>(ref[0]);
		const double dr = static_cast<double>(buf[i][1]) - gain * static_cast<double>(ref[1]);
		sum += 0.5 * (dl * dl + dr * dr);
		++frames;
	}
	return std::sqrt(sum / std::max(frames, 1));
}

//! RMS of (buf - gain * signal(i - shift)) in dBFS.
double residualDb(const std::vector<SampleFrame>& buf, int from, int to, int shift, double gain)
{
	return toDb(residualRms(buf, from, to, shift, gain));
}

double signalRmsDb(int from, int to)
{
	double sum = 0.0;
	int frames = 0;
	for (int i = from; i < to; ++i)
	{
		const SampleFrame s = frameAt(i);
		sum += 0.5 * (static_cast<double>(s[0]) * s[0] + static_cast<double>(s[1]) * s[1]);
		++frames;
	}
	return toDb(std::sqrt(sum / std::max(frames, 1)));
}

//! Steady-state window: past the compensation pre-roll.
int windowStart()
{
	return kLatency + 3 * static_cast<int>(periodFrames());
}

} // namespace

class PdcMixerTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		initEngine();
		evidence("PDC_ENV sample_rate=%d frames_per_period=%d latency=%d periods=%d",
			static_cast<int>(Engine::audioEngine()->outputSampleRate()),
			static_cast<int>(periodFrames()), kLatency, kPeriods);
	}

	void cleanupTestCase()
	{
		destroyEngine();
	}

	//! Unit level: a zero delay must be an exact bypass (returns the caller's
	//! buffer and leaves it untouched), a nonzero delay must shift exactly.
	void latencyCompensationIsAnExactBypassAtZero()
	{
		LatencyCompensation line;
		line.init(periodFrames());
		QCOMPARE(line.delayFrames(), 0);

		std::vector<SampleFrame> buf(periodFrames());
		for (std::size_t i = 0; i < buf.size(); ++i)
		{
			buf[i] = frameAt(static_cast<int>(i));
		}
		const std::vector<SampleFrame> before = buf;

		const SampleFrame* out = line.process(buf.data(), buf.size());
		QVERIFY2(out == buf.data(), "a zero delay must return the caller's buffer");
		QVERIFY2(std::memcmp(buf.data(), before.data(), buf.size() * sizeof(SampleFrame)) == 0,
			"a zero delay must not modify the buffer");

		line.setDelayFrames(3);
		QCOMPARE(line.delayFrames(), 3);
		const SampleFrame* delayed = line.process(buf.data(), buf.size());
		QVERIFY2(delayed != buf.data(), "a nonzero delay must return the line's block");
		bool exact = true;
		for (std::size_t i = 0; i < buf.size(); ++i)
		{
			const std::size_t ref = (i + buf.size() - 3) % buf.size();
			if (delayed[i][0] != buf[ref][0] || delayed[i][1] != buf[ref][1])
			{
				exact = false;
				break;
			}
		}
		QVERIFY2(exact, "the delayed block is not an exact 3-frame shift of the input");
		evidence("PDC_LINE zero_delay_bypass=exact three_frame_shift=exact");
	}

	//! Acceptance (a) plus the causal control and the liveness control.
	void parallelNullCancelsOnlyWhenLatencyIsReported()
	{
		auto mixer = Engine::mixer();

		auto buildNull = [mixer](float wetGain, int reportedLatency) {
			mixer->clear();
			while (mixer->numChannels() < 3) { mixer->createChannel(); }
			// ch1 stays dry; ch2 runs the latent delay into a gain stage.
			EffectChain* chain = chainOf(mixer, 2);
			chain->appendEffect(new LatentDelayEffect(chain, kLatency, reportedLatency));
			chain->appendEffect(new FixedGainEffect(chain, wetGain, wetGain));
		};

		const int from = windowStart();
		const int to = kPeriods * static_cast<int>(periodFrames());
		const double dryDb = signalRmsDb(from, to);

		// Compensated null: dry + inverted wet must cancel.
		buildNull(-1.0f, kLatency);
		const auto cancelled = renderSignal(mixer, {1, 2}, kPeriods);
		const double cancelledDb = rmsDb(cancelled, from, to);

		// Liveness: the same graph in phase must render 2x the dry signal, so a
		// silent render cannot pass the null test.
		buildNull(1.0f, kLatency);
		const auto inPhase = renderSignal(mixer, {1, 2}, kPeriods);
		const double inPhaseDb = rmsDb(inPhase, from, to);
		const double inPhaseErrDb = residualDb(inPhase, from, to, kLatency, 2.0);

		// Causal control: identical DSP, latency not reported => no compensation.
		buildNull(-1.0f, 0);
		const auto combed = renderSignal(mixer, {1, 2}, kPeriods);
		const double combedDb = rmsDb(combed, from, to);

		evidence("PDC_NULL dry_dbfs=%.2f cancelled_dbfs=%.2f cancelled_rms=%.9g "
			"in_phase_dbfs=%.2f in_phase_err_vs_2x_dbfs=%.2f unreported_dbfs=%.2f latency=%d",
			dryDb, cancelledDb, rmsLinear(cancelled, from, to),
			inPhaseDb, inPhaseErrDb, combedDb, kLatency);

		QVERIFY2(dryDb > -20.0, "the test signal is silent");
		QVERIFY2(cancelledDb <= -60.0,
			qPrintable(QString("compensated parallel null only reached %1 dBFS")
				.arg(cancelledDb)));
		QVERIFY2(inPhaseDb >= dryDb + 4.0,
			"the in-phase render is not the constructive sum (liveness control)");
		QVERIFY2(inPhaseErrDb <= -60.0,
			qPrintable(QString("in-phase render deviates from 2x by %1 dB").arg(inPhaseErrDb)));
		QVERIFY2(combedDb >= -20.0,
			qPrintable(QString("unreported latency still cancelled (%1 dBFS); "
				"the null test is not sensitive to misalignment").arg(combedDb)));
		QVERIFY2(combedDb - std::max(cancelledDb, -240.0) >= 40.0,
			"the compensated render is not decisively better than the control");
	}

	//! Acceptance (b): a parallel bus containing a latent plugin stays
	//! phase-coherent instead of combing with the dry path.
	void parallelBusStaysPhaseCoherent()
	{
		auto mixer = Engine::mixer();

		auto buildBus = [mixer](int reportedLatency) {
			mixer->clear();
			while (mixer->numChannels() < 2) { mixer->createChannel(); }
			const int bus = mixer->createBusChannel();
			EffectChain* chain = chainOf(mixer, bus);
			chain->appendEffect(new LatentDelayEffect(chain, kLatency, reportedLatency));
			mixer->createChannelSend(1, bus, 1.0f);
		};

		const int from = windowStart();
		const int to = kPeriods * static_cast<int>(periodFrames());
		const double dryDb = signalRmsDb(from, to);

		buildBus(kLatency);
		QVERIFY2(mixer->channelSendModel(1, 2) != nullptr, "the bus send was not created");
		const auto coherent = renderSignal(mixer, {1}, kPeriods);
		const double coherentErrDb = residualDb(coherent, from, to, kLatency, 2.0);
		const double coherentDb = rmsDb(coherent, from, to);
		const int totalLatency = mixer->totalLatencyFrames();

		buildBus(0);
		const auto combed = renderSignal(mixer, {1}, kPeriods);
		const double combedErrDb = residualDb(combed, from, to, kLatency, 2.0);

		evidence("PDC_BUS dry_dbfs=%.2f coherent_dbfs=%.2f err_vs_2x_dbfs=%.2f "
			"err_vs_2x_rms=%.9g unreported_err_vs_2x_dbfs=%.2f total_latency_frames=%d",
			dryDb, coherentDb, coherentErrDb,
			residualRms(coherent, from, to, kLatency, 2.0),
			combedErrDb, totalLatency);

		QVERIFY2(totalLatency == kLatency,
			qPrintable(QString("reported total latency %1 != %2")
				.arg(totalLatency).arg(kLatency)));
		QVERIFY2(coherentDb >= dryDb + 4.0,
			"the bus render is not the constructive sum (liveness control)");
		QVERIFY2(coherentErrDb <= -60.0,
			qPrintable(QString("parallel bus deviates from the coherent sum by %1 dB")
				.arg(coherentErrDb)));
		QVERIFY2(combedErrDb - std::max(coherentErrDb, -240.0) >= 40.0,
			"the uncompensated bus render is not decisively worse");
	}

	//! Sidechain sends are mixer paths too: a PreFx tap on a latent sender must
	//! be delayed to the receiver's alignment point like any other input.
	void sidechainTapIsAlignedWithTheMainInput()
	{
		auto mixer = Engine::mixer();

		auto buildSidechain = [mixer](int reportedLatency, SidechainDiffProbe** probeOut) {
			mixer->clear();
			while (mixer->numChannels() < 4) { mixer->createChannel(); }
			EffectChain* sender = chainOf(mixer, 1);
			sender->appendEffect(new LatentDelayEffect(sender, kLatency, reportedLatency));
			auto* probe = new SidechainDiffProbe(chainOf(mixer, 3));
			chainOf(mixer, 3)->appendEffect(probe);
			mixer->createChannelSend(1, 3, 1.0f);
			mixer->createSidechainSend(1, 3, 1.0f, SidechainTapPoint::PreFx);
			*probeOut = probe;
		};

		SidechainDiffProbe* compensatedProbe = nullptr;
		buildSidechain(kLatency, &compensatedProbe);
		QVERIFY2(mixer->channelSendModel(1, 3) != nullptr, "the regular send was not created");
		QVERIFY2(mixer->channelSidechainSend(1, 3) != nullptr, "the sidechain send was not created");
		renderSignal(mixer, {1}, kPeriods);
		const double compensatedDb = toDb(compensatedProbe->rms());
		const double compensatedMax = compensatedProbe->maxAbs();
		const int compensatedBlocks = compensatedProbe->blocks();

		SidechainDiffProbe* controlProbe = nullptr;
		buildSidechain(0, &controlProbe);
		renderSignal(mixer, {1}, kPeriods);
		const double controlDb = toDb(controlProbe->rms());

		evidence("PDC_SIDECHAIN compensated_diff_dbfs=%.2f compensated_max_abs=%.6g "
			"probe_blocks=%d unreported_diff_dbfs=%.2f",
			compensatedDb, compensatedMax, compensatedBlocks, controlDb);

		QVERIFY2(compensatedBlocks > 0, "the probe never ran");
		QVERIFY2(compensatedDb <= -60.0,
			qPrintable(QString("sidechain/main input difference only reached %1 dBFS")
				.arg(compensatedDb)));
		QVERIFY2(controlDb >= -20.0,
			"the unreported control did not misalign the sidechain tap");
		QVERIFY2(controlDb - std::max(compensatedDb, -240.0) >= 40.0,
			"the compensated sidechain is not decisively better than the control");
	}

	//! Negative control: with no reported latency the graph must be
	//! bit-identical to the uncompensated mixer, and a bypassed effect must be
	//! bit-identical to no effect at all. A sensitivity render must differ.
	void zeroLatencyGraphIsBitIdentical()
	{
		auto mixer = Engine::mixer();

		auto build = [mixer](int mode) {
			mixer->clear();
			while (mixer->numChannels() < 3) { mixer->createChannel(); }
			EffectChain* chain1 = chainOf(mixer, 1);
			chain1->appendEffect(new FixedGainEffect(chain1, 0.5f, 0.5f));
			EffectChain* chain2 = chainOf(mixer, 2);
			if (mode > 0)
			{
				auto* latent = new LatentDelayEffect(chain2, kLatency);
				chain2->appendEffect(latent);
				if (mode == 1) { latent->setDontRun(true); }
			}
			chain2->appendEffect(new FixedGainEffect(chain2, 0.25f, 0.25f));
			mixer->createChannelSend(1, 2, 0.5f);
			mixer->createSidechainSend(1, 2, 1.0f, SidechainTapPoint::PostFader);
		};

		build(0);
		QVERIFY2(mixer->channelSendModel(1, 2) != nullptr, "the regular send was not created");
		QVERIFY2(mixer->channelSidechainSend(1, 2) != nullptr, "the sidechain send was not created");
		const auto noFx = renderSignal(mixer, {1, 2}, 6);
		const int latencyNoFx = mixer->totalLatencyFrames();

		build(1);
		const auto bypassed = renderSignal(mixer, {1, 2}, 6);
		const int latencyBypassed = mixer->totalLatencyFrames();

		build(2);
		const auto active = renderSignal(mixer, {1, 2}, 6);
		const int latencyActive = mixer->totalLatencyFrames();

		const double levelDb = rmsDb(noFx, 0, static_cast<int>(noFx.size()));
		const bool sameHash = sha256(noFx) == sha256(bypassed);
		const bool differsWhenActive = sha256(noFx) != sha256(active);

		evidence("PDC_NOOP total_latency_no_fx=%d total_latency_bypassed=%d "
			"total_latency_active=%d level_dbfs=%.2f bypass_bit_identical=%d "
			"active_differs=%d",
			latencyNoFx, latencyBypassed, latencyActive, levelDb,
			sameHash ? 1 : 0, differsWhenActive ? 1 : 0);

		QVERIFY2(levelDb > -20.0, "the reference graph is silent (vacuous control)");
		QVERIFY2(latencyNoFx == 0, "a zero-latency graph reported nonzero latency");
		QVERIFY2(latencyBypassed == 0, "a bypassed latent effect still contributed latency");
		QVERIFY2(latencyActive == kLatency, "the active latent effect was not reported");
		QVERIFY2(sameHash,
			"bypass vs no-FX is not bit-identical; the PDC path changed behaviour");
		QVERIFY2(differsWhenActive, "the sensitivity control did not differ");
	}
};

QTEST_GUILESS_MAIN(PdcMixerTest)

#include "PdcMixerTest.moc"
