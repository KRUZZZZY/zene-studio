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

#include "ControlInstrumentCommands.h"
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
	//! track in the shared engine. Both failure paths are driven explicitly so
	//! the test does not depend on what this build ships: no device at all, and
	//! a device id plugin.load refuses.
	void failedInstrumentSetupLeavesNoTrack()
	{
		const int before = revtest::trackCount();
		QVERIFY(revtest::addInstrumentTrackFor(QString()).isEmpty());
		QCOMPARE(revtest::trackCount(), before);
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
};

QTEST_GUILESS_MAIN(InstrumentLayerZonePublishTest)
#include "InstrumentLayerZonePublishTest.moc"
