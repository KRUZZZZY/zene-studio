/*
 * PhaseDSidechainTest.cpp - Phase D sidechain routing behaviour tests (#587)
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

//! SPEC-dynamic-routing.md v1.2 section 8.1, Phase D tests:
//!
//!  * tap-point semantics: the four tap points (pre-FX, pre-fader,
//!    post-fader, post-fader-no-gain) deliver distinct, analytically
//!    predictable sidechain signals;
//!  * sidechain ducking: a keyed receiver's gain follows the key channel;
//!  * parallel bus demo: two channels route through a shared bus whose
//!    native Compressor ducks on a sidechain send - no Peak Controller;
//!  * a parallel bus sums its inputs and runs them through its FX chain
//!    exactly once;
//!  * cycle-forming sidechain sends are rejected.
//!
//! Every measurement is printed as a flushed PARTD_EVIDENCE line so the
//! output can be pasted into PART-D-SIDECHAIN.md verbatim.

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "PhaseDMixerTestSupport.h"

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "Plugin.h"

using namespace lmms;
using namespace partd;

namespace
{

EffectChain* chainOf(Mixer* mixer, int channel)
{
	return &mixer->mixerChannel(channel)->m_fxChain;
}

//! Mix a constant (DC) signal into a channel's input.
void feedDc(PeriodHarness& harness, int channel, float value)
{
	for (f_cnt_t f = 0; f < harness.fpp(); ++f)
	{
		harness.in()[f][0] = value;
		harness.in()[f][1] = value;
	}
	harness.feed(channel);
}

bool nearValue(float actual, float expected, float eps = 1.0e-6f)
{
	return std::fabs(actual - expected) <= eps;
}

} // namespace

class PhaseDSidechainTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// Isolate the native Compressor BEFORE the engine starts: the
		// plugin factory scans $LMMS_PLUGIN_DIR once, on first access, so
		// point it at a private directory holding just a link to the
		// compressor built by the normal build.
		const QString compressor = QString::fromUtf8(PART_D_COMPRESSOR_LIBRARY);
		QVERIFY2(QFile::exists(compressor), qPrintable(compressor));
		m_pluginDir = std::make_unique<QTemporaryDir>();
		QVERIFY2(m_pluginDir->isValid(), "could not create the plugin directory");
		const QString linkPath = m_pluginDir->path() + QString("/libcompressor.so");
		QVERIFY2(QFile::link(compressor, linkPath), "could not link libcompressor.so");
		qputenv("LMMS_PLUGIN_DIR", m_pluginDir->path().toUtf8());

		initEngine();
		QVERIFY2(periodFrames() == 256, "frames per period is not 256");

		// The D3 gate is specified at 48 kHz / 256 frames. The test engine
		// defaults to 44.1 kHz; every sample-rate consumer reads
		// AudioEngine::outputSampleRate(), i.e. the device rate, so pinning
		// the dummy device pins the whole DSP path. Done before any effect
		// is instantiated so plugin coefficients are computed for 48 kHz.
		Engine::audioEngine()->audioDev()->setSampleRateForTesting(48000);
		QVERIFY2(Engine::audioEngine()->outputSampleRate() == 48000u,
			"the output sample rate did not follow the device rate");

		evidence("ENV sample_rate=%d frames_per_period=%d plugin_dir=%s",
			static_cast<int>(Engine::audioEngine()->outputSampleRate()),
			static_cast<int>(periodFrames()),
			m_pluginDir->path().toUtf8().constData());
	}

	void cleanupTestCase()
	{
		destroyEngine();
	}

	//! 8.1 tap points: pre-FX / pre-fader / post-fader / post-fader-no-gain
	//! must hand the receiver different, exactly predictable signals.
	void tapPointsDeliverDistinctSidechainSignals()
	{
		auto mixer = Engine::mixer();
		mixer->clear(); // each test builds its own graph from the master channel
		while (mixer->numChannels() < 6) { mixer->createChannel(); }

		// Sender: gain 2.0 in the FX chain, fader 0.25, input 0.5.
		const float input = 0.5f;
		const float fxGain = 2.0f;
		const float fader = 0.25f;

		mixer->mixerChannel(1)->m_volumeModel.setValue(fader);
		chainOf(mixer, 1)->appendEffect(new FixedGainEffect(chainOf(mixer, 1), fxGain, fxGain));

		auto* probePreFx = new TapProbeEffect(chainOf(mixer, 2));
		auto* probePreFader = new TapProbeEffect(chainOf(mixer, 3));
		auto* probePostFader = new TapProbeEffect(chainOf(mixer, 4));
		auto* probeNoGain = new TapProbeEffect(chainOf(mixer, 5));
		chainOf(mixer, 2)->appendEffect(probePreFx);
		chainOf(mixer, 3)->appendEffect(probePreFader);
		chainOf(mixer, 4)->appendEffect(probePostFader);
		chainOf(mixer, 5)->appendEffect(probeNoGain);

		QVERIFY(mixer->createSidechainSend(1, 2, 1.0f, SidechainTapPoint::PreFx) != nullptr);
		QVERIFY(mixer->createSidechainSend(1, 3, 1.0f, SidechainTapPoint::PreFader) != nullptr);
		// half send amount: post-fader applies it, post-fader-no-gain ignores it
		QVERIFY(mixer->createSidechainSend(1, 4, 0.5f, SidechainTapPoint::PostFader) != nullptr);
		QVERIFY(mixer->createSidechainSend(1, 5, 0.5f,
			SidechainTapPoint::PostFaderNoGain) != nullptr);

		PeriodHarness harness(mixer);
		feedDc(harness, 1, input);
		// Receivers get a small input so their FX chains are awake and their
		// probes run; the probe reads the sidechain, not the audio.
		for (int ch = 2; ch <= 5; ++ch) { feedDc(harness, ch, 0.1f); }
		harness.render();

		const float expectPreFx = input;
		const float expectPreFader = input * fxGain;
		const float expectPostFader = input * fxGain * fader * 0.5f; // send amount applied
		const float expectNoGain = input * fxGain * fader; // send amount ignored

		QVERIFY2(probePreFx->sawSidechain(), "pre-FX receiver saw no sidechain");
		QVERIFY2(probePreFader->sawSidechain(), "pre-fader receiver saw no sidechain");
		QVERIFY2(probePostFader->sawSidechain(), "post-fader receiver saw no sidechain");
		QVERIFY2(probeNoGain->sawSidechain(), "post-fader-no-gain receiver saw no sidechain");

		QVERIFY2(nearValue(probePreFx->firstL(), expectPreFx),
			qPrintable(QString("pre-FX tap %1 != %2")
				.arg(probePreFx->firstL()).arg(expectPreFx)));
		QVERIFY2(nearValue(probePreFader->firstL(), expectPreFader),
			qPrintable(QString("pre-fader tap %1 != %2")
				.arg(probePreFader->firstL()).arg(expectPreFader)));
		QVERIFY2(nearValue(probePostFader->firstL(), expectPostFader),
			qPrintable(QString("post-fader tap %1 != %2")
				.arg(probePostFader->firstL()).arg(expectPostFader)));
		QVERIFY2(nearValue(probeNoGain->firstL(), expectNoGain),
			qPrintable(QString("post-fader-no-gain tap %1 != %2 (amount must be ignored)")
				.arg(probeNoGain->firstL()).arg(expectNoGain)));

		evidence("TAP_POINT input=%.4f fx=%.2f fader=%.2f "
			"pre_fx=%.6f(expect %.6f) pre_fader=%.6f(expect %.6f) "
			"post_fader=%.6f(expect %.6f) post_fader_no_gain=%.6f(expect %.6f)",
			input, fxGain, fader,
			probePreFx->firstL(), expectPreFx,
			probePreFader->firstL(), expectPreFader,
			probePostFader->firstL(), expectPostFader,
			probeNoGain->firstL(), expectNoGain);
	}

	//! 8.1 ducking: the receiver's gain follows the key channel. The ducker
	//! is deterministic (gain = 1 / (1 + amount * |key|)), so the measured
	//! level ratio has a closed-form expectation.
	void duckingFollowsTheSidechainKey()
	{
		auto mixer = Engine::mixer();
		mixer->clear(); // each test builds its own graph from the master channel
		while (mixer->numChannels() < 3) { mixer->createChannel(); }

		// Channel 1 = key, channel 2 = target.
		mixer->channelSendModel(1, 0)->setValue(0.0f); // key is not audible
		auto* duck = new KeyedDuckEffect(chainOf(mixer, 2), 8.0f);
		chainOf(mixer, 2)->appendEffect(duck);
		QVERIFY(mixer->createSidechainSend(1, 2, 1.0f,
			SidechainTapPoint::PostFader) != nullptr);

		const float target = 0.5f;
		const float key = 0.5f;
		const float expectedGain = 1.0f / (1.0f + 8.0f * key); // 0.2

		PeriodHarness harness(mixer);

		duck->resetStats();
		feedDc(harness, 1, 0.0f);
		feedDc(harness, 2, target);
		const double openLevel = meanAbs(harness.render(), 0, harness.fpp());
		QVERIFY2(duck->keyedBlocks() == 0, "ducker keyed while the key was silent");
		QVERIFY2(nearValue(static_cast<float>(openLevel), target, 1.0e-6f),
			"unkeyed target is not transparent");

		duck->resetStats();
		feedDc(harness, 1, key);
		feedDc(harness, 2, target);
		const double duckedLevel = meanAbs(harness.render(), 0, harness.fpp());

		QVERIFY2(duck->keyedBlocks() > 0, "ducker was not keyed by the key channel");
		QVERIFY2(nearValue(duck->lastKeyPeak(), key, 1.0e-6f),
			"the key peak seen by the ducker does not match the key channel");

		const double ratio = duckedLevel / openLevel;
		QVERIFY2(std::fabs(ratio - expectedGain) < 1.0e-3,
			qPrintable(QString("ducked/open = %1, expected %2")
				.arg(ratio).arg(expectedGain)));

		evidence("DUCKING target=%.4f key=%.4f open=%.6f ducked=%.6f "
			"ratio=%.6f(expect %.6f) drop_db=%.2f key_peak=%.4f keyed_blocks=%d",
			target, key, openLevel, duckedLevel, ratio, expectedGain,
			toDb(ratio), duck->lastKeyPeak(), duck->keyedBlocks());
	}

	//! 8.1 parallel bus: a bus sums its (pre-fader) inputs and runs the sum
	//! through its FX chain exactly once. Sender faders must not matter.
	void parallelBusSumsInputsThroughItsFxChain()
	{
		auto mixer = Engine::mixer();
		mixer->clear(); // each test builds its own graph from the master channel
		while (mixer->numChannels() < 4) { mixer->createChannel(); }

		const int bus = mixer->createBusChannel();
		QVERIFY2(mixer->isBusChannel(bus), "createBusChannel did not mark a bus");

		// Both sources reach the bus only - not the master directly.
		mixer->channelSendModel(1, 0)->setValue(0.0f);
		mixer->channelSendModel(2, 0)->setValue(0.0f);
		QVERIFY(mixer->createChannelSend(1, bus, 1.0f) != nullptr);
		QVERIFY(mixer->createChannelSend(2, bus, 1.0f) != nullptr);

		// Faders differ wildly and must not affect the pre-fader bus input.
		mixer->mixerChannel(1)->m_volumeModel.setValue(4.0f);
		mixer->mixerChannel(2)->m_volumeModel.setValue(0.1f);

		const float busGainL = 0.5f;
		const float busGainR = 0.25f;
		chainOf(mixer, bus)->appendEffect(
			new FixedGainEffect(chainOf(mixer, bus), busGainL, busGainR));

		const float inA = 0.5f;
		const float inB = 0.25f;
		PeriodHarness harness(mixer);
		feedDc(harness, 1, inA);
		feedDc(harness, 2, inB);
		const std::vector<SampleFrame> out = harness.render();

		const float expectL = (inA + inB) * busGainL;
		const float expectR = (inA + inB) * busGainR;
		QVERIFY2(nearValue(out[0][0], expectL),
			qPrintable(QString("bus L %1 != %2").arg(out[0][0]).arg(expectL)));
		QVERIFY2(nearValue(out[0][1], expectR),
			qPrintable(QString("bus R %1 != %2").arg(out[0][1]).arg(expectR)));

		evidence("PARALLEL_BUS bus=%d inA=%.4f inB=%.4f gainL=%.2f gainR=%.2f "
			"outL=%.6f(expect %.6f) outR=%.6f(expect %.6f)",
			bus, inA, inB, busGainL, busGainR,
			out[0][0], expectL, out[0][1], expectR);
	}

	//! 8.1 demo project: two channels through a shared bus with the native
	//! sidechain Compressor keyed from a third channel - no Peak Controller.
	//! The bus must be transparent without a key and duck hard with one.
	void demoProjectBusWithNativeSidechainCompressor()
	{
		auto mixer = Engine::mixer();
		mixer->clear(); // each test builds its own graph from the master channel
		while (mixer->numChannels() < 5) { mixer->createChannel(); }

		const int bus = mixer->createBusChannel();
		QVERIFY2(mixer->isBusChannel(bus), "createBusChannel did not mark a bus");

		// Channels: 1 = kick (key only), 2 = bass, 3 = pad.
		mixer->channelSendModel(1, 0)->setValue(0.0f); // key is not audible
		mixer->channelSendModel(2, 0)->setValue(0.0f); // bass -> bus only
		mixer->channelSendModel(3, 0)->setValue(0.0f); // pad  -> bus only
		QVERIFY(mixer->createChannelSend(2, bus, 1.0f) != nullptr);
		QVERIFY(mixer->createChannelSend(3, bus, 1.0f) != nullptr);

		// The sidechain send that keys the bus compressor.
		auto* sc = mixer->createSidechainSend(1, bus, 1.0f,
			SidechainTapPoint::PostFader);
		QVERIFY2(sc != nullptr, "could not create the kick -> bus sidechain send");

		// The native Compressor, loaded through the normal plugin factory.
		EffectChain* busChain = chainOf(mixer, bus);
		Effect* compressor = Effect::instantiate("compressor", busChain, nullptr);
		QVERIFY2(compressor != nullptr,
			"native Compressor plugin could not be instantiated");
		QVERIFY(compressor->descriptor() != nullptr);
		const QString effectName = QString::fromUtf8(compressor->descriptor()->name);
		QCOMPARE(effectName, QString("compressor"));
		QVERIFY2(!effectName.contains("peak", Qt::CaseInsensitive),
			"the demo bus must not use a Peak Controller");
		busChain->appendEffect(compressor);

		// Configure the compressor through its normal XML path: a hard-knee,
		// fast peak compressor with a low threshold.
		{
			QDomDocument doc;
			QDomElement effect = doc.createElement("effect");
			effect.setAttribute("on", "1");
			effect.setAttribute("wet", "1");
			QDomElement controls = doc.createElement("CompressorControls");
			controls.setAttribute("threshold", "-20");
			controls.setAttribute("ratio", "20");
			controls.setAttribute("attack", "1");
			controls.setAttribute("release", "50");
			controls.setAttribute("knee", "0");
			controls.setAttribute("peakmode", "1");
			controls.setAttribute("rms", "1");
			controls.setAttribute("autoMakeup", "0");
			controls.setAttribute("outGain", "0");
			effect.appendChild(controls);
			compressor->restoreState(effect);
		}

		const float bass = 0.02f;
		const float pad = 0.01f;
		const float kick = 0.5f;
		// The native Compressor applies a hard-coded 0.999 output gain
		// (plugins/Compressor/Compressor.cpp calcOutGain(): "0.999 is
		// needed to keep the values from crossing the threshold all the
		// time ... and is kept across all modes for consistency", upstream
		// 459948f8cd), so an otherwise-transparent pass measures
		// program * 0.999, not program.
		const float program = (bass + pad) * 0.999f;

		PeriodHarness harness(mixer);

		// Phase A: no kick. The program is below the threshold, so the
		// compressor must be transparent.
		double openLevel = 0.0;
		for (int p = 0; p < 8; ++p)
		{
			feedDc(harness, 1, 0.0f);
			feedDc(harness, 2, bass);
			feedDc(harness, 3, pad);
			const std::vector<SampleFrame> out = harness.render();
			if (p >= 4) { openLevel += meanAbs(out, 0, harness.fpp()); }
		}
		openLevel /= 4.0;

		// Phase B: kick on every period - the key drives the bus compressor.
		double duckedLevel = 0.0;
		for (int p = 0; p < 8; ++p)
		{
			feedDc(harness, 1, kick);
			feedDc(harness, 2, bass);
			feedDc(harness, 3, pad);
			const std::vector<SampleFrame> out = harness.render();
			if (p >= 4) { duckedLevel += meanAbs(out, 0, harness.fpp()); }
		}
		duckedLevel /= 4.0;

		const double ratio = duckedLevel / openLevel;

		QVERIFY2(nearValue(static_cast<float>(openLevel), program, 1.0e-5f),
			qPrintable(QString("bus is not transparent without a key: %1 != %2")
				.arg(openLevel).arg(program)));
		QVERIFY2(duckedLevel < 0.5 * openLevel,
			qPrintable(QString("bus did not duck: open %1 ducked %2")
				.arg(openLevel).arg(duckedLevel)));

		// The saved demo project must contain the bus and the sidechain send
		// and no Peak Controller anywhere.
		QDomDocument doc;
		QDomElement root = doc.createElement("root");
		mixer->saveSettings(doc, root);
		doc.appendChild(root);
		const QString xml = doc.toString();
		QVERIFY2(xml.contains("<bus"), "saved demo project has no <bus> element");
		QVERIFY2(xml.contains("<sidechain-send"),
			"saved demo project has no <sidechain-send> element");
		QVERIFY2(xml.contains("name=\"compressor\""),
			"saved demo project does not contain the compressor");
		QVERIFY2(!xml.contains("peakcontroller", Qt::CaseInsensitive),
			"saved demo project contains a Peak Controller");
		QCOMPARE(xml.count("<effect "), 1);

		evidence("DEMO_BUS bus=%d bass=%.4f pad=%.4f kick=%.4f program=%.4f "
			"open=%.6f ducked=%.6f ratio=%.4f drop_db=%.2f effect=%s "
			"peakcontroller_present=%d effects_in_project=%d",
			bus, bass, pad, kick, program, openLevel, duckedLevel, ratio,
			toDb(ratio), effectName.toUtf8().constData(),
			xml.contains("peakcontroller", Qt::CaseInsensitive) ? 1 : 0,
			static_cast<int>(xml.count("<effect ")));
	}

	//! A sidechain send that would close a scheduling cycle is refused.
	void cycleFormingSidechainSendIsRejected()
	{
		auto mixer = Engine::mixer();
		mixer->clear(); // each test builds its own graph from the master channel
		while (mixer->numChannels() < 3) { mixer->createChannel(); }

		MixerSidechainRoute* forward = mixer->createSidechainSend(1, 2, 1.0f);
		QVERIFY(forward != nullptr);
		MixerSidechainRoute* backward = mixer->createSidechainSend(2, 1, 1.0f);
		QVERIFY2(backward == nullptr, "cycle-forming sidechain send was accepted");
		QVERIFY2(mixer->channelSidechainSend(2, 1) == nullptr,
			"a cycle-forming sidechain route was left in the graph");

		evidence("CYCLE_GUARD forward_created=%d backward_created=%d route_left=%d",
			forward != nullptr ? 1 : 0, backward != nullptr ? 1 : 0,
			mixer->channelSidechainSend(2, 1) == nullptr ? 0 : 1);
	}

private:
	std::unique_ptr<QTemporaryDir> m_pluginDir;
};

QTEST_GUILESS_MAIN(PhaseDSidechainTest)
#include "PhaseDSidechainTest.moc"
