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
 * And the load of a track's settings - the layers, the MIDI parts and the
 * output-bus routes - publishes each change under the audio engine's
 * model-change guard, the boundary a whole render period holds (cluster
 * new-p14-124).
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

#include <vector>

#include <QDomDocument>
#include <QDomElement>

#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "ControlInstrumentCommands.h"
#include "Engine.h"
#include "InstrumentLayers.h"
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

	//! Cluster new-p14-124: InstrumentTrack::loadTrackSpecificSettings() loads the layers, the MIDI parts
	//! and the output-bus routes AFTER it releases the track's own processing lock, and the playback paths
	//! read them. That lock is not the boundary for this state - the render pass may take it with tryLock() -
	//! so the question this slot answers is whether the loaders publish under the guard that IS the boundary:
	//! the audio engine's model-change guard, the mutex a whole render period holds. Both loaders are driven
	//! directly, with no caller guard of their own, and each zone publication is asked whether the guard was
	//! held.
	void settingsLoadPublishesUnderTheModelChangeGuard()
	{
		if (m_device.isEmpty()) { QSKIP("this build exposes no loadable instrument"); }
		const QString fresh = revtest::addInstrumentTrack();
		InstrumentTrack* track = trackOf(fresh);
		QVERIFY(track != nullptr);
		const ControlResult added = revtest::run(QStringLiteral("rack.layer_add"),
			{{QStringLiteral("target"), fresh}, {QStringLiteral("device"), m_device}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		track->midiParts().zones().assign(manyParts(2));

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("trackcontainer"));
		doc.appendChild(root);
		const QDomElement saved = track->saveState(doc, root);
		// The track-specific settings live in their own element inside it (Track::saveTrack), which is the
		// element loadTrackSpecificSettings() reads - the same one the project load passes down.
		const QDomElement settings = saved.firstChildElement(QStringLiteral("instrumenttrack"));
		QVERIFY2(!settings.isNull(), "the track saved no settings element");
		QVERIFY2(!settings.firstChildElement(QStringLiteral("layers")).isNull(), "the track saved no layers to load");
		QVERIFY2(!settings.firstChildElement(QStringLiteral("midiparts")).isNull(), "the track saved no parts to load");

		AudioEngine* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		QVERIFY2(!engine->isChangeInModelHeld(), "a guard was held before the load - the probe would be blind");

		// The loaders on their own: no outer guard, exactly what the call site after unlock() has.
		int layerPublishes = 0;
		int layerUnguarded = 0;
		int partPublishes = 0;
		int partUnguarded = 0;
		track->layers().zones().setChangedHook([&]
		{
			++layerPublishes;
			if (!engine->isChangeInModelHeld()) { ++layerUnguarded; }
		});
		track->midiParts().zones().setChangedHook([&]
		{
			++partPublishes;
			if (!engine->isChangeInModelHeld()) { ++partUnguarded; }
		});
		track->layers().loadSettings(settings);
		track->midiParts().loadSettings(settings);
		track->layers().zones().setChangedHook({});
		track->midiParts().zones().setChangedHook({});
		QVERIFY2(layerPublishes > 0, "the layer load published nothing - this slot would be vacuous");
		QVERIFY2(partPublishes > 0, "the MIDI-part load published nothing - this slot would be vacuous");
		QCOMPARE(layerUnguarded, 0);
		QCOMPARE(partUnguarded, 0);
		QVERIFY2(!engine->isChangeInModelHeld(), "the loader left the model-change guard taken");

		// And the path a project open takes: restoreState -> loadTrack -> loadTrackSpecificSettings, whose
		// unlock() precedes the same load. The guard is held there too, so the mutation after it is inside
		// the audio boundary.
		int reloadPublishes = 0;
		int reloadUnguarded = 0;
		track->layers().zones().setChangedHook([&]
		{
			++reloadPublishes;
			if (!engine->isChangeInModelHeld()) { ++reloadUnguarded; }
		});
		track->restoreState(saved);
		track->layers().zones().setChangedHook({});
		QVERIFY2(reloadPublishes > 0, "the track-settings load published nothing");
		QCOMPARE(reloadUnguarded, 0);
		QVERIFY2(track->layers().layerCount() == 1, "the reload did not restore the layer");
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

	//! Cluster new-p19-4: a layer mix update is published as the layer bus's gain - one
	//! std::atomic<float> the render thread loads once a period - not as shared model state the
	//! mixer would read. Layer-0 is the track's own instrument, so no loadable instrument is
	//! needed: rack.layer_set mutes it and the bus gain (what a sounding note is scaled by) goes to
	//! 0; the undo publishes the old gain back, and a reload publishes the loaded mix.
	void aLayerMixIsPublishedAsItsBusGain()
	{
		const QString fresh = revtest::addTrack();
		QVERIFY(!fresh.isEmpty());
		InstrumentTrack* track = trackOf(fresh);
		QVERIFY(track != nullptr);
		AudioBusHandle* bus = track->audioBusHandle();
		QCOMPARE(track->layers().layerCount(), 0);  // layer-0 only: the track's own instrument
		QCOMPARE(bus->gain(), 1.0f);

		const ControlResult muted = revtest::run(QStringLiteral("rack.layer_set"),
			{{QStringLiteral("target"), fresh}, {QStringLiteral("layer"), QStringLiteral("layer-0")},
			 {QStringLiteral("muted"), true}});
		QVERIFY2(muted.ok, qPrintable(muted.errorMessage));
		QVERIFY(track->layers().mix(0).muted);
		QCOMPARE(bus->gain(), 0.0f);  // the value the audio thread reads once a period

		REV_UNDO_OR_FAIL();
		QVERIFY(!track->layers().mix(0).muted);
		QCOMPARE(bus->gain(), 1.0f);

		// Solo takes the same path: layer-0 is the only layer, so it stays at unity, and a save and
		// reload re-publishes the loaded mix instead of leaving the bus with an unset gain.
		const ControlResult soloed = revtest::run(QStringLiteral("rack.layer_set"),
			{{QStringLiteral("target"), fresh}, {QStringLiteral("layer"), QStringLiteral("layer-0")},
			 {QStringLiteral("solo"), true}});
		QVERIFY2(soloed.ok, qPrintable(soloed.errorMessage));
		QCOMPARE(bus->gain(), 1.0f);
		saveAndReload(track);
		QVERIFY(track->layers().mix(0).solo);
		QCOMPARE(bus->gain(), 1.0f);

		// Leave the shared fixture without this slot's undo state or its fresh track.
		REV_UNDO_OR_FAIL();
		QVERIFY(!track->layers().mix(0).solo);
		QCOMPARE(bus->gain(), 1.0f);
		REV_UNDO_OR_FAIL();
		QVERIFY(!revtest::hasTrack(fresh));
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
