/*
 * InstrumentLayersCommandsTest.cpp - 0.5.0 F2: the control surface of MX-112
 * (rack.layer*), MX-114 (plugin.midi_part*, plugin.output_*) and MX-120
 * (modulator.voice_set, modulator.instrument_target_set), with their undo
 * steps and the layer rack's and parts' save/load round trip.
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ControlInstrumentCommands.h"
#include "InstrumentTrack.h"
#include "ModulationLayer.h"
#include "Song.h"
#include "RackTestSupport.h"
#include "ReversibilityTestSupport.h"

using namespace racktest;
using revtest::run;

namespace
{

InstrumentTrack* trackOf(const QString& id)
{
	ControlResult ignored;
	return control::resolveInstrumentTarget(id, &ignored);
}

int layerCount(const QString& track)
{
	return run(QStringLiteral("rack.layers"), {{QStringLiteral("target"), track}}).result.value(QStringLiteral("layer_count")).toInt(-1);
}

int layerZoneCount(const QString& track)
{
	return run(QStringLiteral("rack.layers"), {{QStringLiteral("target"), track}}).result.value(QStringLiteral("zones")).toArray().size();
}

int partCount(const QString& track)
{
	return run(QStringLiteral("plugin.midi_parts"), {{QStringLiteral("target"), track}}).result.value(QStringLiteral("part_count")).toInt(-1);
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

class InstrumentLayersCommandsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's instruments, so the layers have one to load
#endif
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
		m_track = revtest::addInstrumentTrack();
		m_device = revtest::firstLoadable(QStringLiteral("instrument"));
	}

	void cleanupTestCase() { teardownRackFixture(); }

	//! MX-112: a layer is added with a whole-keyboard zone, zones gate the track's own instrument, a removal
	//! takes the layer's zones with it, control.undo brings both back, and the rack survives save/load.
	void layersAddZoneRemoveUndoReload()
	{
		if (m_track.isEmpty() || m_device.isEmpty()) { QSKIP("this build exposes no loadable instrument"); }
		const ControlResult added = run(QStringLiteral("rack.layer_add"),
			{{QStringLiteral("target"), m_track}, {QStringLiteral("device"), m_device}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(added.result.value(QStringLiteral("layer")).toString(), QStringLiteral("layer-1"));
		QCOMPARE(layerCount(m_track), 1);
		QCOMPARE(layerZoneCount(m_track), 1);
		InstrumentTrack* track = trackOf(m_track);
		QVERIFY(track != nullptr);
		QVERIFY(track->layers().basePlays(80, 100));  // no zone of its own: the track's instrument plays everything

		QVERIFY(run(QStringLiteral("rack.layer_zone_add"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("layer"), QStringLiteral("layer-0")}, {QStringLiteral("low_key"), 0},
			{QStringLiteral("high_key"), 59}}).ok);
		QVERIFY(!track->layers().basePlays(80, 100));
		QVERIFY(track->layers().basePlays(40, 100));
		QVERIFY(!run(QStringLiteral("rack.layer_zone_add"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("layer"), QStringLiteral("layer-5")}}).ok);  // no such layer: refused, typed

		const ControlResult parameters = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("plugin"), QStringLiteral("layer-1")}, {QStringLiteral("index"), 0}});
		QVERIFY2(parameters.ok || parameters.errorKind == ControlErrorKind::NotFound, qPrintable(parameters.errorMessage));

		saveAndReload(track);
		QCOMPARE(layerCount(m_track), 1);
		QCOMPARE(layerZoneCount(m_track), 2);

		QVERIFY(run(QStringLiteral("rack.layer_remove"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("layer"), QStringLiteral("layer-1")}}).ok);
		QCOMPARE(layerCount(m_track), 0);
		QCOMPARE(layerZoneCount(m_track), 1);  // layer-1's zone went with it; layer-0's stays
		REV_UNDO_OR_FAIL();
		QCOMPARE(layerCount(m_track), 1);
		QCOMPARE(layerZoneCount(m_track), 2);
		QVERIFY(!run(QStringLiteral("rack.layer_remove"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("layer"), QStringLiteral("layer-0")}}).ok);  // the track's own instrument stays
	}

	//! MX-114: a MIDI part is added and removed through the surface, undone, and saved with the track.
	void midiPartsAddRemoveUndoReload()
	{
		if (m_track.isEmpty()) { QSKIP("this build exposes no loadable instrument"); }
		const ControlResult added = run(QStringLiteral("plugin.midi_part_add"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("midi_channel"), 3}, {QStringLiteral("low_key"), 60}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(added.result.value(QStringLiteral("part")).toString(), QStringLiteral("part-0"));
		QCOMPARE(added.result.value(QStringLiteral("parts")).toArray().at(0).toObject()
			.value(QStringLiteral("midi_channel")).toInt(), 3);
		QCOMPARE(trackOf(m_track)->midiParts().zones().zone(0)->chain, 2);  // channel 3 is index 2

		saveAndReload(trackOf(m_track));
		QCOMPARE(partCount(m_track), 1);

		QVERIFY(run(QStringLiteral("plugin.midi_part_remove"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("part"), QStringLiteral("part-0")}}).ok);
		QCOMPARE(partCount(m_track), 0);
		REV_UNDO_OR_FAIL();
		QCOMPARE(partCount(m_track), 1);
		QVERIFY(!run(QStringLiteral("plugin.midi_part_remove"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("part"), QStringLiteral("part-9")}}).ok);
	}

	//! MX-114: a single-output instrument has no buses to route - refused, typed; a hosted one lists them.
	void outputBusesAnswerTyped()
	{
		if (m_track.isEmpty()) { QSKIP("this build exposes no loadable instrument"); }
		const ControlResult buses = run(QStringLiteral("plugin.output_buses"), {{QStringLiteral("target"), m_track}});
		QVERIFY(buses.ok || buses.errorKind == ControlErrorKind::Refused);
		if (buses.ok) { QVERIFY(buses.result.value(QStringLiteral("bus_count")).toInt() >= 1); }
		QVERIFY(!run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("bus"), 0}, {QStringLiteral("channel"), QString()}}).ok);
	}

	//! MX-120: a per-voice modulator needs a track to follow; bound to the track's instrument it reports
	//! which voice mode drives each target, and the bind is one undo step.
	void perVoiceModulatorReportsItsMode()
	{
		if (m_track.isEmpty()) { QSKIP("this build exposes no loadable instrument"); }
		const ControlResult created = run(QStringLiteral("modulator.create"), {{QStringLiteral("name"), QStringLiteral("voice")},
			{QStringLiteral("rate"), 2.0}, {QStringLiteral("shape"), QStringLiteral("sine")}});
		QVERIFY2(created.ok, qPrintable(created.errorMessage));
		const QString modulator = created.result.value(QStringLiteral("modulator")).toString();
		QVERIFY(!run(QStringLiteral("modulator.voice_set"), {{QStringLiteral("modulator"), modulator},
			{QStringLiteral("per_voice"), true}}).ok);  // no trigger track: refused
		QVERIFY(run(QStringLiteral("modulator.voice_set"), {{QStringLiteral("modulator"), modulator},
			{QStringLiteral("per_voice"), true}, {QStringLiteral("trigger"), m_track}}).ok);

		const ControlResult parameter = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), m_track},
			{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("index"), 0}});
		if (!parameter.ok) { QSKIP("the instrument exposes no parameter to bind"); }
		const QString name = parameter.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("name")).toString();
		const ControlResult bound = run(QStringLiteral("modulator.instrument_target_set"), {{QStringLiteral("modulator"), modulator},
			{QStringLiteral("target"), m_track}, {QStringLiteral("parameter"), name}, {QStringLiteral("depth"), 0.25}});
		QVERIFY2(bound.ok, qPrintable(bound.errorMessage));
		const QJsonObject state = run(QStringLiteral("modulator.get_state")).result;
		const QJsonObject first = state.value(QStringLiteral("modulators")).toArray().last().toObject();
		QVERIFY(first.value(QStringLiteral("source")).toObject().value(QStringLiteral("per_voice")).toBool());
		const QJsonObject target = first.value(QStringLiteral("targets")).toArray().at(0).toObject();
		const QString mode = target.value(QStringLiteral("voice_mode")).toString();
		QVERIFY2(mode == QStringLiteral("per_note") || mode == QStringLiteral("mono_newest_note"), qPrintable(mode));
		QCOMPARE(mode == QStringLiteral("per_note"), bound.result.value(QStringLiteral("per_note")).toBool());
		QVERIFY(!target.value(QStringLiteral("voice_note")).toString().isEmpty());
		REV_UNDO_OR_FAIL();
		const ModulationLayer& layer = Engine::getSong()->modulationLayer().layer();
		QCOMPARE(layer.modulator(layer.modulatorCount() - 1)->routeCount(), 0);
	}

private:
	QString m_track;
	QString m_device;
};

QTEST_GUILESS_MAIN(InstrumentLayersCommandsTest)
#include "InstrumentLayersCommandsTest.moc"
