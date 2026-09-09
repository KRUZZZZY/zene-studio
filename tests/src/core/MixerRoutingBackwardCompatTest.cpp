/*
 * MixerRoutingBackwardCompatTest.cpp - Phase D (task #587) routing
 * serialization tests: legacy projects load with identical routing, and
 * Phase D projects round-trip through save/load.
 *
 * Copyright (c) 2026 LMMS developers
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
			lines << QString("  ch%1 send->%2 amount=%3 prefader=%4")
				.arg(i)
				.arg(r->receiverIndex())
				.arg(r->amount()->value())
				.arg(r->preFader() ? 1 : 0);
		}
		for (MixerSidechainRoute* r : ch->m_sidechainSends)
		{
			lines << QString("  ch%1 sc->%2 amount=%3 mode=%4")
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
};

QTEST_GUILESS_MAIN(MixerRoutingBackwardCompatTest)
#include "MixerRoutingBackwardCompatTest.moc"
