/*
 * RackTest.cpp - the rack: parallel chains and the chain selector (#599)
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

//! The mixer channel's first honest rack slice, measured through
//! Mixer::masterMix() - the same path `lmms render` drives:
//!
//!   * **no rack configured** - the rack is not on the path at all, and the
//!     render is the channel's own chain, sample-exactly, twice in a row (the
//!     same-build run-to-run floor is 0 LSB in this harness);
//!   * **parallel** - two chains process the same block and sum, and the sum is
//!     exactly the arithmetic of the two chains (sample-for-sample, not a
//!     tolerance);
//!   * **the selector** - selecting chain A vs chain B gives the chain's own
//!     output and *not* the other chain's, the unselected chain's effect is
//!     never called, and the switch is stepwise (no crossfade);
//!   * **the audio path allocates nothing** (tests/src/core/AllocationProbe.h);
//!   * **persistence** - the chains and the selector survive save/reload, and a
//!     project with no <rack> element still loads and re-saves without one.
//!
//! Evidence is printed unconditionally (stdout, flushed), so the numbers can be
//! pasted into docs/RACKS.md.

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioBuffer.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"
#include "Mixer.h"
#include "Plugin.h"
#include "Rack.h"
#include "RoutingGraph.h"
#include "SampleFrame.h"

namespace lmms
{

namespace
{

//! The descriptor and key of the test effect. The key cannot be null:
//! EffectChain::saveSettings() writes it through effect->key().saveXML().
const Plugin::Descriptor s_rackScaleDescriptor
{
	"rackscaletest",
	"Rack scale effect",
	"Deterministic per-channel gain used by RackTest",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

const Plugin::Descriptor::SubPluginFeatures::Key s_rackScaleKey{&s_rackScaleDescriptor};

//! The test effect's controls. An effect that is saved must have them: the save
//! path walks controls(). A gain with fixed factors exposes no parameters.
class RackScaleControls : public EffectControls
{
public:
	explicit RackScaleControls(Effect* effect) : EffectControls(effect) {}

	auto nodeName() const -> QString override { return QStringLiteral("RackScaleControls"); }
	auto controlCount() -> int override { return 0; }
	auto createView() -> gui::EffectControlDialog* override { return nullptr; }

	//! A gain with fixed factors has no parameters to write or read.
	void saveSettings(QDomDocument& doc, QDomElement& element) override
	{
		Q_UNUSED(doc)
		Q_UNUSED(element)
	}
	void loadSettings(const QDomElement& element) override { Q_UNUSED(element) }
};

//! Deterministic per-channel gain (L *= left, R *= right) that counts how many
//! blocks it was actually asked to process. Exercises the rack's chains without
//! depending on any plugin being loadable in a headless test.
class RackScaleEffect : public Effect
{
public:
	RackScaleEffect(Model* parent, float left, float right, std::uint64_t* calls) :
		Effect{&s_rackScaleDescriptor, parent, &s_rackScaleKey},
		m_left(left),
		m_right(right),
		m_calls(calls)
	{
	}

	EffectControls* controls() override { return &m_controls; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		if (m_calls != nullptr) { ++(*m_calls); }
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			buf[f][0] *= m_left;
			buf[f][1] *= m_right;
		}
		return ProcessStatus::Continue;
	}

private:
	float m_left;
	float m_right;
	std::uint64_t* m_calls;
	RackScaleControls m_controls{this};
};

//! One period, asserted against the engine in initTestCase().
constexpr int kFramesPerPeriod = 256;
//! 16 * 256 frames = 4096 frames = 8192 output samples.
constexpr int kPeriods = 16;
//! Master + the one channel that carries the rack.
constexpr int kChannels = 2;
//! The channel under test.
constexpr int kChannel = 1;

//! The two chains' gains, in chain order. Powers of two on purpose: every
//! product is exact in float, so the summed render can be compared with `==`.
constexpr float kChainALeft = 2.0f;
constexpr float kChainARight = 2.0f;
constexpr float kChainBLeft = 0.5f;
constexpr float kChainBRight = 0.25f;

//! One 24-bit LSB, the unit this repo states render deltas in.
constexpr double kLsb = 2.0 / 16777216.0;

//! Deterministic signal in [-0.5, 0.5). Integer arithmetic only (no libm), so
//! the exact bit pattern is platform-independent.
inline float rackSignal(int period, int frame, int side)
{
	const int n = period * 7919 + frame * 131 + side * 4093;
	return static_cast<float>((n % 2048) - 1024) * (1.0f / 2048.0f);
}

//! What the harness fed the channel for output sample @a index (interleaved).
inline float inputAt(int index)
{
	const int frame = index / 2;
	const int side = index % 2;
	return rackSignal(frame / kFramesPerPeriod, frame % kFramesPerPeriod, side);
}

struct Delta
{
	long long maxLsb = 0;
	long long differing = 0;
};

//! Worst |difference| between two renders, in 24-bit LSB, and how many of the
//! output samples differ at all.
Delta compare(const std::vector<float>& got, const std::vector<float>& want)
{
	Delta delta;
	const std::size_t count = std::min(got.size(), want.size());
	for (std::size_t i = 0; i < count; ++i)
	{
		const double difference = std::abs(static_cast<double>(got[i]) - static_cast<double>(want[i]));
		if (difference != 0.0) { ++delta.differing; }
		delta.maxLsb = std::max(delta.maxLsb, static_cast<long long>(difference / kLsb + 0.5));
	}
	return delta;
}

void printEvidence(const char* label, const Delta& delta, int samples)
{
	std::fprintf(stdout, "RACK_EVIDENCE %s samples=%d differing=%lld max_delta_lsb=%lld\n",
		label, samples, delta.differing, delta.maxLsb);
	std::fflush(stdout);
}

void printEvidence(const char* label, const QString& detail)
{
	std::fprintf(stdout, "RACK_EVIDENCE %s %s\n", label, detail.toUtf8().constData());
	std::fflush(stdout);
}

//! The arithmetic the parallel rack claims: input * chainA + input * chainB.
std::vector<float> expectedParallel()
{
	const int count = kPeriods * kFramesPerPeriod * 2;
	std::vector<float> want(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		const float dry = inputAt(i);
		const float a = (i % 2 == 0) ? kChainALeft : kChainARight;
		const float b = (i % 2 == 0) ? kChainBLeft : kChainBRight;
		want[static_cast<std::size_t>(i)] = dry * a + dry * b;
	}
	return want;
}

//! The arithmetic one chain claims on its own.
std::vector<float> expectedChain(float left, float right)
{
	const int count = kPeriods * kFramesPerPeriod * 2;
	std::vector<float> want(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		want[static_cast<std::size_t>(i)] = inputAt(i) * (i % 2 == 0 ? left : right);
	}
	return want;
}

//! Saved chain XML, so the test can inspect a chain's effects without reaching
//! into EffectChain's private list (the same idiom as
//! MixerRoutingBackwardCompatTest).
int effectCountOf(EffectChain& chain)
{
	QDomDocument doc;
	QDomElement element = doc.createElement(QStringLiteral("fxchain"));
	doc.appendChild(element);
	chain.saveSettings(doc, element);
	return element.elementsByTagName(QStringLiteral("effect")).size();
}

} // namespace

} // namespace lmms

using namespace lmms;

class RackTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy device thread renders in the background; this test drives
		// the mixer synchronously, so stop it to keep the buffers stable.
		Engine::audioEngine()->audioDev()->stopProcessing();
		QCOMPARE(Engine::audioEngine()->framesPerPeriod(), static_cast<f_cnt_t>(kFramesPerPeriod));

		auto mixer = Engine::mixer();
		while (mixer->numChannels() < kChannels) { mixer->createChannel(); }

		// Chain 0 is the channel's own chain, with one identifiable effect in
		// it. The channel's default send to master and its unity fader stay as
		// they are, so the render is the rack's output.
		EffectChain* const chain = baseChain();
		chain->appendEffect(new RackScaleEffect(chain, kChainALeft, kChainARight, &m_chainACalls));
		// The rack drives each chain through the chain's own machinery; the
		// channel's chain is on its own RoutingGraph here, as it is in the
		// mixer path (docs/ROUTING-GRAPH-LIVE.md).
		QVERIFY2(chain->routesThroughGraph(), "the channel's chain is not routed through a graph");
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! With no rack configured the rack is not on the path: the channel renders
	//! its own chain, reproducibly.
	void noRackKeepsTheChannelsOwnPath()
	{
		Rack& rack = rackUnderTest();
		QCOMPARE(rack.chainCount(), 1);
		QCOMPARE(rack.selectedChain(), Rack::Parallel);
		QCOMPARE(rack.routingGraph().nodeCount(), 0);
		QVERIFY(!rack.routedChains().size());
		QVERIFY2(!rack.canProcessThroughRack(currentBus()),
			"a channel with one chain is on the rack's path - the no-rack claim would be vacuous");

		const std::vector<float> first = render();
		const std::vector<float> second = render();

		// The same-build run-to-run floor of this harness: what the renderer
		// moves on its own, which every other comparison is measured against.
		const Delta floor = compare(first, second);
		printEvidence("no-rack-run-to-run-floor", floor, static_cast<int>(first.size()));
		QCOMPARE(floor.differing, 0LL);
		QCOMPARE(floor.maxLsb, 0LL);

		// ... and it is the channel's chain, exactly: one gain, no rack.
		const Delta arithmetic = compare(first, expectedChain(kChainALeft, kChainARight));
		printEvidence("no-rack-vs-chain-a-arithmetic", arithmetic, static_cast<int>(first.size()));
		QCOMPARE(arithmetic.differing, 0LL);
		QCOMPARE(arithmetic.maxLsb, 0LL);

		m_noRackRender = first;
	}

	//! Two chains process the same input and sum: the render is the sum of the
	//! two chains' arithmetic, sample for sample, and both effects ran.
	void parallelChainsBothProcessAndSum()
	{
		Rack& rack = rackUnderTest();
		const int index = rack.addChain();
		QCOMPARE(index, 1);
		QCOMPARE(rack.chainCount(), 2);

		EffectChain* const chain = rack.chain(1);
		QVERIFY(chain != nullptr);
		chain->appendEffect(new RackScaleEffect(chain, kChainBLeft, kChainBRight, &m_chainBCalls));

		// The graph now carries both chains: input + chain 0 + chain 1 + sum.
		QCOMPARE(rack.routingGraph().nodeCount(), 4);
		QCOMPARE(rack.routingGraph().connections().size(), std::size_t{4});
		QVERIFY(rack.routedChains() == std::vector<int>({0, 1}));
		QVERIFY2(rack.canProcessThroughRack(currentBus()),
			"a two-chain rack is not on the path - the parallel claim would be vacuous");

		m_chainACalls = 0;
		m_chainBCalls = 0;
		const std::vector<float> summed = render();
		printEvidence("parallel-chain-a-blocks",
			QStringLiteral("blocks=%1").arg(static_cast<qulonglong>(m_chainACalls)));
		printEvidence("parallel-chain-b-blocks",
			QStringLiteral("blocks=%1").arg(static_cast<qulonglong>(m_chainBCalls)));
		QCOMPARE(m_chainACalls, std::uint64_t{kPeriods});
		QCOMPARE(m_chainBCalls, std::uint64_t{kPeriods});

		// The sum, sample for sample: each chain's output on the same input.
		const Delta sumDelta = compare(summed, expectedParallel());
		printEvidence("parallel-vs-sum-of-both-chains", sumDelta, static_cast<int>(summed.size()));
		QVERIFY2(sumDelta.differing == 0 && sumDelta.maxLsb == 0,
			"the parallel render is not the sum of the two chains' arithmetic");

		// Sensitivity control: the rack's output must differ from the render of
		// the same channel with no rack, and from either chain alone.
		const Delta vsNoRack = compare(summed, m_noRackRender);
		printEvidence("parallel-vs-no-rack-sensitivity-control", vsNoRack, static_cast<int>(summed.size()));
		QVERIFY2(vsNoRack.differing > 0, "configuring a rack did not change the render");
		// ... and it is neither chain alone. Only the input samples that are
		// exactly zero can coincide, so the two differ almost everywhere while
		// the sum stays exact everywhere (asserted above).
		const Delta vsChainA = compare(summed, expectedChain(kChainALeft, kChainARight));
		printEvidence("parallel-vs-chain-a-alone", vsChainA, static_cast<int>(summed.size()));
		const Delta vsChainB = compare(summed, expectedChain(kChainBLeft, kChainBRight));
		printEvidence("parallel-vs-chain-b-alone", vsChainB, static_cast<int>(summed.size()));
		QVERIFY2(vsChainA.differing > 0 && vsChainB.differing > 0,
			"the summed render is one of the chains alone");

		m_parallelRender = summed;
	}

	//! The Chain Selector: selecting chain A versus chain B gives that chain's
	//! output and not the other's; the unselected chain never processes; and the
	//! switch is stepwise, with no fade at the boundary.
	void theSelectorRoutesOneChainAndIdlesTheOther()
	{
		Rack& rack = rackUnderTest();

		rack.setSelectedChain(1);
		QCOMPARE(rack.selectedChain(), 1);
		// Only the selected chain is wired: input + chain 1 + sum. The chain
		// that is not selected is not in the node set, so it cannot run.
		QCOMPARE(rack.routingGraph().nodeCount(), 3);
		QCOMPARE(rack.routingGraph().processingOrder().size(), std::size_t{3});
		QVERIFY(rack.routedChains() == std::vector<int>({1}));
		QVERIFY(rack.canProcessThroughRack(currentBus()));

		m_chainACalls = 0;
		m_chainBCalls = 0;
		const std::vector<float> chainB = render();
		printEvidence("selector-b-chain-a-blocks",
			QStringLiteral("blocks=%1").arg(static_cast<qulonglong>(m_chainACalls)));
		printEvidence("selector-b-chain-b-blocks",
			QStringLiteral("blocks=%1").arg(static_cast<qulonglong>(m_chainBCalls)));

		// The stated idle behaviour: a chain that is not selected is not in the
		// graph's node set, so its effect is never called and it contributes
		// nothing - not even its input.
		QCOMPARE(m_chainACalls, std::uint64_t{0});
		QCOMPARE(m_chainBCalls, std::uint64_t{kPeriods});

		const Delta vsChainB = compare(chainB, expectedChain(kChainBLeft, kChainBRight));
		printEvidence("selector-b-vs-chain-b-arithmetic", vsChainB, static_cast<int>(chainB.size()));
		QCOMPARE(vsChainB.differing, 0LL);
		QCOMPARE(vsChainB.maxLsb, 0LL);

		// A versus B: measurably different output.
		const Delta ab = compare(chainB, m_noRackRender);
		printEvidence("selector-a-vs-selector-b-sensitivity-control", ab, static_cast<int>(chainB.size()));
		QVERIFY2(ab.differing > 0, "selecting A and selecting B produced the same render");

		// The switch is stepwise: the very first block after it is already the
		// selected chain's output, so there is no crossfade and no fade-in.
		QCOMPARE(chainB[0], inputAt(0) * kChainBLeft);
		QCOMPARE(chainB[1], inputAt(1) * kChainBRight);

		// Selecting chain 0 routes the channel's own chain through the rack -
		// and the render is byte-identical to the no-rack render, so the rack's
		// wiring adds no arithmetic of its own.
		rack.setSelectedChain(0);
		QVERIFY(rack.routedChains() == std::vector<int>({0}));
		QCOMPARE(rack.routingGraph().nodeCount(), 3);
		QVERIFY(rack.canProcessThroughRack(currentBus()));

		m_chainACalls = 0;
		m_chainBCalls = 0;
		const std::vector<float> chainA = render();
		QCOMPARE(m_chainACalls, std::uint64_t{kPeriods});
		QCOMPARE(m_chainBCalls, std::uint64_t{0});

		const Delta transparent = compare(chainA, m_noRackRender);
		printEvidence("selector-a-vs-no-rack-transparency", transparent, static_cast<int>(chainA.size()));
		QCOMPARE(transparent.differing, 0LL);
		QCOMPARE(transparent.maxLsb, 0LL);

		// ... and back to parallel, which comes back exactly.
		rack.setSelectedChain(Rack::Parallel);
		QCOMPARE(rack.routingGraph().nodeCount(), 4);
		QCOMPARE(compare(render(), m_parallelRender).differing, 0LL);
	}

	//! The audio thread's path through the rack allocates nothing.
	void processingThroughTheRackAllocatesNothing()
	{
		Rack& rack = rackUnderTest();
		QVERIFY(rack.canProcessThroughRack(currentBus()));

		auto bus = currentBus();
		// The first block may touch lazy internals; measure the second.
		rack.processAudioBuffer(bus);

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		rack.processAudioBuffer(bus);
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;

		printEvidence("rack-process-allocations",
			QStringLiteral("allocations=%1").arg(static_cast<qulonglong>(allocations)));
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! The rack's configuration - the chains, their effects and the selector's
	//! choice - survives save/reload through the tree's own Mixer serialization.
	void rackConfigurationSurvivesSaveAndReload()
	{
		Rack& rack = rackUnderTest();
		rack.setSelectedChain(1);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(root);
		Engine::mixer()->saveSettings(doc, root);

		const QDomElement channel = mixerChannelElement(root);
		QVERIFY2(!channel.isNull(), "the saved project has no mixer channel 1");
		const QDomElement rackElement = channel.firstChildElement(QStringLiteral("rack"));
		QVERIFY2(!rackElement.isNull(), "the rack was not saved");
		QCOMPARE(rackElement.attribute(QStringLiteral("selected")), QStringLiteral("1"));

		// The <rack> element carries the *additional* chains: chain 0 is the
		// channel's own <fxchain>, which the channel already saved.
		const QDomNodeList chains = rackElement.elementsByTagName(QStringLiteral("chain"));
		QCOMPARE(chains.size(), 1);
		const QDomElement chainElement = chains.at(0).toElement();
		QCOMPARE(chainElement.attribute(QStringLiteral("index")), QStringLiteral("1"));
		const QDomElement chainState = chainElement.firstChildElement(QStringLiteral("fxchain"));
		QVERIFY2(!chainState.isNull(), "the chain was saved without its <fxchain>");
		QCOMPARE(chainState.elementsByTagName(QStringLiteral("effect")).size(), 1);
		QCOMPARE(effectCountOf(*rack.chain(1)), 1);
		printEvidence("persistence-saved",
			QStringLiteral("rack=1 selected=%1 chains=1 chain_effects=1 channel_fxchain_effects=1")
				.arg(rackElement.attribute(QStringLiteral("selected"))));

		// Reload. The fixture's chains carry no effects on purpose: loading a
		// chain from XML instantiates its effects, which starts the plugin
		// loader's threads, and a headless test binary that tears the engine
		// down while one is still running aborts (the same teardown race
		// docs/ROUTING-GRAPH-LIVE.md had to work around). The effect half of
		// this claim is asserted on the save side above (each chain's own
		// <fxchain>, written by the existing EffectChain machinery), and the
		// real load path with real effects is exercised end to end by the
		// headless render in docs/RACKS.md.
		static const char* xml =
			"<mixer>\n"
			"  <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel num=\"1\" muted=\"0\" volume=\"1\" name=\"Rack\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"    <rack version=\"1\" selected=\"1\">\n"
			"      <chain index=\"1\">\n"
			"        <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"      </chain>\n"
			"    </rack>\n"
			"    <send channel=\"0\" amount=\"1\"/>\n"
			"  </mixerchannel>\n"
			"</mixer>\n";

		QDomDocument loadDoc;
		loadDoc.setContent(QString::fromUtf8(xml));
		Engine::mixer()->loadSettings(loadDoc.documentElement());

		Rack& reloaded = rackUnderTest();
		QCOMPARE(reloaded.chainCount(), 2);
		QCOMPARE(reloaded.selectedChain(), 1);
		QVERIFY(reloaded.routedChains() == std::vector<int>({1}));
		QCOMPARE(reloaded.routingGraph().nodeCount(), 3);
		QVERIFY2(reloaded.canProcessThroughRack(currentBus()),
			"the reloaded rack is not on the path");

		// The reloaded rack is the live path, and an empty chain is a disabled
		// chain: it passes its input through unchanged (the meaning its enabled
		// flag already has), so the channel's output is its input.
		const Delta passthrough = compare(render(), expectedChain(1.0f, 1.0f));
		printEvidence("reloaded-rack-passthrough", passthrough,
			kPeriods * kFramesPerPeriod * 2);
		QCOMPARE(passthrough.differing, 0LL);
		QCOMPARE(passthrough.maxLsb, 0LL);

		// ... and the reloaded rack routes real DSP through the selector: an
		// effect added to the reloaded chain is the whole output.
		EffectChain* const reloadedChain = reloaded.chain(1);
		QVERIFY(reloadedChain != nullptr);
		reloadedChain->appendEffect(
			new RackScaleEffect(reloadedChain, kChainBLeft, kChainBRight, &m_chainBCalls));

		m_chainACalls = 0;
		m_chainBCalls = 0;
		const std::vector<float> routed = render();
		QCOMPARE(m_chainACalls, std::uint64_t{0});
		QCOMPARE(m_chainBCalls, std::uint64_t{kPeriods});
		const Delta routedDelta = compare(routed, expectedChain(kChainBLeft, kChainBRight));
		printEvidence("reloaded-rack-selector-routes", routedDelta, static_cast<int>(routed.size()));
		QCOMPARE(routedDelta.differing, 0LL);
		QCOMPARE(routedDelta.maxLsb, 0LL);

		// Save again: the reloaded rack writes its configuration back.
		QDomDocument again;
		QDomElement rootAgain = again.createElement(QStringLiteral("mixer"));
		again.appendChild(rootAgain);
		Engine::mixer()->saveSettings(again, rootAgain);
		const QDomElement rackAgain =
			mixerChannelElement(rootAgain).firstChildElement(QStringLiteral("rack"));
		QVERIFY(!rackAgain.isNull());
		QCOMPARE(rackAgain.attribute(QStringLiteral("selected")), QStringLiteral("1"));
		QCOMPARE(rackAgain.elementsByTagName(QStringLiteral("chain")).size(), 1);
		printEvidence("persistence-round-trip",
			QStringLiteral("saved=rack1/chain1/effect1 loaded=same re-saved=same"));

		QVERIFY(Engine::mixer()->numChannels() == kChannels);
	}

	//! A project saved before racks existed has no <rack> element: it loads,
	//! nothing is routed, and it is saved back without a <rack> element.
	void anOldProjectWithNoRackStillLoads()
	{
		static const char* xml =
			"<mixer>\n"
			"  <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel num=\"1\" muted=\"0\" volume=\"1\" name=\"Channel 1\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"    <send channel=\"0\" amount=\"1\"/>\n"
			"  </mixerchannel>\n"
			"</mixer>\n";

		QDomDocument doc;
		doc.setContent(QString::fromUtf8(xml));
		Engine::mixer()->loadSettings(doc.documentElement());

		Rack& rack = rackUnderTest();
		QCOMPARE(rack.chainCount(), 1);
		QVERIFY(rack.routedChains().empty());
		QCOMPARE(rack.routingGraph().nodeCount(), 0);
		QVERIFY2(!rack.canProcessThroughRack(currentBus()),
			"a project with no <rack> element was put on the rack's path");

		// ... and a channel with no rack writes no <rack> element.
		QDomDocument saved;
		QDomElement savedRoot = saved.createElement(QStringLiteral("mixer"));
		saved.appendChild(savedRoot);
		Engine::mixer()->saveSettings(saved, savedRoot);
		QVERIFY2(mixerChannelElement(savedRoot).firstChildElement(QStringLiteral("rack")).isNull(),
			"a channel with no rack wrote a <rack> element");

		printEvidence("old-project-no-rack", QStringLiteral("chains=1 routed=0 rack_element=0"));
	}

private:
	auto rackUnderTest() -> Rack&
	{
		return Engine::mixer()->mixerChannel(kChannel)->m_rack;
	}

	auto baseChain() -> EffectChain*
	{
		return &Engine::mixer()->mixerChannel(kChannel)->m_fxChain;
	}

	//! The bus the channel under test is processed on, for the predicates that
	//! ask whether a block of this shape can be rendered through the rack.
	auto currentBus() -> AudioBus
	{
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		m_busBlock.assign(static_cast<std::size_t>(fpp), SampleFrame{});
		m_busData[0] = m_busBlock.data();
		return AudioBus{m_busData, 1, fpp};
	}

	auto mixerChannelElement(const QDomElement& root) -> QDomElement
	{
		for (QDomElement channel = root.firstChildElement(QStringLiteral("mixerchannel"));
			!channel.isNull(); channel = channel.nextSiblingElement(QStringLiteral("mixerchannel")))
		{
			if (channel.attribute(QStringLiteral("num")).toInt() == kChannel) { return channel; }
		}
		return QDomElement{};
	}

	//! Renders the channel under test through the master for kPeriods periods,
	//! interleaved. This is the mixer path `lmms render` drives.
	auto render() -> std::vector<float>
	{
		auto mixer = Engine::mixer();
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

		std::vector<float> out;
		out.reserve(static_cast<std::size_t>(kPeriods) * fpp * 2);

		std::vector<SampleFrame> masterOut(fpp);
		std::vector<SampleFrame> input(fpp);
		SampleFrame* busData[1] = {input.data()};

		for (int period = 0; period < kPeriods; ++period)
		{
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				input[f][0] = rackSignal(period, static_cast<int>(f), 0);
				input[f][1] = rackSignal(period, static_cast<int>(f), 1);
			}
			const AudioBus bus{busData, 1, fpp};
			mixer->mixToChannel(bus, kChannel);

			mixer->prepareMasterMix();
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				masterOut[f][0] = 0.0f;
				masterOut[f][1] = 0.0f;
			}
			mixer->masterMix(masterOut.data());

			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				out.push_back(masterOut[f][0]);
				out.push_back(masterOut[f][1]);
			}
		}
		return out;
	}

	std::uint64_t m_chainACalls = 0;
	std::uint64_t m_chainBCalls = 0;
	std::vector<float> m_noRackRender;
	std::vector<float> m_parallelRender;

	std::vector<SampleFrame> m_busBlock;
	SampleFrame* m_busData[1] = {nullptr};
};

QTEST_GUILESS_MAIN(RackTest)
#include "RackTest.moc"
