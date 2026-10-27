/*
 * DrumRackPadsTest.cpp - Q050-066 stage 1: a drum rack's sample pads, created, edited and read back
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
 *
 */
/*! Q050-066 stage 1 through the registry: track.drum_rack_get_state reads every pad of a rack (its name,
 *  sample, trigger key, chain and bus) the way the tracks hold them; track.drum_rack_pad_add puts a pad in
 *  a rack and track.drum_rack_pad_set edits one, each ONE undo step whose redo keeps the ids; the state reads
 *  back the same after a save and reopen. The interface half is DrumRackPanelTest.cpp, the socket and
 *  render half tests/control-drum-rack-pads.py. */

#include <QtTest>

#include <QJsonArray>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "Song.h"
#include "TrackFolder.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString sample(const char* file)
{
	return QStringLiteral(ZENE_DRUM_SAMPLES_DIR "/") + QLatin1String(file);
}

QJsonObject rackState(const QString& folder)
{
	return run(QStringLiteral("track.drum_rack_get_state"), {{QStringLiteral("track"), folder}}).result;
}

QJsonArray padsOf(const QString& folder)
{
	return rackState(folder).value(QStringLiteral("pads")).toArray();
}

//! A two-pad rack from track.drum_rack: its folder id.
QString makeRack(QJsonObject* made = nullptr)
{
	const ControlResult result = run(QStringLiteral("track.drum_rack"), {{QStringLiteral("name"), QStringLiteral("Kit")},
		{QStringLiteral("pads"), QJsonArray{QJsonObject{{QStringLiteral("sample"), sample("bassdrum01.ogg")}, {QStringLiteral("name"), QStringLiteral("Kick")}},
			QJsonObject{{QStringLiteral("sample"), sample("snare01.ogg")}, {QStringLiteral("key"), 38}}}}});
	if (made != nullptr) { *made = result.result; }
	return result.result.value(QStringLiteral("folder")).toString();
}

InstrumentTrack* trackOf(const QJsonValue& id)
{
	const int number = id.toString().section(QLatin1Char('-'), 1).toInt();
	return dynamic_cast<InstrumentTrack*>(Engine::getSong()->findTrackById(number));
}

} // namespace

class DrumRackPadsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A rack built by track.drum_rack reads back pad by pad: name, sample, trigger key, the pad's own channel
	//! sending to the rack's bus, and the sampler's playback settings.
	void aRackReadsBackItsPads()
	{
		QJsonObject made;
		const QString folder = makeRack(&made);
		QVERIFY(!folder.isEmpty());
		const QJsonObject state = rackState(folder);
		QCOMPARE(state.value(QStringLiteral("folder")).toString(), folder);
		QCOMPARE(state.value(QStringLiteral("name")).toString(), QStringLiteral("Kit"));
		QCOMPARE(state.value(QStringLiteral("bus")).toString(), made.value(QStringLiteral("bus")).toString());
		const QJsonArray pads = state.value(QStringLiteral("pads")).toArray();
		QCOMPARE(pads.size(), 2);
		const QJsonObject kick = pads.at(0).toObject();
		QCOMPARE(kick.value(QStringLiteral("name")).toString(), QStringLiteral("Kick"));
		QCOMPARE(kick.value(QStringLiteral("key")).toInt(), 36);
		QCOMPARE(kick.value(QStringLiteral("sample")).toString(), sample("bassdrum01.ogg"));
		QCOMPARE(kick.value(QStringLiteral("track")).toString(), made.value(QStringLiteral("tracks")).toArray().at(0).toString());
		QCOMPARE(kick.value(QStringLiteral("channel")).toString(), made.value(QStringLiteral("channels")).toArray().at(1).toString());
		QCOMPARE(kick.value(QStringLiteral("sends_to")).toString(), made.value(QStringLiteral("bus")).toString());
		const QJsonObject playback = kick.value(QStringLiteral("playback")).toObject();
		QCOMPARE(playback.value(QStringLiteral("amp")).toDouble(), 100.0);
		QCOMPARE(playback.value(QStringLiteral("end")).toDouble(), 1.0);
		QCOMPARE(playback.value(QStringLiteral("reversed")).toBool(), false);
		const QJsonObject snare = pads.at(1).toObject();
		QCOMPARE(snare.value(QStringLiteral("name")).toString(), QStringLiteral("snare01"));
		QCOMPARE(snare.value(QStringLiteral("key")).toInt(), 38);
		QCOMPARE(trackOf(snare.value(QStringLiteral("track")))->baseNoteModel()->value(), 38);  // the key IS the base note

		QCOMPARE(run(QStringLiteral("track.drum_rack_get_state"), {{QStringLiteral("track"), kick.value(QStringLiteral("track"))}}).errorKind,
			ControlErrorKind::InvalidArgs);  // a pad is not a rack
		QCOMPARE(run(QStringLiteral("track.drum_rack_get_state"), {{QStringLiteral("track"), QStringLiteral("trk-99999")}}).errorKind,
			ControlErrorKind::NotFound);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

	//! A pad added to a rack is a sampler track in its folder with its own channel sending to the rack's bus; the
	//! answer is what get_state reads. One undo takes the pad and its channel away; the redo brings it back under
	//! the same ids.
	void aPadIsAddedAsOneUndoStep()
	{
		const QString folder = makeRack();
		const QString bus = rackState(folder).value(QStringLiteral("bus")).toString();
		const int tracks = static_cast<int>(Engine::getSong()->tracks().size());
		const int channels = Engine::mixer()->numChannels();
		const ControlResult added = run(QStringLiteral("track.drum_rack_pad_add"), {{QStringLiteral("track"), folder},
			{QStringLiteral("sample"), sample("hihat_closed01.ogg")}, {QStringLiteral("name"), QStringLiteral("Hat")},
			{QStringLiteral("key"), 42}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(added.result.value(QStringLiteral("name")).toString(), QStringLiteral("Hat"));
		QCOMPARE(added.result.value(QStringLiteral("key")).toInt(), 42);
		QCOMPARE(added.result.value(QStringLiteral("sends_to")).toString(), bus);
		QJsonArray pads = padsOf(folder);
		QCOMPARE(pads.size(), 3);
		QCOMPARE(pads.at(2).toObject(), added.result);  // the answer is the state
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks + 1);
		QCOMPARE(Engine::mixer()->numChannels(), channels + 1);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(padsOf(folder).size(), 2);
		QCOMPARE(Engine::mixer()->numChannels(), channels);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		pads = padsOf(folder);
		QCOMPARE(pads.size(), 3);
		QCOMPARE(pads.at(2).toObject(), added.result);  // the same track and channel ids, the same state

		// Without a key the pad takes the next one after the rack's pads (36 + 3).
		const ControlResult next = run(QStringLiteral("track.drum_rack_pad_add"), {{QStringLiteral("track"), folder},
			{QStringLiteral("sample"), sample("clap01.ogg")}});
		QVERIFY2(next.ok, qPrintable(next.errorMessage));
		QCOMPARE(next.result.value(QStringLiteral("key")).toInt(), 39);
		QCOMPARE(next.result.value(QStringLiteral("name")).toString(), QStringLiteral("clap01"));

		const auto add = [](const QJsonObject& args) { return run(QStringLiteral("track.drum_rack_pad_add"), args).errorKind; };
		QCOMPARE(add({{QStringLiteral("track"), folder}, {QStringLiteral("sample"), QStringLiteral("/no/such.wav")}}), ControlErrorKind::NotFound);
		QCOMPARE(add({{QStringLiteral("track"), folder}, {QStringLiteral("sample"), sample("clap01.ogg")}, {QStringLiteral("key"), 128}}),
			ControlErrorKind::InvalidArgs);
		QCOMPARE(add({{QStringLiteral("track"), pads.at(0).toObject().value(QStringLiteral("track"))},
			{QStringLiteral("sample"), sample("clap01.ogg")}}), ControlErrorKind::InvalidArgs);  // a pad is not a rack
		QCOMPARE(padsOf(folder).size(), 4);  // nothing refused was added
	}

	//! A pad's name, trigger key and sample are edited together as ONE undo step; redo puts the edit back.
	void aPadIsEditedAsOneUndoStep()
	{
		const QString folder = makeRack();
		const QJsonObject before = padsOf(folder).at(1).toObject();
		const QJsonValue pad = before.value(QStringLiteral("track"));
		const ControlResult set = run(QStringLiteral("track.drum_rack_pad_set"), {{QStringLiteral("track"), pad},
			{QStringLiteral("name"), QStringLiteral("Rim")}, {QStringLiteral("key"), 37}, {QStringLiteral("sample"), sample("snare02.ogg")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		const QJsonObject after = padsOf(folder).at(1).toObject();
		QCOMPARE(after, set.result);
		QCOMPARE(after.value(QStringLiteral("name")).toString(), QStringLiteral("Rim"));
		QCOMPARE(after.value(QStringLiteral("key")).toInt(), 37);
		QCOMPARE(after.value(QStringLiteral("sample")).toString(), sample("snare02.ogg"));
		QCOMPARE(after.value(QStringLiteral("channel")), before.value(QStringLiteral("channel")));  // the chain stays

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // ONE step: all three back
		QCOMPARE(padsOf(folder).at(1).toObject(), before);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(padsOf(folder).at(1).toObject(), after);

		const auto edit = [](const QJsonObject& args) { return run(QStringLiteral("track.drum_rack_pad_set"), args).errorKind; };
		QCOMPARE(edit({{QStringLiteral("track"), pad}}), ControlErrorKind::InvalidArgs);  // nothing to change
		QCOMPARE(edit({{QStringLiteral("track"), pad}, {QStringLiteral("name"), QString()}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(edit({{QStringLiteral("track"), pad}, {QStringLiteral("key"), -1}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(edit({{QStringLiteral("track"), pad}, {QStringLiteral("sample"), QStringLiteral("/no/such.wav")}}), ControlErrorKind::NotFound);
		QCOMPARE(edit({{QStringLiteral("track"), folder}, {QStringLiteral("name"), QStringLiteral("x")}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(padsOf(folder).at(1).toObject(), after);  // a refusal writes nothing
	}

	//! The rack's own undo and redo keep its ids, so a pad added to it redoes into the same folder.
	void aPadAddedToARedoneRackLandsInIt()
	{
		QJsonObject made;
		const QString folder = makeRack(&made);
		const ControlResult added = run(QStringLiteral("track.drum_rack_pad_add"), {{QStringLiteral("track"), folder},
			{QStringLiteral("sample"), sample("clap01.ogg")}});
		QVERIFY(added.ok);
		const QJsonObject state = rackState(folder);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the pad
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the rack
		QVERIFY(!run(QStringLiteral("track.drum_rack_get_state"), {{QStringLiteral("track"), folder}}).ok);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(rackState(folder), state);  // the same folder, pads, channels and bus
	}

	//! The rack's state is what the project saves: after a save, an edit the file does not hold and a reopen, the
	//! state read back is the saved one, field for field.
	void thePadStateSurvivesASaveAndReopen()
	{
		const QString folder = makeRack();
		QVERIFY(run(QStringLiteral("track.drum_rack_pad_add"), {{QStringLiteral("track"), folder},
			{QStringLiteral("sample"), sample("hihat_closed01.ogg")}, {QStringLiteral("name"), QStringLiteral("Hat")}, {QStringLiteral("key"), 42}}).ok);
		const QJsonValue snare = padsOf(folder).at(1).toObject().value(QStringLiteral("track"));
		QVERIFY(run(QStringLiteral("track.drum_rack_pad_set"), {{QStringLiteral("track"), snare}, {QStringLiteral("name"), QStringLiteral("Rim")},
			{QStringLiteral("key"), 40}}).ok);
		const QJsonObject saved = rackState(folder);
		const QString path = m_dir.filePath(QStringLiteral("pads.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path, false));
		QVERIFY(run(QStringLiteral("track.drum_rack_pad_set"), {{QStringLiteral("track"), snare}, {QStringLiteral("key"), 50}}).ok);
		Engine::getSong()->loadProject(path);
		QCOMPARE(rackState(folder), saved);
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(DrumRackPadsTest)
#include "DrumRackPadsTest.moc"
