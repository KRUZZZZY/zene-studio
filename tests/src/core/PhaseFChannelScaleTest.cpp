/*
 * PhaseFChannelScaleTest.cpp - Phase F 100+ mixer-channel scale proof (#592)
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

//! SPEC-dynamic-routing.md v1.2 section 8.2, criterion 1 (Phase C):
//!
//!   "Dynamic channel allocation (not fixed 64) - Create 100+ mixer channels
//!    via API -> verify no 64-artifact limit"
//!
//! The test builds the graph through the real public Mixer API
//! (createChannel / createBusChannel / createChannelSend / createSidechainSend)
//! and renders the real audio path (mixToChannel -> prepareMasterMix ->
//! masterMix) with 128 user channels plus a parallel bus at index 129. Every
//! assertion is an exact output level, so any fixed 64-channel artefact - a
//! cap, a dead channel at or above 64, a routing table that stops at 64, a
//! dependency counter that cannot schedule 129 channels - fails the test with
//! a measured number instead of a crash.
//!
//! Each measurement is printed as a flushed PARTF_EVIDENCE line so the output
//! can be pasted into the Phase F criteria-to-evidence map verbatim.

#include <QtTest>

#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

#include "PhaseDMixerTestSupport.h"

#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"

using namespace lmms;
using namespace partd;

namespace
{

//! User channels created for every measurement; 0 is always master.
constexpr int kUserChannels = 128;
//! The historically claimed fixed limit (SPEC 8.2: "not fixed 64").
constexpr int kClaimedLimit = 64;

EffectChain* chainOf(Mixer* mixer, int channel)
{
	return &mixer->mixerChannel(channel)->m_fxChain;
}

void fill(PeriodHarness& harness, float value)
{
	for (f_cnt_t f = 0; f < harness.fpp(); ++f)
	{
		harness.in()[f][0] = value;
		harness.in()[f][1] = value;
	}
}

void feedChannel(PeriodHarness& harness, int channel, float value)
{
	fill(harness, value);
	harness.feed(channel);
}

//! First-frame level of the master output (mean of L and R).
double masterLevel(const std::vector<SampleFrame>& out)
{
	return 0.5 * (static_cast<double>(out[0][0]) + static_cast<double>(out[0][1]));
}

void requireNear(double actual, double expected, double eps, const char* what)
{
	QVERIFY2(std::fabs(actual - expected) <= eps,
		qPrintable(QString("%1: expected %2, got %3")
			.arg(QString::fromUtf8(what))
			.arg(expected, 0, 'g', 12)
			.arg(actual, 0, 'g', 12)));
}

} // namespace

class PhaseFChannelScaleTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		initEngine();
		QVERIFY2(periodFrames() == 256, "frames per period is not 256");
	}

	void cleanupTestCase()
	{
		destroyEngine();
	}

	//! Every measurement starts from a known graph: master plus 128 user
	//! channels, each with its default send to master.
	void init()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		QCOMPARE(int(mixer->numChannels()), 1);
		for (int i = 0; i < kUserChannels; ++i)
		{
			QCOMPARE(mixer->createChannel(), i + 1);
		}
		QCOMPARE(int(mixer->numChannels()), kUserChannels + 1);
	}

	//! Criterion 1, part A: the API allocates more than 100 channels, each
	//! with a distinct index and its own default routing to master.
	void createsMoreThanOneHundredChannels()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY2(int(mixer->numChannels()) > 100,
			qPrintable(QString("only %1 channels exist").arg(mixer->numChannels())));

		std::set<int> indices;
		std::set<QString> names;
		for (int i = 1; i <= kUserChannels; ++i)
		{
			MixerChannel* ch = mixer->mixerChannel(i);
			QVERIFY2(ch != nullptr, qPrintable(QString("channel %1 is null").arg(i)));
			QCOMPARE(int(ch->index()), i);
			indices.insert(ch->index());
			names.insert(ch->m_name);

			// exactly one default send per channel, to master (index 0)
			QCOMPARE(int(ch->m_sends.size()), 1);
			QCOMPARE(int(ch->m_sends.front()->receiverIndex()), 0);
			QVERIFY2(ch->m_receives.empty(),
				qPrintable(QString("channel %1 has unexpected receives").arg(i)));
		}
		QCOMPARE(int(indices.size()), kUserChannels);
		QCOMPARE(int(names.size()), kUserChannels);
		QCOMPARE(int(mixer->mixerChannel(0)->m_receives.size()), kUserChannels);
		QVERIFY2(mixer->m_mixerRoutes.size() >= static_cast<std::size_t>(kUserChannels),
			"fewer routes than channels");

		evidence("PARTF_SCALE user_channels=%d num_channels=%d distinct_indices=%d "
			"distinct_names=%d master_receives=%d routes=%d",
			kUserChannels, int(mixer->numChannels()), int(indices.size()),
			int(names.size()), int(mixer->mixerChannel(0)->m_receives.size()),
			int(mixer->m_mixerRoutes.size()));
	}

	//! Criterion 1, part B: the boundary the "fixed 64" claim predicts is not
	//! special. Channels below, at and above 64, plus indices 100 and 127, all
	//! deliver their input to master at unity gain, and master sees exactly
	//! the arithmetic sum.
	void noFixedSixtyFourArtifact()
	{
		Mixer* mixer = Engine::mixer();
		PeriodHarness harness(mixer);

		struct Feed
		{
			int channel;
			float value;
		};
		const Feed feeds[] = {
			{kClaimedLimit - 1, 0.01f}, // 63, below the claimed limit
			{kClaimedLimit, 0.02f},     // 64, the claimed limit itself
			{kClaimedLimit + 1, 0.04f}, // 65, the first channel past it
			{100, 0.08f},               // the criterion's "100+"
			{127, 0.16f},               // the highest user channel
		};

		double expected = 0.0;
		harness.zeroInput();
		for (const Feed& feed : feeds)
		{
			feedChannel(harness, feed.channel, feed.value);
			expected += static_cast<double>(feed.value);
		}

		const double level = masterLevel(harness.render());
		requireNear(level, expected, 1.0e-6, "sum of channels 63/64/65/100/127");
		QVERIFY2(level > 0.0, "master output is silent");

		evidence("PARTF_BOUNDARY feeds=%d expected=%.6f measured=%.6f "
			"channel_64_alive=%d channel_127_alive=%d",
			int(sizeof(feeds) / sizeof(feeds[0])), expected, level,
			level > 0.0 ? 1 : 0, level > 0.0 ? 1 : 0);
	}

	//! Criterion 1, part C: routing between a channel above index 100 and
	//! master actually carries audio (non-zero output), measured alone.
	void channelAboveOneHundredCarriesAudioToMaster()
	{
		Mixer* mixer = Engine::mixer();
		PeriodHarness harness(mixer);

		harness.zeroInput();
		feedChannel(harness, 127, 0.5f);
		const double level = masterLevel(harness.render());
		requireNear(level, 0.5, 1.0e-6, "channel 127 -> master");
		QVERIFY2(level != 0.0, "channel 127 produced silence");

		evidence("PARTF_HIGH_TO_MASTER source_channel=127 input=0.500000 master=%.6f",
			level);
	}

	//! Criterion 1, part D: a regular send between two channels above index
	//! 100 works, and both the direct and the forwarded path reach master.
	void channelToChannelSendAboveOneHundred()
	{
		Mixer* mixer = Engine::mixer();
		PeriodHarness harness(mixer);

		QVERIFY2(mixer->createChannelSend(128, 126, 1.0f) != nullptr,
			"send 128 -> 126 was refused");
		QCOMPARE(int(mixer->mixerChannel(126)->m_receives.size()), 1);

		harness.zeroInput();
		feedChannel(harness, 128, 0.5f);
		// 128 keeps its own default send to master, so master sees the direct
		// 0.5 plus the 0.5 forwarded through 126: exactly 1.0.
		const double level = masterLevel(harness.render());
		requireNear(level, 1.0, 1.0e-6, "128 -> 126 -> master plus 128 -> master");

		evidence("PARTF_HIGH_SEND from=128 to=126 input=0.500000 master=%.6f", level);
	}

	//! Criterion 1, part E: a parallel bus created above index 100 sums three
	//! high-index senders, runs them through its own FX chain exactly once and
	//! reaches master.
	void parallelBusAboveOneHundred()
	{
		Mixer* mixer = Engine::mixer();
		PeriodHarness harness(mixer);

		const int bus = mixer->createBusChannel();
		QVERIFY2(bus > 100, qPrintable(QString("bus index %1 is not above 100").arg(bus)));
		QVERIFY2(mixer->isBusChannel(bus), "created channel is not a bus");
		chainOf(mixer, bus)->appendEffect(
			new FixedGainEffect(chainOf(mixer, bus), 0.5f, 0.5f));

		QVERIFY2(mixer->createChannelSend(100, bus, 1.0f) != nullptr,
			"send 100 -> bus was refused");
		QVERIFY2(mixer->createChannelSend(120, bus, 1.0f) != nullptr,
			"send 120 -> bus was refused");
		QVERIFY2(mixer->createChannelSend(127, bus, 1.0f) != nullptr,
			"send 127 -> bus was refused");
		QCOMPARE(int(mixer->mixerChannel(bus)->m_receives.size()), 3);

		harness.zeroInput();
		feedChannel(harness, 100, 0.2f);
		feedChannel(harness, 120, 0.1f);
		feedChannel(harness, 127, 0.05f);

		// direct: 0.2 + 0.1 + 0.05 = 0.35; via the bus: 0.35 * 0.5 = 0.175
		const double expected = 0.35 + 0.175;
		const double level = masterLevel(harness.render());
		requireNear(level, expected, 1.0e-6, "parallel bus at index > 100");

		evidence("PARTF_HIGH_BUS bus=%d direct=0.350000 bus_gain=0.500000 "
			"bus_out=0.175000 master=%.6f", bus, level);
	}

	//! Criterion 1, part F: a native sidechain send between high-index
	//! channels delivers the sender's tap to the receiver's sidechain input,
	//! without leaking into the audible mix.
	void sidechainSendAboveOneHundred()
	{
		Mixer* mixer = Engine::mixer();
		PeriodHarness harness(mixer);

		auto* probe = new TapProbeEffect(chainOf(mixer, 127));
		chainOf(mixer, 127)->appendEffect(probe);

		MixerSidechainRoute* sc = mixer->createSidechainSend(
			100, 127, 1.0f, SidechainTapPoint::PreFader);
		QVERIFY2(sc != nullptr, "sidechain send 100 -> 127 was refused");
		QCOMPARE(int(mixer->mixerChannel(127)->m_sidechainReceives.size()), 1);

		// Receiver gets a small input so its FX chain (and the probe) run;
		// the probe reads the sidechain, not the receiver's own audio.
		harness.zeroInput();
		feedChannel(harness, 100, 0.75f);
		feedChannel(harness, 127, 0.10f);
		const double withKey = masterLevel(harness.render());

		QVERIFY2(probe->sawSidechain(), "receiver 127 saw no sidechain buffer");
		requireNear(probe->firstL(), 0.75, 1.0e-6, "sidechain tap value at 127");
		// the key must not leak into the audible mix: master is exactly the
		// sender's 0.75 plus the receiver's 0.10; a leak would add another
		// 0.75.
		requireNear(withKey, 0.85, 1.0e-6, "master with sidechain key only");

		// the receiver still processes its own input normally alongside the key
		harness.zeroInput();
		feedChannel(harness, 100, 0.75f);
		feedChannel(harness, 127, 0.25f);
		const double both = masterLevel(harness.render());
		requireNear(both, 1.0, 1.0e-6, "master with key plus receiver input");
		requireNear(probe->firstL(), 0.75, 1.0e-6, "sidechain tap value, period 2");

		evidence("PARTF_HIGH_SIDECHAIN from=100 to=127 tap=prefader key=%.6f "
			"master_period1=%.6f master_period2=%.6f",
			double(probe->firstL()), withKey, both);
	}

	//! Criterion 1, part G: allocation keeps growing past 128 and the newest
	//! channel still carries audio to master.
	void growsBeyondOneHundredTwentyEight()
	{
		Mixer* mixer = Engine::mixer();
		const int extra = 72;
		for (int i = 0; i < extra; ++i)
		{
			QCOMPARE(mixer->createChannel(), kUserChannels + 1 + i);
		}
		QCOMPARE(int(mixer->numChannels()), kUserChannels + extra + 1);

		PeriodHarness harness(mixer);
		harness.zeroInput();
		feedChannel(harness, 200, 0.25f);
		const double level = masterLevel(harness.render());
		requireNear(level, 0.25, 1.0e-6, "channel 200 -> master");

		evidence("PARTF_GROWTH user_channels=%d num_channels=%d "
			"channel200_input=0.250000 master=%.6f",
			kUserChannels + extra, int(mixer->numChannels()), level);
	}

	//! Criterion 1, part H: the dependency counter and the job queue handle a
	//! 129-channel graph for repeated periods - no deadlock, no drift, no
	//! silent period.
	void repeatedPeriodsAreStable()
	{
		Mixer* mixer = Engine::mixer();
		PeriodHarness harness(mixer);

		const int periods = 10;
		for (int p = 0; p < periods; ++p)
		{
			harness.zeroInput();
			feedChannel(harness, 127, 0.5f);
			const double level = masterLevel(harness.render());
			requireNear(level, 0.5, 1.0e-6, "repeated period level");
		}

		evidence("PARTF_STABILITY periods=%d expected=0.500000 ok=1", periods);
	}
};

QTEST_GUILESS_MAIN(PhaseFChannelScaleTest)
#include "PhaseFChannelScaleTest.moc"
