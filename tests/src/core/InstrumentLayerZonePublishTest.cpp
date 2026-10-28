/*
 * InstrumentLayerZonePublishTest.cpp - a layer insert, add or removal publishes
 * its zone renumbering and its layer slot as one edit (cluster new-p8-131-port),
 * and a track's MIDI parts stop at the 128-part limit (cluster new-p10-46).
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

#include <vector>

#include <QDomDocument>
#include <QDomElement>

#include "ControlInstrumentCommands.h"
#include "InstrumentMidiParts.h"
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

int partCount(const QString& track)
{
	return revtest::run(QStringLiteral("plugin.midi_parts"), {{QStringLiteral("target"), track}}).result.value(QStringLiteral("part_count")).toInt(-1);
}

//! @a count parts, each over the whole keyboard and velocity range, on the MIDI channels in turn. Written
//! through the model, so a test that uses it checks the command's own limit and nothing else.
std::vector<RackZone> manyParts(int count)
{
	std::vector<RackZone> parts;
	for (int i = 0; i < count; ++i)
	{
		RackZone zone;
		zone.chain = i % InstrumentMidiParts::Channels;
		parts.push_back(zone);
	}
	return parts;
}

//! How many undo steps the registry holds a transaction for. A refused command must not grow this.
int transactionCount()
{
	return revtest::run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray().size();
}

//! The track's state saved and read back into the same track - the project round trip of one track.
void saveAndReload(InstrumentTrack* track)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("trackcontainer"));
	doc.appendChild(root);
	const QDomElement saved = track->saveState(doc, root);
	track->restoreState(saved);
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

	//! MX-114: a track holds at most 128 MIDI parts (one per MIDI key; the audio thread scans them on every
	//! note-on). At that count plugin.midi_part_add is refused, typed, records no undo step and leaves the list
	//! alone; removing one part frees its slot for the next add.
	void midiPartsAddRefusedAtTheLimit()
	{
		QVERIFY2(!m_device.isEmpty(), "no loadable instrument: build the instrument plugins (e.g. the audiofileprocessor target)");
		constexpr int kLimit = 128;  // ControlCommandsPluginParts.cpp kMaxParts
		const QString fresh = revtest::addInstrumentTrack();
		InstrumentTrack* track = trackOf(fresh);
		QVERIFY(track != nullptr);
		track->midiParts().zones().assign(manyParts(kLimit));
		QCOMPARE(partCount(fresh), kLimit);

		const int transactionsBefore = transactionCount();
		const ControlResult refused = revtest::run(QStringLiteral("plugin.midi_part_add"), {{QStringLiteral("target"), fresh},
			{QStringLiteral("midi_channel"), 3}, {QStringLiteral("low_key"), 60}});
		QVERIFY2(!refused.ok, "a 129th part was accepted");
		QVERIFY(refused.errorKind == ControlErrorKind::Refused);
		QCOMPARE(partCount(fresh), kLimit);
		QCOMPARE(transactionCount(), transactionsBefore);  // a refused add records no undo step

		QVERIFY(revtest::run(QStringLiteral("plugin.midi_part_remove"), {{QStringLiteral("target"), fresh},
			{QStringLiteral("part"), QStringLiteral("part-127")}}).ok);
		QCOMPARE(partCount(fresh), kLimit - 1);
		QVERIFY2(revtest::run(QStringLiteral("plugin.midi_part_add"), {{QStringLiteral("target"), fresh},
			{QStringLiteral("midi_channel"), 3}, {QStringLiteral("low_key"), 60}}).ok, "a freed slot took no part");
		QCOMPARE(partCount(fresh), kLimit);
	}

	//! A saved list longer than the limit (an older project, written before the command had one) loads whole:
	//! the load drops nothing, and one more part is refused.
	void midiPartsOverLimitSavedListLoadsWhole()
	{
		QVERIFY2(!m_device.isEmpty(), "no loadable instrument: build the instrument plugins (e.g. the audiofileprocessor target)");
		constexpr int kSaved = 130;
		const QString fresh = revtest::addInstrumentTrack();
		InstrumentTrack* track = trackOf(fresh);
		QVERIFY(track != nullptr);
		track->midiParts().zones().assign(manyParts(kSaved));
		saveAndReload(track);
		QCOMPARE(partCount(fresh), kSaved);

		const ControlResult refused = revtest::run(QStringLiteral("plugin.midi_part_add"), {{QStringLiteral("target"), fresh},
			{QStringLiteral("midi_channel"), 3}, {QStringLiteral("low_key"), 60}});
		QVERIFY(!refused.ok);
		QVERIFY(refused.errorKind == ControlErrorKind::Refused);
		QCOMPARE(partCount(fresh), kSaved);
	}
};

QTEST_GUILESS_MAIN(InstrumentLayerZonePublishTest)
#include "InstrumentLayerZonePublishTest.moc"
