/*
 * InstrumentLayerZonePublishTest.cpp - a layer insert, add or removal publishes
 * its zone renumbering and its layer slot as one edit (cluster new-p8-131-port).
 *
 * The zone list and the layer slots are read by the audio workers. Published as
 * two edits, a worker could see the zones already renumbered while the layer
 * slots still hold the old tracks. Kept apart from InstrumentLayersCommandsTest,
 * which is at the file-length ratchet.
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
#include <QDomNodeList>

#include <cmath>
#include <limits>

#include "ControlInstrumentCommands.h"
#include "InstrumentLayers.h"
#include "InstrumentTrack.h"
#include "NotePlayHandle.h"
#include "RackTestSupport.h"
#include "RackZones.h"
#include "ReversibilityTestSupport.h"

using namespace racktest;

namespace
{

InstrumentTrack* trackOf(const QString& id)
{
	ControlResult ignored;
	return control::resolveInstrumentTarget(id, &ignored);
}

//! The layer zones as one published state: @a count layers, one zone that routes to @a chain, and @a expected at that chain.
bool layerZoneMappingIs(InstrumentTrack* track, int count, int chain, InstrumentTrack* expected)
{
	const RackZones& zones = track->layers().zones();
	const RackZone* zone = zones.zone(0);
	return track->layers().layerCount() == count && zones.zoneCount() == 1 && zone != nullptr &&
		zone->chain == chain && track->layers().layer(chain) == expected;
}

} // namespace

class InstrumentLayerZonePublishTest : public QObject
{
	Q_OBJECT

	QString m_device;

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's instruments, so the layers have one to load
#endif
		QString why;
		QVERIFY2(racktest::initRackFixture(&why), qPrintable(why));
		NotePlayHandleManager::init();
		m_device = revtest::firstLoadable(QStringLiteral("instrument"));
	}

	void cleanupTestCase() { racktest::teardownRackFixture(); }

	//! Cluster 2466: an instrument fixture that cannot finish must leave no
	//! track in the shared engine. The two failure paths are driven explicitly
	//! so the test does not depend on what this build ships, and they live in
	//! SEPARATE slots: one failing QVERIFY used to return from the slot and
	//! leave the other path without its own evidence.
	//!
	//! Failure path 1 - no loadable device at all: nothing to load, so no
	//! track may be created.
	void noDeviceInstrumentSetupLeavesNoTrack()
	{
		const int before = revtest::trackCount();
		QVERIFY(revtest::addInstrumentTrackFor(QString()).isEmpty());
		QCOMPARE(revtest::trackCount(), before);
	}

	//! Failure path 2 - a device id plugin.load refuses: the track was created
	//! and the load failed, so the track must be taken back.
	void refusedDeviceLoadLeavesNoTrack()
	{
		const int before = revtest::trackCount();
		QVERIFY(revtest::addInstrumentTrackFor(QStringLiteral("dev-999999")).isEmpty());
		QCOMPARE(revtest::trackCount(), before);
	}

	//! A layer insert or removal publishes its zone renumbering and its layer slot as one edit: a change
	//! observed between the two would route a zone to a layer slot that does not hold its track yet.
	void layerZoneEditsPublishWithTheirSlots()
	{
		if (m_device.isEmpty()) { QSKIP("this build exposes no loadable instrument"); }
		const QString fresh = revtest::addInstrumentTrack();
		InstrumentTrack* track = trackOf(fresh);
		QVERIFY(track != nullptr);
		const ControlResult added = revtest::run(QStringLiteral("rack.layer_add"),
			{{QStringLiteral("target"), fresh}, {QStringLiteral("device"), m_device}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(track->layers().layerCount(), 1);
		QCOMPARE(track->layers().zones().zoneCount(), 1);
		InstrumentTrack* existing = track->layers().layer(1);
		QVERIFY(existing != nullptr);

		bool insertPublishedTogether = false;
		track->layers().zones().setChangedHook([&]
		{
			insertPublishedTogether = layerZoneMappingIs(track, 2, 2, existing);
		});
		const int inserted = track->layers().insertLayer(1, track->layers().layerXml(1));
		track->layers().zones().setChangedHook({});
		QCOMPARE(inserted, 1);
		QVERIFY(insertPublishedTogether);

		bool removePublishedTogether = false;
		track->layers().zones().setChangedHook([&]
		{
			removePublishedTogether = layerZoneMappingIs(track, 1, 1, existing);
		});
		const bool removedLayer = track->layers().removeLayer(1);
		track->layers().zones().setChangedHook({});
		QVERIFY(removedLayer);
		QVERIFY(removePublishedTogether);
	}

	//! A layer's level is a finite gain (cluster new-p22-60). NaN passes every < and > range check, so a NaN
	//! level used to reach the layer's gain: rack.layer_set refuses it typed, and InstrumentLayers::setMix
	//! refuses it for any other caller. The layer's mix stays what it was; a finite level still lands.
	void layerLevelRefusesNonFinite()
	{
		if (m_device.isEmpty()) { QSKIP("this build exposes no loadable instrument"); }
		const QString fresh = revtest::addInstrumentTrack();
		InstrumentTrack* track = trackOf(fresh);
		QVERIFY(track != nullptr);
		const ControlResult added = revtest::run(QStringLiteral("rack.layer_add"),
			{{QStringLiteral("target"), fresh}, {QStringLiteral("device"), m_device}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const int index = track->layers().layerCount();  // the layer added last
		const QString layer = added.result.value(QStringLiteral("layer")).toString();
		const float before = track->layers().mix(index).level;
		const double nan = std::numeric_limits<double>::quiet_NaN();

		const ControlResult refused = revtest::run(QStringLiteral("rack.layer_set"),
			{{QStringLiteral("target"), fresh}, {QStringLiteral("layer"), layer}, {QStringLiteral("level"), nan}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(track->layers().mix(index).level, before);

		InstrumentLayers::Mix nanMix = track->layers().mix(index);
		nanMix.level = static_cast<float>(nan);
		QVERIFY(!track->layers().setMix(index, nanMix));
		QCOMPARE(track->layers().mix(index).level, before);

		QVERIFY(revtest::run(QStringLiteral("rack.layer_set"),
			{{QStringLiteral("target"), fresh}, {QStringLiteral("layer"), layer}, {QStringLiteral("level"), 0.5}}).ok);
		QCOMPARE(track->layers().mix(index).level, 0.5f);
	}

	//! A saved layer level that is not a number loads as unity, not as a NaN gain (readMix clamped it, and
	//! std::clamp passes NaN through). A finite saved level still loads as saved. The track goes through its
	//! own save and restore, the path a project load takes.
	void savedLayerLevelNotANumberLoadsAsUnity()
	{
		if (m_device.isEmpty()) { QSKIP("this build exposes no loadable instrument"); }
		const QString fresh = revtest::addInstrumentTrack();
		InstrumentTrack* track = trackOf(fresh);
		QVERIFY(track != nullptr);
		const ControlResult added = revtest::run(QStringLiteral("rack.layer_add"),
			{{QStringLiteral("target"), fresh}, {QStringLiteral("device"), m_device}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const int index = track->layers().layerCount();  // the layer added last

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("trackcontainer"));
		doc.appendChild(root);
		const QDomElement saved = track->saveState(doc, root);
		const QDomNodeList entries = saved.elementsByTagName(QStringLiteral("layer"));
		QVERIFY(!entries.isEmpty());
		QDomElement entry = entries.at(entries.size() - 1).toElement();  // the layer added last

		entry.setAttribute(QStringLiteral("layerlevel"), QStringLiteral("0.5"));
		track->restoreState(saved);
		QCOMPARE(track->layers().mix(index).level, 0.5f);

		entry.setAttribute(QStringLiteral("layerlevel"), QStringLiteral("nan"));
		track->restoreState(saved);
		QVERIFY(std::isfinite(track->layers().mix(index).level));
		QCOMPARE(track->layers().mix(index).level, 1.0f);
	}
};

QTEST_GUILESS_MAIN(InstrumentLayerZonePublishTest)
#include "InstrumentLayerZonePublishTest.moc"
