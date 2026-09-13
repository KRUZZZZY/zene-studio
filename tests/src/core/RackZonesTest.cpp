/*
 * RackZonesTest.cpp - the KEY/VELOCITY ZONE half of the rack.* command group
 *                     (SPEC A11-A16), and the persistence of the whole rack
 *                     configuration.
 *
 * A zone is an inclusive key range (0..127), an inclusive velocity range
 * (0..200, the engine's own note-velocity range) and the chain they map to,
 * with an optional sample reference - one object, because a zone that states
 * only keys is a zone whose velocity range is the whole space. The lookup rule
 * is first-match-wins in the order the zones were added.
 *
 * HONEST SCOPE, asserted here rather than only written down: nothing in this
 * build consults a zone while a note plays. The rack renders one stereo block
 * and has no per-note input, so what ships is the persisted, validated,
 * queryable zone model, its resolver and its surface; rack.zone_resolve answers
 * which zone a note falls into and routes nothing. docs/KNOWN-LIMITATIONS.md and
 * the release notes carry the same sentence in one line each.
 *
 * The macro half is RackMacrosTest.cpp; the fixture both share is
 * RackTestSupport.h.
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
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "Engine.h"
#include "Mixer.h"
#include "Rack.h"
#include "RackZones.h"
#include "RackTestSupport.h"
#include "ReversibilityTestSupport.h"

using namespace racktest;

class RackZonesTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
		QCOMPARE(rackUnderTest().zones().zoneCount(), 0);
	}

	void cleanupTestCase() { teardownRackFixture(); }

	//! The resolver: inclusive ranges on both axes, first match wins where two
	//! zones overlap, -1 where nothing matches, and the four refusal shapes.
	void zonesResolveKeyAndVelocityRanges()
	{
		RackZones& zones = rackUnderTest().zones();

		RackZone low;
		low.lowKey = 36;
		low.highKey = 48;
		low.lowVelocity = 0;
		low.highVelocity = 200;
		low.chain = kDrivenChain;
		low.sample = QStringLiteral("bass.wav");
		QVERIFY(isValidRackZone(low, rackUnderTest().chainCount()));
		QCOMPARE(zones.addZone(low), 0);

		RackZone high;
		high.lowKey = 36;
		high.highKey = 96;
		high.lowVelocity = 100;
		high.highVelocity = 200;
		high.chain = 0;
		QVERIFY(isValidRackZone(high, rackUnderTest().chainCount()));
		QCOMPARE(zones.addZone(high), 1);

		// Inclusive bounds on both axes.
		QCOMPARE(zones.resolve(36, 0), 0);
		QCOMPARE(zones.resolve(48, 200), 0);
		QCOMPARE(zones.resolve(49, 0), -1);
		QCOMPARE(zones.resolve(35, 100), -1);
		// FIRST MATCH WINS: (40, 150) is inside BOTH zones and zone 0 was added
		// first; the second zone is reached only where the first does not apply.
		QCOMPARE(zones.resolve(40, 150), 0);
		QCOMPARE(zones.resolve(60, 150), 1);
		printEvidence("zone-resolve",
			QStringLiteral("zones=2 first-match: (40,150)->0 (both match); (60,150)->1; (49,0)->-1"));

		// The bounds are the engine's own: a key outside 0..127, a velocity
		// outside 0..200, a backwards range or a chain that is not there are not
		// zones, and rack.zone_add refuses them rather than storing them.
		RackZone badKey = low;
		badKey.lowKey = 200;
		QVERIFY(!isValidRackZone(badKey, rackUnderTest().chainCount()));
		RackZone backwards = low;
		backwards.lowVelocity = 180;
		backwards.highVelocity = 20;
		QVERIFY(!isValidRackZone(backwards, rackUnderTest().chainCount()));
		RackZone noChain = low;
		noChain.chain = 9;
		QVERIFY(!isValidRackZone(noChain, rackUnderTest().chainCount()));
	}

	//! The zone surface: add through the registry, resolve, remove, and take the
	//! removal back with control.undo - the zone returns at its own index, which
	//! is the order the first-match rule reads.
	void theZoneSurfaceAddsResolvesAndUndoes()
	{
		const QString channel = revtest::channelId(kChannel);

		const ControlResult added = revtest::run(QStringLiteral("rack.zone_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("low_key"), 24},
				{QStringLiteral("high_key"), 35}, {QStringLiteral("chain"), kDrivenChain},
				{QStringLiteral("sample"), QStringLiteral("sub.wav")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString zone = added.result.value(QStringLiteral("zone")).toString();
		QCOMPARE(added.result.value(QStringLiteral("zone_state"))
			.toObject().value(QStringLiteral("sample")).toString(), QStringLiteral("sub.wav"));

		const ControlResult matched = revtest::run(QStringLiteral("rack.zone_resolve"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("key"), 30},
				{QStringLiteral("velocity"), 120}});
		QVERIFY2(matched.ok, qPrintable(matched.errorMessage));
		QVERIFY(matched.result.value(QStringLiteral("matched")).toBool());
		QCOMPARE(matched.result.value(QStringLiteral("zone")).toString(), zone);
		QCOMPARE(matched.result.value(QStringLiteral("zone_state"))
			.toObject().value(QStringLiteral("chain")).toInt(), kDrivenChain);
		// A note outside every zone is an honest miss, not a default chain.
		QCOMPARE(revtest::run(QStringLiteral("rack.zone_resolve"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("key"), 5},
				{QStringLiteral("velocity"), 120}})
			.result.value(QStringLiteral("matched")).toBool(), false);

		// Typed refusals, before the removal.
		const ControlResult badChain = revtest::run(QStringLiteral("rack.zone_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("chain"), 9}});
		QVERIFY(!badChain.ok);
		QCOMPARE(badChain.errorKind, ControlErrorKind::InvalidArgs);
		// The schema bounds the key range, so a key of 200 never reaches the
		// handler.
		const ControlResult badKey = revtest::run(QStringLiteral("rack.zone_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("chain"), 0},
				{QStringLiteral("low_key"), 200}});
		QVERIFY(!badKey.ok);
		QCOMPARE(badKey.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult badZone = revtest::run(QStringLiteral("rack.zone_remove"),
			{{QStringLiteral("channel"), channel},
				{QStringLiteral("zone"), QStringLiteral("zone-99")}});
		QVERIFY(!badZone.ok);
		QCOMPARE(badZone.errorKind, ControlErrorKind::NotFound);
		printEvidence("zone-refusals",
			QStringLiteral("bad chain/key -> invalid_args; unknown zone -> not_found; a note "
				"outside every zone -> matched=false"));

		QCOMPARE(revtest::run(QStringLiteral("rack.get_state"),
			QJsonObject{{QStringLiteral("channel"), channel}})
			.result.value(QStringLiteral("zone_count")).toInt(), 3);

		const ControlResult removed = revtest::run(QStringLiteral("rack.zone_remove"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("zone"), zone}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(revtest::stateOf(QStringLiteral("rack.zone_remove"))
			.value(QStringLiteral("class")).toString(), QStringLiteral("true_inverse"));
		REV_UNDO_OR_FAIL();
		QCOMPARE(revtest::run(QStringLiteral("rack.zone_resolve"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("key"), 30},
				{QStringLiteral("velocity"), 120}})
			.result.value(QStringLiteral("zone")).toString(), zone);
		printEvidence("surface-zone-undo",
			QStringLiteral("zone_remove -> control.undo -> the zone is back at its index"));
	}

	//! The whole configuration - the chains, the selector, the macros with their
	//! targets and the zones - is saved as children of the channel's EXISTING
	//! <rack> element and comes back from a reload, and a reloaded macro whose
	//! device is not in the fixture writes nothing rather than dereferencing
	//! anything.
	void theConfigurationPersistsUnderTheRackElement()
	{
		Rack& rack = rackUnderTest();
		const int macrosBefore = rack.macros().macroCount();
		const int zonesBefore = rack.zones().zoneCount();
		rack.setSelectedChain(kDrivenChain);

		const int macro = rack.macros().addMacro(QStringLiteral("Persisted"), 0.5f);
		QCOMPARE(rack.macros().addTarget(macro, targetOf(kGainName, 0.25f, 0.75f)), 0);
		RackZone first;
		first.lowKey = 0;
		first.highKey = 47;
		first.chain = kDrivenChain;
		first.sample = QStringLiteral("bass.wav");
		QCOMPARE(rack.zones().addZone(first), zonesBefore);
		RackZone second;
		second.lowKey = 48;
		second.highKey = 127;
		second.chain = 0;
		QCOMPARE(rack.zones().addZone(second), zonesBefore + 1);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(root);
		Engine::mixer()->saveSettings(doc, root);

		const QDomElement rackElement = savedRackElement(root);
		QVERIFY2(!rackElement.isNull(), "the rack was not saved");
		// Version 2: macros and zones are children of this element, so a
		// version-1 reader ignores them and this one reads them.
		QCOMPARE(rackElement.attribute(QStringLiteral("version")), QStringLiteral("2"));
		QCOMPARE(rackElement.attribute(QStringLiteral("selected")), QStringLiteral("1"));
		const QDomNodeList macros = rackElement.elementsByTagName(QStringLiteral("macro"));
		QCOMPARE(macros.size(), macrosBefore + 1);
		const QDomElement savedMacro = macros.at(macrosBefore).toElement();
		QCOMPARE(savedMacro.attribute(QStringLiteral("name")), QStringLiteral("Persisted"));
		QCOMPARE(savedMacro.attribute(QStringLiteral("value")), QStringLiteral("0.5"));
		const QDomNodeList targets = savedMacro.elementsByTagName(QStringLiteral("target"));
		QCOMPARE(targets.size(), 1);
		QCOMPARE(targets.at(0).toElement().attribute(QStringLiteral("parameter")),
			QString::fromLatin1(kGainName));
		QCOMPARE(targets.at(0).toElement().attribute(QStringLiteral("low")), QStringLiteral("0.25"));
		const QDomNodeList zones = rackElement.elementsByTagName(QStringLiteral("zone"));
		QCOMPARE(zones.size(), zonesBefore + 2);
		QCOMPARE(zones.at(zonesBefore).toElement().attribute(QStringLiteral("sample")),
			QStringLiteral("bass.wav"));
		// The second zone names no sample: the attribute is simply absent, and
		// the zone is still a key/velocity zone.
		QVERIFY(zones.at(zonesBefore + 1).toElement().attribute(QStringLiteral("sample")).isEmpty());
		printEvidence("persistence-saved",
			QStringLiteral("rack version=2 selected=1 macro+1 target+1 zone+2, all children of "
				"<rack>"));

		// Reload the mixer from XML. The fixture's chains carry no effects on
		// purpose - instantiating one from XML starts the plugin loader, the
		// teardown race docs/RACKS.md section 6 records - so the macro's target
		// is asserted as DATA here, and as a live write by RackMacrosTest.
		QDomDocument loadDoc;
		loadDoc.setContent(QString::fromUtf8(reloadFixtureXml()));
		Engine::mixer()->loadSettings(loadDoc.documentElement());

		Rack& reloaded = rackUnderTest();
		QCOMPARE(reloaded.chainCount(), 2);
		QCOMPARE(reloaded.selectedChain(), 1);
		QCOMPARE(reloaded.macros().macroCount(), 1);
		QCOMPARE(reloaded.macros().macro(0)->name, QStringLiteral("Loaded"));
		QCOMPARE(reloaded.macros().macro(0)->value, 0.25f);
		QCOMPARE(static_cast<int>(reloaded.macros().macro(0)->targets.size()), 1);
		QCOMPARE(reloaded.macros().macro(0)->targets[0].parameter, QString::fromLatin1(kGainName));
		QCOMPARE(reloaded.macros().macro(0)->targets[0].low, 0.5f);
		QCOMPARE(reloaded.macros().macro(0)->targets[0].high, 1.0f);
		QCOMPARE(reloaded.zones().zoneCount(), 2);
		QCOMPARE(reloaded.zones().resolve(40, 150), 0);
		QCOMPARE(reloaded.zones().resolve(55, 150), 1);
		QCOMPARE(reloaded.zones().resolve(55, 20), -1);
		QCOMPARE(reloaded.zones().zone(0)->sample, QStringLiteral("kick.wav"));

		// The reloaded macro's target names a device the fixture does not build,
		// so applying it writes nothing and says so.
		QCOMPARE(static_cast<int>(reloaded.macros().apply(0, reloaded).size()), 0);
		printEvidence("persistence-reloaded",
			QStringLiteral("macro=1 value=0.25 target=1 zone=2 resolve(40,150)=0 resolve(55,20)=-1 "
				"apply_writes=0 (the fixture's chain has no devices)"));

		// Save again: the reloaded configuration writes the same children.
		QDomDocument again;
		QDomElement rootAgain = again.createElement(QStringLiteral("mixer"));
		again.appendChild(rootAgain);
		Engine::mixer()->saveSettings(again, rootAgain);
		const QDomElement rackAgain = savedRackElement(rootAgain);
		QVERIFY(!rackAgain.isNull());
		QCOMPARE(rackAgain.attribute(QStringLiteral("version")), QStringLiteral("2"));
		QCOMPARE(rackAgain.elementsByTagName(QStringLiteral("macro")).size(), 1);
		QCOMPARE(rackAgain.elementsByTagName(QStringLiteral("target")).size(), 1);
		QCOMPARE(rackAgain.elementsByTagName(QStringLiteral("zone")).size(), 2);
		QCOMPARE(rackAgain.elementsByTagName(QStringLiteral("chain")).size(), 1);
		printEvidence("persistence-round-trip",
			QStringLiteral("saved=macro1/target1/zone2 loaded=same re-saved=same"));
	}
};

QTEST_GUILESS_MAIN(RackZonesTest)
#include "RackZonesTest.moc"
