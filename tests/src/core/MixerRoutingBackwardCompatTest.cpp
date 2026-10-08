/*
 * MixerRoutingBackwardCompatTest.cpp - Phase D (task #587) routing
 * serialization tests: legacy projects load with identical routing, and
 * Phase D projects round-trip through save/load.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <QtTest>

#include "PhaseDMixerTestSupport.h"

#include "EffectChain.h"
#include "Mixer.h"
#include "VcaGroup.h"

#include <QDomDocument>
#include <QStringList>

#include <vector>

using namespace partd;

namespace
{

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

//! Serialize an effect chain so the test can inspect it without touching
//! EffectChain internals (m_effects is private; saveSettings is public).
void saveChainXml(EffectChain& chain, QDomDocument& doc, QDomElement& out)
{
	out = doc.createElement("fxchain");
	doc.appendChild(out);
	chain.saveSettings(doc, out);
}

int effectCountOf(EffectChain& chain)
{
	QDomDocument doc;
	QDomElement chainXml;
	saveChainXml(chain, doc, chainXml);
	return chainXml.elementsByTagName("effect").size();
}

} // namespace

// ---------------------------------------------------------------------------
// A textual, order-independent dump of the routing-relevant state of a mixer.
// Used to compare "the routing of a loaded legacy project" against "the
// routing built programmatically" and "the routing reloaded from a saved
// project" without relying on XML attribute ordering.
// ---------------------------------------------------------------------------
static QStringList routeTable(Mixer* mixer)
{
	QStringList lines;
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		MixerChannel* ch = mixer->mixerChannel(i);
		lines << QString("ch%1 name=%2 vol=%3 muted=%4 soloed=%5 bus=%6 fx=%7")
			.arg(i)
			.arg(ch->m_name)
			.arg(ch->m_volumeModel.value())
			.arg(ch->m_muteModel.value() ? 1 : 0)
			.arg(ch->m_soloModel.value() ? 1 : 0)
			.arg(ch->isBus() ? 1 : 0)
			.arg(effectCountOf(ch->m_fxChain));
		for (MixerRoute* r : ch->m_sends)
		{
			lines << QString("ch%1 send->%2 amount=%3 prefader=%4")
				.arg(i)
				.arg(r->receiverIndex())
				.arg(r->amount()->value())
				.arg(r->preFader() ? 1 : 0);
		}
		for (MixerSidechainRoute* r : ch->m_sidechainSends)
		{
			lines << QString("ch%1 sc->%2 amount=%3 mode=%4")
				.arg(i)
				.arg(r->receiverIndex())
				.arg(r->amount()->value())
				.arg(static_cast<int>(r->mode()));
		}
	}
	lines.sort();
	return lines;
}

static std::vector<SampleFrame> renderOnePeriod(Mixer* mixer, float in1, float in2)
{
	PeriodHarness harness(mixer);
	feedDc(harness, 1, in1);
	feedDc(harness, 2, in2);
	return harness.render();
}

// The routing table a Phase C-era LMMS project saves: <mixerchannel> with
// volume/muted/name attributes, an <fxchain> child and <send> children. No
// <bus>, no <sidechain-send>, no prefader attribute - this is the exact shape
// the D1 reference render was produced from.
static const char* kLegacyProjectXml =
	"<mixer>\n"
	"  <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n"
	"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
	"  </mixerchannel>\n"
	"  <mixerchannel num=\"1\" muted=\"0\" volume=\"1.5\" name=\"Kick\">\n"
	"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
	"    <send channel=\"0\" amount=\"0.75\"/>\n"
	"  </mixerchannel>\n"
	"  <mixerchannel num=\"2\" muted=\"0\" volume=\"0.5\" name=\"Bass\">\n"
	"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
	"    <send channel=\"0\" amount=\"1\"/>\n"
	"    <send channel=\"3\" amount=\"0.5\"/>\n"
	"  </mixerchannel>\n"
	"  <mixerchannel num=\"3\" muted=\"0\" volume=\"0.8\" name=\"Group\">\n"
	"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
	"    <send channel=\"0\" amount=\"0.25\"/>\n"
	"  </mixerchannel>\n"
	"</mixer>\n";

static bool parseMixerElement(const char* xml, QDomDocument& doc, QDomElement& out)
{
	QString error;
	int line = 0, column = 0;
	if (!doc.setContent(QString::fromUtf8(xml), &error, &line, &column))
	{
		qWarning("fixture is not valid XML at %d:%d: %s",
			line, column, qPrintable(error));
		return false;
	}
	out = doc.documentElement();
	return true;
}

class MixerRoutingBackwardCompatTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		initEngine();
		QVERIFY2(periodFrames() == 256, "frames per period is not 256");
	}

	void cleanupTestCase() { destroyEngine(); }

	// A Phase C-era project (no bus, no sidechain elements) must load with
	// byte-identical routing to a programmatically built equivalent graph,
	// and must render sample-identical audio.
	void legacyProjectLoadsWithIdenticalRouting()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);

		QDomDocument legacyDoc;
		QDomElement legacy;
		QVERIFY2(parseMixerElement(kLegacyProjectXml, legacyDoc, legacy),
			"the legacy fixture is not valid XML");
		mixer->loadSettings(legacy);

		QVERIFY2(mixer->numChannels() == 4, "legacy project did not allocate 4 channels");
		const QStringList loadedTable = routeTable(mixer);

		// The expected legacy routing, stated explicitly as evidence.
		QVERIFY2(loadedTable.contains("ch0 name=Master vol=1 muted=0 soloed=0 bus=0 fx=0"), qPrintable(loadedTable.join('\n')));
		QVERIFY2(loadedTable.contains("ch1 name=Kick vol=1.5 muted=0 soloed=0 bus=0 fx=0"), qPrintable(loadedTable.join('\n')));
		QVERIFY2(loadedTable.contains("ch1 send->0 amount=0.75 prefader=0"), qPrintable(loadedTable.join('\n')));
		QVERIFY2(loadedTable.contains("ch2 name=Bass vol=0.5 muted=0 soloed=0 bus=0 fx=0"), qPrintable(loadedTable.join('\n')));
		QVERIFY2(loadedTable.contains("ch2 send->0 amount=1 prefader=0"), qPrintable(loadedTable.join('\n')));
		QVERIFY2(loadedTable.contains("ch2 send->3 amount=0.5 prefader=0"), qPrintable(loadedTable.join('\n')));
		QVERIFY2(loadedTable.contains("ch3 name=Group vol=0.8 muted=0 soloed=0 bus=0 fx=0"), qPrintable(loadedTable.join('\n')));
		QVERIFY2(loadedTable.contains("ch3 send->0 amount=0.25 prefader=0"), qPrintable(loadedTable.join('\n')));
		// No Phase D state may appear in a legacy project.
		QVERIFY2(!loadedTable.filter("bus=1").size(), "a legacy channel was loaded as a bus");
		QVERIFY2(!loadedTable.filter("sc->").size(), "a legacy project gained a sidechain route");
		evidence("LEGACY_LOADED channels=%d routes=%d sidechain=%d",
			mixer->numChannels(), mixer->m_mixerRoutes.size(),
			mixer->mixerChannel(1)->m_sidechainSends.size() +
			mixer->mixerChannel(2)->m_sidechainSends.size() +
			mixer->mixerChannel(3)->m_sidechainSends.size());

		// Measured: render the loaded graph.
		//   ch1: 0.4 * 1.5 * 0.75            = 0.45
		//   ch2: 0.2 * 0.5 * 1.0             = 0.10
		//   ch2 -> ch3: 0.2 * 0.5 * 0.5      = 0.05
		//   ch3: 0.05 * 0.8 * 0.25           = 0.01
		//   master                            = 0.56
		const std::vector<SampleFrame> loadedOut = renderOnePeriod(mixer, 0.4f, 0.2f);
		const double expected = 0.45 + 0.10 + 0.01;
		QVERIFY2(std::abs(meanAbs(loadedOut, 0, periodFrames()) - expected) < 1e-6,
			qPrintable(QString("legacy render is %1, expected %2")
				.arg(meanAbs(loadedOut, 0, periodFrames())).arg(expected)));
		evidence("LEGACY_RENDER expected=%.6f measured=%.9f",
			expected, meanAbs(loadedOut, 0, periodFrames()));

		// Rebuild the same routing from scratch and compare both the routing
		// table and the rendered audio sample by sample.
		mixer->clear();
		while (mixer->numChannels() < 4) { mixer->createChannel(); }
		for (int ch = 1; ch <= 3; ++ch) { mixer->deleteChannelSend(ch, 0); }
		mixer->createChannelSend(1, 0, 0.75f);
		mixer->createChannelSend(2, 0, 1.0f);
		mixer->createChannelSend(2, 3, 0.5f);
		mixer->createChannelSend(3, 0, 0.25f);
		mixer->mixerChannel(1)->m_volumeModel.setValue(1.5f);
		mixer->mixerChannel(2)->m_volumeModel.setValue(0.5f);
		mixer->mixerChannel(3)->m_volumeModel.setValue(0.8f);
		mixer->mixerChannel(1)->m_name = "Kick";
		mixer->mixerChannel(2)->m_name = "Bass";
		mixer->mixerChannel(3)->m_name = "Group";

		const QStringList builtTable = routeTable(mixer);
		QCOMPARE(loadedTable, builtTable);
		const std::vector<SampleFrame> builtOut = renderOnePeriod(mixer, 0.4f, 0.2f);
		QCOMPARE(loadedOut.size(), builtOut.size());
		for (std::size_t i = 0; i < loadedOut.size(); ++i)
		{
			QVERIFY2(loadedOut[i][0] == builtOut[i][0] && loadedOut[i][1] == builtOut[i][1],
				qPrintable(QString("frame %1 differs: loaded (%2,%3) vs built (%4,%5)")
					.arg(i)
					.arg(loadedOut[i][0]).arg(loadedOut[i][1])
					.arg(builtOut[i][0]).arg(builtOut[i][1])));
		}
		evidence("LEGACY_EQUIVALENCE routes_identical=1 audio_identical=1 frames=%d",
			static_cast<int>(loadedOut.size()));

		// A legacy project must not grow Phase D elements when saved again.
		QDomDocument savedDoc;
		QDomElement savedRoot = savedDoc.createElement("root");
		savedDoc.appendChild(savedRoot);
		mixer->saveSettings(savedDoc, savedRoot);
		const QString savedXml = savedDoc.toString();
		QVERIFY2(!savedXml.contains("<bus"), "saveSettings emitted a <bus> element for a legacy project");
		QVERIFY2(!savedXml.contains("sidechain"), "saveSettings emitted sidechain elements for a legacy project");
		QVERIFY2(savedXml.contains("amount=\"0.75\""), qPrintable(savedXml));
		QVERIFY2(savedXml.contains("amount=\"0.25\""), qPrintable(savedXml));
		evidence("LEGACY_SAVE no_bus=1 no_sidechain=1 bytes=%d", savedXml.size());
	}

	// A Phase D project (bus + sidechain sends + pre-fader send) must survive
	// a save/load round-trip with identical routing and identical audio.
	void phaseDProjectRoundTripsThroughSaveLoad()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);

		mixer->clear();
		while (mixer->numChannels() < 4) { mixer->createChannel(); }
		const int bus = mixer->createBusChannel();
		mixer->mixerChannel(bus)->m_name = "DrumBus";
		mixer->mixerChannel(1)->m_name = "Kick";
		mixer->mixerChannel(2)->m_name = "Snare";

		mixer->deleteChannelSend(1, 0);
		mixer->deleteChannelSend(2, 0);
		mixer->createChannelSend(1, bus, 0.8f);
		mixer->createChannelSend(2, bus, 0.6f, true); // pre-fader
		mixer->createChannelSend(bus, 0, 1.0f);
		mixer->createSidechainSend(1, 2, 0.7f, SidechainTapPoint::PostFaderNoGain);
		mixer->createSidechainSend(bus, 2, 1.0f, SidechainTapPoint::PreFader);

		const QStringList before = routeTable(mixer);
		const mix_ch_t beforeChannels = mixer->numChannels();
		const std::vector<SampleFrame> beforeOut = renderOnePeriod(mixer, 0.4f, 0.2f);

		QDomDocument savedDoc;
		QDomElement savedRoot = savedDoc.createElement("root");
		savedDoc.appendChild(savedRoot);
		mixer->saveSettings(savedDoc, savedRoot);
		const QString savedXml = savedDoc.toString();
		QVERIFY2(savedXml.contains("<bus"), "saveSettings did not emit a <bus> element");
		QVERIFY2(savedXml.contains("sidechain-send"), "saveSettings did not emit <sidechain-send> elements");
		QVERIFY2(savedXml.contains("prefader=\"1\""), "saveSettings lost the pre-fader send flag");

		// Reload into the same mixer (loadSettings clears first).
		QDomDocument reloadDoc;
		QDomElement reloadRoot;
		QVERIFY2(parseMixerElement(savedXml.toUtf8().constData(), reloadDoc, reloadRoot),
			"the saved project is not valid XML");
		mixer->loadSettings(reloadRoot);

		QVERIFY2(mixer->numChannels() == beforeChannels,
			qPrintable(QString("channel count changed: %1 -> %2")
				.arg(beforeChannels).arg(mixer->numChannels())));
		const QStringList after = routeTable(mixer);
		QCOMPARE(after, before);
		QVERIFY2(mixer->mixerChannel(4)->isBus(), "the bus flag did not survive the round-trip");
		QVERIFY2(mixer->channelSidechainSend(1, 2) != nullptr, "sidechain send 1->2 did not survive");
		QVERIFY2(mixer->channelSidechainSend(bus, 2) != nullptr, "sidechain send bus->2 did not survive");
		QVERIFY2(mixer->channelSidechainSend(1, 2)->mode() == SidechainTapPoint::PostFaderNoGain,
			"sidechain tap mode did not survive the round-trip");

		const std::vector<SampleFrame> afterOut = renderOnePeriod(mixer, 0.4f, 0.2f);
		QCOMPARE(beforeOut.size(), afterOut.size());
		for (std::size_t i = 0; i < beforeOut.size(); ++i)
		{
			QVERIFY2(beforeOut[i][0] == afterOut[i][0] && beforeOut[i][1] == afterOut[i][1],
				qPrintable(QString("frame %1 differs after round-trip").arg(i)));
		}
		evidence("PHASED_ROUNDTRIP routes_identical=1 audio_identical=1 xml_bytes=%d", savedXml.size());
	}

	// Luna cluster 1512: createChannelSend() forces every send to a bus
	// pre-fader, so a post-fader bus send (set on the route directly) saved
	// without a prefader attribute and came back pre-fader.
	void aPostFaderSendToABusSurvivesSaveLoad()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		while (mixer->numChannels() < 3) { mixer->createChannel(); }
		const int bus = mixer->createBusChannel();
		mixer->mixerChannel(bus)->m_name = "DrumBus";

		mixer->createChannelSend(1, bus, 0.8f);
		MixerRoute* send = nullptr;
		for (MixerRoute* r : mixer->mixerChannel(1)->m_sends) { if (r->receiverIndex() == bus) { send = r; } }
		QVERIFY(send != nullptr);
		QVERIFY2(send->preFader(), "a new bus send is pre-fader by default (spec 5.5)");
		send->setPreFader(false);  // the user switches it to post-fader

		QDomDocument savedDoc;
		QDomElement savedRoot = savedDoc.createElement("root");
		savedDoc.appendChild(savedRoot);
		mixer->saveSettings(savedDoc, savedRoot);
		const QString savedXml = savedDoc.toString();

		QDomDocument reloadDoc;
		QDomElement reloadRoot;
		QVERIFY2(parseMixerElement(savedXml.toUtf8().constData(), reloadDoc, reloadRoot),
			"the saved project is not valid XML");
		mixer->loadSettings(reloadRoot);

		const int loadedBus = 3;
		QVERIFY2(mixer->mixerChannel(loadedBus)->isBus(), "the bus flag did not survive the round-trip");
		MixerRoute* loaded = nullptr;
		for (MixerRoute* r : mixer->mixerChannel(1)->m_sends) { if (r->receiverIndex() == loadedBus) { loaded = r; } }
		QVERIFY2(loaded != nullptr, "the post-fader bus send was dropped entirely");
		QVERIFY2(!loaded->preFader(), "a post-fader send to a bus reloaded as pre-fader (1512)");
		QCOMPARE(loaded->amount()->value(), 0.8f);
	}

	// A legacy project referencing a plugin this build does not have must
	// still load, keep the unknown effect's data verbatim and re-save it.
	void unknownLegacyEffectIsPreservedAsDummy()
	{
		static const char* xml =
			"<mixer>\n"
			"  <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel num=\"1\" muted=\"0\" volume=\"1\" name=\"Channel 1\">\n"
			"    <fxchain numofeffects=\"1\" enabled=\"1\">\n"
			"      <effect name=\"legacy-unknown-effect\" on=\"1\" wet=\"1\" autoquit=\"1\">\n"
			"        <key/>\n"
			"      </effect>\n"
			"    </fxchain>\n"
			"    <send channel=\"0\" amount=\"1\"/>\n"
			"  </mixerchannel>\n"
			"</mixer>\n";

		Mixer* mixer = Engine::mixer();
		QDomDocument doc;
		QDomElement root;
		QVERIFY2(parseMixerElement(xml, doc, root), "the fixture is not valid XML");
		mixer->loadSettings(root);

		EffectChain* chain = &mixer->mixerChannel(1)->m_fxChain;
		QDomDocument chainDoc;
		QDomElement chainXml;
		saveChainXml(*chain, chainDoc, chainXml);
		const QDomNodeList effects = chainXml.elementsByTagName("effect");
		QVERIFY2(effects.size() == 1, "the unknown effect was dropped");
		const QDomElement effect = effects.at(0).toElement();
		QCOMPARE(effect.attribute("name"), QString("legacy-unknown-effect"));
		QVERIFY2(effect.attribute("on") == "1" && effect.attribute("wet") == "1",
			"the unknown effect's control data was not preserved");

		QDomDocument savedDoc;
		QDomElement savedRoot = savedDoc.createElement("root");
		savedDoc.appendChild(savedRoot);
		mixer->saveSettings(savedDoc, savedRoot);
		QVERIFY2(savedDoc.toString().contains("legacy-unknown-effect"),
			"the unknown effect's original data was not re-saved");
		evidence("LEGACY_UNKNOWN_EFFECT preserved=1 resaved=1");
	}

	//! A cycle the feedback submode permits (A->B->A, the closing edge flagged)
	//! must not make a later, unrelated check recurse forever. The traversal
	//! has no visited set, so asking whether C->A closes a cycle walks
	//! A->B->A->B->A until the stack is gone.
	//!
	//! The answer itself is false: from C nothing reaches C, so no cycle is
	//! closed - the check must terminate and say so.
	void anExistingFeedbackCycleDoesNotRecurseForever()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);

		mixer->clear();
		QCOMPARE(mixer->setFeedbackMode(true), 0);
		while (mixer->numChannels() < 4) { mixer->createChannel(); }

		const mix_ch_t a = 1;
		const mix_ch_t b = 2;
		const mix_ch_t c = 3;
		QVERIFY2(mixer->createChannelSend(a, b, 1.0f) != nullptr, "A->B was not created");
		QVERIFY(mixer->isInfiniteLoop(b, a));
		QVERIFY2(mixer->createChannelSend(b, a, 1.0f) != nullptr,
			"the feedback submode did not permit the loop-closing send");
		// Confirm the cycle really exists, so the case cannot pass vacuously.
		QVERIFY(mixer->channelSendModel(b, a) != nullptr);

		const bool closesCycle = mixer->isInfiniteLoop(c, a);

		evidence("FEEDBACK_CYCLE_TERMINATES closes_cycle=%d", closesCycle ? 1 : 0);

		QVERIFY2(!closesCycle,
			"adding C->A was reported as a cycle although nothing routes back to C");
	}

	// A channel number that is not a channel must be rejected, not indexed.
	// The bus pre-pass read m_mixerChannels[busNum] and the main pass
	// read m_mixerChannels[num] without rejecting a negative `num`, and a
	// negative <sidechain-send channel> reached createSidechainSend, which
	// converts it to an unsigned index. A project carrying num="-1" (or a
	// send to channel="-1") therefore indexed outside the channel vector.
	void aNegativeChannelNumberIsRejectedNotIndexed()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);

		mixer->clear();
		while (mixer->numChannels() < 3) { mixer->createChannel(); }

		static const char* kNegativeXml =
			"<mixer>\n"
			"  <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel num=\"1\" muted=\"0\" volume=\"1\" name=\"Kick\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"    <send channel=\"-1\" amount=\"0.5\"/>\n"
			"    <sidechain-send channel=\"-1\" amount=\"0.5\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel num=\"-1\" muted=\"0\" volume=\"1\" name=\"Ghost\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"    <bus/>\n"
			"  </mixerchannel>\n"
			"</mixer>\n";

		QDomDocument doc;
		QDomElement root;
		QVERIFY2(parseMixerElement(kNegativeXml, doc, root), "the fixture is not valid XML");

		// The whole point: loading must not index outside m_mixerChannels.
		mixer->loadSettings(root);

		// A negative number is not a channel, so it must not have created one
		// and must not have routed anything to one. The fixture describes
		// channel 0 and channel 1, so master + Kick is the whole mixer.
		QCOMPARE(mixer->numChannels(), 2);
		// The only <send> in the fixture points at channel="-1", so channel 1
		// must have ended up with no send at all rather than one to a channel
		// that does not exist.
		QVERIFY2(mixer->mixerChannel(1)->m_sends.empty(),
			"a send to channel=\"-1\" was created");
		for (int i = 0; i < mixer->numChannels(); ++i)
		{
			QVERIFY2(!mixer->mixerChannel(i)->isBus(),
				qPrintable(QString("channel %1 became a bus from a num=\"-1\" element").arg(i)));
			MixerChannel* ch = mixer->mixerChannel(i);
			for (MixerSidechainRoute* r : ch->m_sidechainSends)
			{
				QVERIFY2(r->receiverIndex() >= 0,
					"a sidechain send survived with a negative receiver index");
			}
		}
	}

	// Luna cluster 4277: a deferred sidechain route (one that closes a cycle
	// through a regular send) is non-gating by construction. Deferred status is
	// inferred from the graph at creation time, but the saved <sidechain-send>
	// element carried no deferred flag, so loadSettings re-inferred it from a
	// HALF-BUILT graph: channel 1's sidechain is created while loading channel
	// 1, before the regular send of channel 2 that makes it deferred exists.
	// It came back gating, and the regular send loaded afterwards closed a
	// scheduling cycle in which neither channel is ever queued.
	void aDeferredSidechainRouteSurvivesSaveLoadAsDeferred()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);

		mixer->clear();
		while (mixer->numChannels() < 3) { mixer->createChannel(); }

		// 2 sends to 1, so a 1->2 sidechain closes a cycle through a regular
		// send and must be created DEFERRED (non-gating).
		QVERIFY2(mixer->createChannelSend(2, 1, 1.0f) != nullptr, "2->1 was not created");
		QVERIFY2(mixer->createSidechainSend(1, 2, 0.7f, SidechainTapPoint::PreFader) != nullptr,
			"1->2 sidechain was refused");
		MixerSidechainRoute* before = mixer->channelSidechainSend(1, 2);
		QVERIFY(before != nullptr);
		QVERIFY2(before->deferred(),
			"1->2 was not created deferred; this case would pass vacuously");

		QDomDocument savedDoc;
		QDomElement savedRoot = savedDoc.createElement("root");
		savedDoc.appendChild(savedRoot);
		mixer->saveSettings(savedDoc, savedRoot);
		const QString savedXml = savedDoc.toString();

		QDomDocument reloadDoc;
		QDomElement reloadRoot;
		QVERIFY2(parseMixerElement(savedXml.toUtf8().constData(), reloadDoc, reloadRoot),
			"the saved project is not valid XML");
		mixer->loadSettings(reloadRoot);

		MixerSidechainRoute* after = mixer->channelSidechainSend(1, 2);
		QVERIFY2(after != nullptr, "the sidechain send 1->2 did not survive the round trip");
		QVERIFY2(after->deferred(),
			"a saved deferred sidechain reloaded as a gating route (4277)");
		QCOMPARE(static_cast<int>(after->mode()), static_cast<int>(SidechainTapPoint::PreFader));
		evidence("DEFERRED_ROUNDTRIP deferred_before=1 deferred_after=1 xml_bytes=%d", savedXml.size());
	}

	// Luna cluster 3295: a <member> whose `channel` attribute is missing,
	// negative or absurdly large was still handed to allocateChannelsTo() and
	// cast to mix_ch_t. A large positive number allocated that many channels
	// (a malformed file could allocate the whole mixer); a negative one wrapped
	// to 65535 and joined the group as a member that names no channel.
	void aMalformedVcaMemberChannelIsSkipped()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);
		mixer->clear();

		static const char* kBadMembersXml =
			"<mixer>\n"
			"  <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel num=\"1\" muted=\"0\" volume=\"1\" name=\"Kick\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <vcagroup id=\"0\" name=\"Bad\">\n"
			"    <member channel=\"500\"/>\n"
			"    <member channel=\"-1\"/>\n"
			"    <member/>\n"
			"    <member channel=\"1\"/>\n"
			"  </vcagroup>\n"
			"</mixer>\n";

		QDomDocument doc;
		QDomElement root;
		QVERIFY2(parseMixerElement(kBadMembersXml, doc, root), "the fixture is not valid XML");
		mixer->loadSettings(root);

		QVERIFY2(mixer->numChannels() < 100,
			qPrintable(QString("a member channel=\"500\" allocated %1 channels (3295)")
				.arg(mixer->numChannels())));

		QVERIFY2(mixer->vcaGroups().size() == 1, "the group was dropped entirely");
		const std::vector<mix_ch_t>& members = mixer->vcaGroups().front()->members();
		QVERIFY2(members.size() == 1,
			qPrintable(QString("a malformed member joined the group: %1 members")
				.arg(members.size())));
		QCOMPARE(static_cast<int>(members.front()), 1);
		evidence("MALFORMED_MEMBER channels=%d members=%d",
			mixer->numChannels(), static_cast<int>(members.size()));
	}

	// Soloing walks the send graph with unmuteForSolo()/
	// unmuteSenderForSolo()/unmuteReceiverForSolo(). Those recurse over
	// m_sends/m_receives with no visited-channel guard, and the
	// cycle-permitted submode puts a real A->B->A cycle in m_sends, so
	// soloing either channel walked the pair until the stack was gone.
	void soloingAChannelInAFeedbackCycleTerminates()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);

		mixer->clear();
		mixer->setFeedbackMode(true);
		while (mixer->numChannels() < 4) { mixer->createChannel(); }

		const mix_ch_t a = 1;
		const mix_ch_t b = 2;
		QVERIFY(mixer->createChannelSend(a, b, 1.0f) != nullptr);
		QVERIFY(mixer->isInfiniteLoop(b, a));
		QVERIFY2(mixer->createChannelSend(b, a, 1.0f) != nullptr,
			"the feedback submode did not permit the loop-closing send");
		// Confirm the cycle is really in the graph, so this cannot pass vacuously.
		QVERIFY(mixer->channelSendModel(a, b) != nullptr);
		QVERIFY(mixer->channelSendModel(b, a) != nullptr);

		// The whole point: this must return instead of recursing forever.
		mixer->mixerChannel(a)->m_soloModel.setValue(true);

		QVERIFY(mixer->mixerChannel(a)->m_muteModel.value() == false);
		mixer->mixerChannel(a)->m_soloModel.setValue(false);
	}

	// Solo propagation must reach every channel that is audible through the
	// send graph, whichever direction each step walks. Here S receives from
	// A, A receives from B and D, B sends to D, and E sends to D. Walking B's
	// send to D in sender mode must not hide D from the receiver walk that
	// reaches it from A, or E (D's receive) stays muted.
	void aSoloReachesEveryUpstreamChannelThroughAMixedWalk()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);

		mixer->clear();
		while (mixer->numChannels() < 6) { mixer->createChannel(); }
		for (mix_ch_t ch = 1; ch <= 5; ++ch) { mixer->deleteChannelSend(ch, 0); }

		const mix_ch_t s = 1;
		const mix_ch_t a = 2;
		const mix_ch_t b = 3;
		const mix_ch_t d = 4;
		const mix_ch_t e = 5;
		QVERIFY(mixer->createChannelSend(a, s, 1.0f) != nullptr);
		QVERIFY(mixer->createChannelSend(b, a, 1.0f) != nullptr);
		QVERIFY(mixer->createChannelSend(d, a, 1.0f) != nullptr);
		QVERIFY(mixer->createChannelSend(b, d, 1.0f) != nullptr);
		QVERIFY(mixer->createChannelSend(e, d, 1.0f) != nullptr);

		mixer->mixerChannel(s)->m_soloModel.setValue(true);

		for (mix_ch_t ch : {s, a, b, d, e})
		{
			QVERIFY2(!mixer->mixerChannel(ch)->m_muteModel.value(),
				qPrintable(QString("channel %1 stayed muted under a solo of %2").arg(ch).arg(s)));
		}
		mixer->mixerChannel(s)->m_soloModel.setValue(false);
	}

	// The master (num="0") is a real channel element. Rejecting a non-positive
	// num must not skip it: a saved master with nondefault state has to come
	// back with that state. Only a negative number is refused.
	void aNonDefaultMasterSurvivesReload()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);
		mixer->clear();

		static const char* kMasterXml =
			"<mixer>\n"
			"  <mixerchannel num=\"0\" muted=\"1\" volume=\"0.5\" pan=\"-0.5\" name=\"Mix\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel num=\"-1\" muted=\"0\" volume=\"1\" name=\"Ghost\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"</mixer>\n";

		QDomDocument doc;
		QDomElement root;
		QVERIFY2(parseMixerElement(kMasterXml, doc, root), "the fixture is not valid XML");
		mixer->loadSettings(root);

		QCOMPARE(mixer->numChannels(), 1);
		MixerChannel* master = mixer->mixerChannel(0);
		QVERIFY(master != nullptr);
		QCOMPARE(master->m_name, QString("Mix"));
		QVERIFY2(qAbs(master->m_volumeModel.value() - 0.5f) < 1e-4f,
			qPrintable(QString("master volume %1, expected 0.5").arg(master->m_volumeModel.value())));
		QVERIFY2(qAbs(master->m_panModel.value() + 0.5f) < 1e-4f,
			qPrintable(QString("master pan %1, expected -0.5").arg(master->m_panModel.value())));
		QVERIFY2(master->m_muteModel.value(), "master mute did not survive the load");
	}
	// A <mixerchannel> whose num is missing or not a number parses to 0 with
	// toInt(), which is the master. Such an element names no channel, so it is
	// skipped: it must not overwrite the master's saved state.
	void aMalformedChannelNumberDoesNotOverwriteTheMaster()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);
		mixer->clear();

		static const char* kMalformedXml =
			"<mixer>\n"
			"  <mixerchannel num=\"0\" muted=\"1\" volume=\"0.5\" pan=\"-0.5\" name=\"Mix\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel muted=\"0\" volume=\"0.1\" name=\"NoNumber\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel num=\"abc\" muted=\"0\" volume=\"0.2\" name=\"WordNumber\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"  </mixerchannel>\n"
			"  <mixerchannel num=\"\" muted=\"0\" volume=\"0.3\" name=\"EmptyNumber\">\n"
			"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
			"    <bus/>\n"
			"  </mixerchannel>\n"
			"</mixer>\n";

		QDomDocument doc;
		QDomElement root;
		QVERIFY2(parseMixerElement(kMalformedXml, doc, root), "the fixture is not valid XML");
		mixer->loadSettings(root);

		QCOMPARE(mixer->numChannels(), 1);
		MixerChannel* master = mixer->mixerChannel(0);
		QVERIFY(master != nullptr);
		QCOMPARE(master->m_name, QString("Mix"));
		QVERIFY2(qAbs(master->m_volumeModel.value() - 0.5f) < 1e-4f,
			qPrintable(QString("master volume %1, expected 0.5").arg(master->m_volumeModel.value())));
		QVERIFY2(master->m_muteModel.value(), "a malformed num overwrote the master's mute");
		QVERIFY2(!master->isBus(), "a malformed num with a bus marker made the master a bus");
	}

};

QTEST_GUILESS_MAIN(MixerRoutingBackwardCompatTest)
#include "MixerRoutingBackwardCompatTest.moc"
