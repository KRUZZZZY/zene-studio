/*
 * PhaseDPerfBench.cpp - D3 per-send CPU gate for the Phase D sidechain
 * implementation (task #587, mission lmms-mixer-routing-mission).
 *
 * Copyright (c) 2026 LMMS developers
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

// Metric (spec v1.2, decision D3, mixer/SPEC-dynamic-routing.md):
//   "<5% single-core CPU per ACTIVE sidechain send, measured against the
//    baseline cost of a regular send, at 48 kHz / 256-frame buffer."
//
// Method
// ------
// One mixer, one process. Each repetition measures four windows of
// `kPeriods` synchronous renders (48 kHz / 256 frames = 5333.33 us of one
// core per period) with CLOCK_PROCESS_CPUTIME_ID, and the graph is toggled
// between windows so that every window is bracketed by an identical-window
// twin -- time-varying machine noise (other processes, frequency scaling)
// therefore cancels in the deltas instead of landing on one state:
//
//   window 1  base      (32 senders + 1 bus, only the bus->master send)
//   window 2  regular   base + 32 regular sends   ch(i) -> master
//   window 3  base      (the 32 regular sends deleted again)
//   window 4  sidechain base + 32 active sidechain sends ch(i) -> bus
//
// The marginal cost of one send is (window - mean(base twin windows)) / 32.
// The base twins' own spread is printed per repetition as the drift estimate.
// Channel processing, the bus, the feeds and the master receive loop are
// identical across windows, so they cancel in the deltas; every channel is
// fed a fresh non-silent input every period, so no silence short-circuit can
// hide the send work. `kReps` repetitions are summarised by their median.
//
// Gate: per-active-sidechain-send marginal CPU < 5% of one core's period
// budget. The sidechain/regular per-send ratio is printed as context.
//
// Deliberately NOT registered with ctest (timing-sensitive). Run:
//   ./tests/PhaseDPerfBench

#include <QtTest/QtTest>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <vector>

#include "PhaseDMixerTestSupport.h"

using namespace lmms;
using namespace partd;

namespace
{

double processCpuSeconds()
{
	timespec ts{};
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return static_cast<double>(ts.tv_sec)
			+ static_cast<double>(ts.tv_nsec) * 1.0e-9;
}

constexpr int kSenderChannels = 32;
constexpr mix_ch_t kBus = kSenderChannels + 1; // 33
constexpr int kPeriods = 2500;
constexpr int kWarmupPeriods = 200;
constexpr int kReps = 5;

//! Feed every sender one fresh non-silent period and render it.
void renderPeriod(PeriodHarness& h, std::uint32_t seed)
{
	const f_cnt_t fpp = h.fpp();
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		const float v = hashSignal(seed + static_cast<std::uint32_t>(f));
		h.in()[f][0] = v;
		h.in()[f][1] = -v;
	}
	for (mix_ch_t i = 1; i <= kSenderChannels; ++i)
	{
		h.feed(i);
	}
	h.render();
}

//! CPU seconds for `kPeriods` renders of the current graph (ramps settled).
double measureWindow(Mixer* mixer, std::uint32_t seed)
{
	PeriodHarness h(mixer);
	for (int p = 0; p < kWarmupPeriods; ++p)
	{
		renderPeriod(h, seed + static_cast<std::uint32_t>(p));
	}

	const double t0 = processCpuSeconds();
	for (int p = 0; p < kPeriods; ++p)
	{
		renderPeriod(h, seed + static_cast<std::uint32_t>(p));
	}
	return processCpuSeconds() - t0;
}

double median(std::vector<double> v)
{
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

} // namespace

class PhaseDPerfBench : public QObject
{
	Q_OBJECT

private slots:
	void perSendCpuGate()
	{
		initEngine();
		Engine::audioEngine()->audioDev()->setSampleRateForTesting(48000);
		QVERIFY2(Engine::audioEngine()->outputSampleRate() == 48000u,
				"bench requires the 48 kHz test sample rate");

		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() < kSenderChannels + 2)
		{
			mixer->createChannel();
		}
		QVERIFY(mixer->createChannelSend(kBus, 0, 1.0f) != nullptr);

		std::vector<double> perRegularNs;
		std::vector<double> perSidechainNs;
		std::vector<double> baseTwinSpreadUs;
		std::uint32_t seed = 0x1000;

		for (int rep = 0; rep < kReps; ++rep)
		{
			// window 1: base
			const double baseA = measureWindow(mixer, seed);
			seed += 0x1000;

			// window 2: + 32 regular sends
			for (mix_ch_t i = 1; i <= kSenderChannels; ++i)
			{
				QVERIFY(mixer->createChannelSend(i, 0, 1.0f) != nullptr);
			}
			const double regular = measureWindow(mixer, seed);
			seed += 0x1000;
			for (mix_ch_t i = 1; i <= kSenderChannels; ++i)
			{
				mixer->deleteChannelSend(i, 0);
			}

			// window 3: base again (twin of window 1)
			const double baseB = measureWindow(mixer, seed);
			seed += 0x1000;

			// window 4: + 32 active sidechain sends
			for (mix_ch_t i = 1; i <= kSenderChannels; ++i)
			{
				QVERIFY(mixer->createSidechainSend(i, kBus, 1.0f,
						SidechainTapPoint::PostFader) != nullptr);
			}
			const double sidechain = measureWindow(mixer, seed);
			seed += 0x1000;
			for (mix_ch_t i = 1; i <= kSenderChannels; ++i)
			{
				mixer->deleteSidechainSend(i, kBus);
			}

			const double base = 0.5 * (baseA + baseB);
			perRegularNs.push_back(
					(regular - base) / kSenderChannels / kPeriods * 1.0e9);
			perSidechainNs.push_back(
					(sidechain - base) / kSenderChannels / kPeriods * 1.0e9);
			baseTwinSpreadUs.push_back(
					std::abs(baseA - baseB) / kPeriods * 1.0e6);

			evidence("D3_REP rep=%d base_a_us=%.2f base_b_us=%.2f "
					"regular_us=%.2f sidechain_us=%.2f per_regular_ns=%.1f "
					"per_sidechain_ns=%.1f base_twin_spread_us=%.2f",
					rep + 1, baseA / kPeriods * 1.0e6, baseB / kPeriods * 1.0e6,
					regular / kPeriods * 1.0e6, sidechain / kPeriods * 1.0e6,
					perRegularNs.back(), perSidechainNs.back(),
					baseTwinSpreadUs.back());
		}

		destroyEngine();

		const double regularPerSendNs = median(perRegularNs);
		const double sidechainPerSendNs = median(perSidechainNs);
		const double driftUs = median(baseTwinSpreadUs);

		// 48 kHz / 256 frames: one period is 5333.33 us of one core.
		const double periodBudgetUs = 256.0 / 48000.0 * 1.0e6;
		const double pctOfCore =
				sidechainPerSendNs / 1000.0 / periodBudgetUs * 100.0;
		const double pctOfRegularSend =
				sidechainPerSendNs / regularPerSendNs * 100.0;

		evidence("D3_BENCH sample_rate=48000 frames_per_period=256 "
				"periods_per_window=%d windows_per_rep=4 reps=%d channels=%d "
				"sends=%d base_twin_spread_us=%.2f",
				kPeriods, kReps, kSenderChannels + 2, kSenderChannels, driftUs);
		evidence("D3_PER_SEND regular_ns=%.1f sidechain_ns=%.1f",
				regularPerSendNs, sidechainPerSendNs);
		evidence("D3_GATE per_send_pct_of_core=%.4f%% threshold=5%% verdict=%s",
				pctOfCore, pctOfCore < 5.0 ? "PASS" : "FAIL");
		evidence("D3_GATE sidechain_pct_of_regular_send=%.1f%% (context)",
				pctOfRegularSend);

		QVERIFY2(pctOfCore < 5.0,
				"per active sidechain send CPU exceeds 5% of one core");
	}
};

QTEST_GUILESS_MAIN(PhaseDPerfBench)
#include "PhaseDPerfBench.moc"
