/*
 * ImportTrackIoTest.cpp - imported and pasted tracks keep their mixer channel and hardware I/O
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
/*! 0.5.0 L09 (Q050-027): project.import_tracks and project.paste_tracks keep a track's mixer channel and the
 *  channel's named hardware routing (direct out, hardware insert send and return). A channel this project
 *  already has under that name is reused as it is; one it lacks is created from the source's description; one
 *  undo step removes the tracks and the channels made for them, and its redo makes them again. */

#include <QtTest>

#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>

#include "AudioIoSetup.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

const QString DrumsBus = QStringLiteral("Drums Bus");
const QString Keys = QStringLiteral("Keys");

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

bool ok(const QString& id, const QJsonObject& args = {})
{
	const ControlResult result = run(id, args);
	if (!result.ok) { qWarning("%s: %s", qPrintable(id), qPrintable(result.errorMessage)); }
	return result.ok;
}

//! mixer.get_state's entry for the channel called @a name (empty when there is none).
QJsonObject stateOf(const QString& name)
{
	for (const QJsonValue& channel : run(QStringLiteral("mixer.get_state")).result.value(QStringLiteral("channels")).toArray())
	{
		if (channel.toObject().value(QStringLiteral("name")).toString() == name) { return channel.toObject(); }
	}
	return {};
}

//! mixer.hardware_get's entry for the channel with id @a id.
QJsonObject hardwareOf(const QString& id)
{
	return run(QStringLiteral("mixer.hardware_get"), {{QStringLiteral("channel"), id}}).result.value(QStringLiteral("channels")).toArray()
		.first().toObject();
}

int channelCount()
{
	return run(QStringLiteral("mixer.get_state")).result.value(QStringLiteral("count")).toInt();
}

//! A channel called @a name, made the way the commands make one.
QString addChannel(const QString& name)
{
	const QString id = run(QStringLiteral("mixer.add_channel")).result.value(QStringLiteral("channel")).toString();
	if (!ok(QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), id}, {QStringLiteral("name"), name}})) { return {}; }
	return id;
}

Track* addTrack(Track::Type type, const QString& name, const QString& channel)
{
	Track* track = Track::create(type, Engine::getSong());
	track->setName(name);
	if (!channel.isEmpty())
	{
		ok(QStringLiteral("track.set_mixer_channel"), {{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("channel"), channel}});
	}
	return track;
}

Track* trackNamed(const QString& name)
{
	for (Track* track : Engine::getSong()->tracks()) { if (track->name() == name) { return track; } }
	return nullptr;
}

//! The name of the channel the track called @a name plays into ("Master" for channel 0).
QString channelOf(const QString& name)
{
	Track* track = trackNamed(name);
	if (track == nullptr || track->mixerChannelModel() == nullptr) { return QStringLiteral("<no track>"); }
	return Engine::mixer()->mixerChannel(track->mixerChannelModel()->value())->m_name;
}

} // namespace

class ImportTrackIoTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_key = audioio::currentDeviceKey();
		m_setupBefore = audioio::storedEntry(m_key);
		// Named interface buses: the routes below say "Phones" and "Line In", the project stores their channel numbers.
		QVERIFY(ok(QStringLiteral("audio.io_setup"), {{QStringLiteral("outputs"), QJsonObject{{QStringLiteral("channels"), 4},
			{QStringLiteral("buses"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Main")}, {QStringLiteral("channels"), QStringLiteral("1/2")}},
				QJsonObject{{QStringLiteral("name"), QStringLiteral("Phones")}, {QStringLiteral("channels"), QStringLiteral("3/4")}}}}}},
			{QStringLiteral("inputs"), QJsonObject{{QStringLiteral("buses"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Line In")},
				{QStringLiteral("channels"), QStringLiteral("1/2")}}}}}}}));
	}

	void cleanupTestCase()
	{
		audioio::restoreEntry(m_key, m_setupBefore);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
	}

	void anImportKeepsItsChannelsAndTheirHardwareRouting()
	{
		const QString file = m_dir.filePath(QStringLiteral("source.mmpz"));
		buildSource(file);
		// This project already has a "Keys" channel with a routing of its own, and no "Drums Bus".
		const QString ownKeys = addChannel(Keys);
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ownKeys}, {QStringLiteral("output"), QStringLiteral("Phones")}}));
		const int channelsBefore = channelCount();
		const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());

		const ControlResult imported = run(QStringLiteral("project.import_tracks"), {{QStringLiteral("path"), file}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QCOMPARE(imported.result.value(QStringLiteral("count")).toInt(), 3);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore + 3);
		QCOMPARE(imported.result.value(QStringLiteral("created_channels")).toArray().size(), 1);  // "Keys" was here, "Drums Bus" was not
		QCOMPARE(channelCount(), channelsBefore + 1);

		// The tracks play into the channels of their names; "Plain" stays on the master.
		QCOMPARE(channelOf(QStringLiteral("Drums")), DrumsBus);
		QCOMPARE(channelOf(QStringLiteral("Loop")), Keys);
		QCOMPARE(channelOf(QStringLiteral("Plain")), QStringLiteral("Master"));
		// mixer.get_state: the created channel is a bus, at the source's fader.
		const QJsonObject drums = stateOf(DrumsBus);
		QVERIFY(!drums.isEmpty());
		QVERIFY(drums.value(QStringLiteral("is_bus")).toBool());
		QVERIFY(qAbs(drums.value(QStringLiteral("volume")).toDouble() - 0.5) < 1e-4);
		QCOMPARE(drums.value(QStringLiteral("id")).toString(), imported.result.value(QStringLiteral("created_channels")).toArray().first().toString());
		// mixer.hardware_get: the direct out and the insert's send and return, by their bus names.
		const QJsonObject hardware = hardwareOf(drums.value(QStringLiteral("id")).toString());
		QCOMPARE(hardware.value(QStringLiteral("output")).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("Phones"));
		const QJsonObject insert = hardware.value(QStringLiteral("insert")).toObject();
		QVERIFY(insert.value(QStringLiteral("enabled")).toBool());
		QCOMPARE(insert.value(QStringLiteral("send")).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("Phones"));
		QCOMPARE(insert.value(QStringLiteral("return")).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("Line In"));
		// The channel this project had keeps ITS routing: a resolved channel is used as it is.
		QCOMPARE(hardwareOf(ownKeys).value(QStringLiteral("output")).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("Phones"));

		// One undo step: the tracks and the channel made for them.
		QVERIFY(ok(QStringLiteral("control.undo")));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore);
		QCOMPARE(channelCount(), channelsBefore);
		QVERIFY(stateOf(DrumsBus).isEmpty());
		QVERIFY(!stateOf(Keys).isEmpty());  // the channel that was already here stays
		// Its redo makes both again, routing included.
		QVERIFY(ok(QStringLiteral("control.redo")));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore + 3);
		QCOMPARE(channelCount(), channelsBefore + 1);
		QCOMPARE(channelOf(QStringLiteral("Drums")), DrumsBus);
		QCOMPARE(hardwareOf(stateOf(DrumsBus).value(QStringLiteral("id")).toString()).value(QStringLiteral("output")).toObject()
			.value(QStringLiteral("label")).toString(), QStringLiteral("Phones"));
	}

	void anImportIntoAnEmptyProjectCreatesBothChannels()
	{
		const QString file = m_dir.filePath(QStringLiteral("source2.mmpz"));
		buildSource(file);
		const int channelsBefore = channelCount();
		const ControlResult imported = run(QStringLiteral("project.import_tracks"), {{QStringLiteral("path"), file}, {QStringLiteral("tracks"), QJsonArray{0, 1}}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QCOMPARE(imported.result.value(QStringLiteral("created_channels")).toArray().size(), 2);
		QCOMPARE(hardwareOf(stateOf(Keys).value(QStringLiteral("id")).toString()).value(QStringLiteral("output")).toObject()
			.value(QStringLiteral("label")).toString(), QStringLiteral("Main"));
		// Importing again resolves both by name: nothing more is created.
		const ControlResult again = run(QStringLiteral("project.import_tracks"), {{QStringLiteral("path"), file}, {QStringLiteral("tracks"), QJsonArray{0, 1}}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QCOMPARE(again.result.value(QStringLiteral("created_channels")).toArray().size(), 0);
		QCOMPARE(channelCount(), channelsBefore + 2);
		QVERIFY(ok(QStringLiteral("control.undo")));  // the second import only
		QCOMPARE(channelCount(), channelsBefore + 2);
		QVERIFY(ok(QStringLiteral("control.undo")));  // the first: its tracks and both channels
		QCOMPARE(channelCount(), channelsBefore);
	}

	void aPasteKeepsItsChannelsAndTheirHardwareRouting()
	{
		const int bus = Engine::mixer()->createBusChannel();
		const QString drums = control::channelIdOf(Engine::mixer()->mixerChannel(bus));
		QVERIFY(ok(QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), drums}, {QStringLiteral("name"), DrumsBus}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), drums}, {QStringLiteral("output"), QStringLiteral("Phones")}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), drums}, {QStringLiteral("enabled"), true},
			{QStringLiteral("send"), QStringLiteral("Phones")}, {QStringLiteral("return"), QStringLiteral("Line In")}}));
		Track* source = addTrack(Track::Type::Instrument, QStringLiteral("Drums"), drums);
		QVERIFY(ok(QStringLiteral("project.copy_tracks"), {{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(source)}}}));

		// Pasted into the project that has the channel: the track joins it, nothing is created.
		const int channelsBefore = channelCount();
		const ControlResult same = run(QStringLiteral("project.paste_tracks"));
		QVERIFY2(same.ok, qPrintable(same.errorMessage));
		QCOMPARE(same.result.value(QStringLiteral("created_channels")).toArray().size(), 0);
		QCOMPARE(channelCount(), channelsBefore);
		QVERIFY(ok(QStringLiteral("control.undo")));

		// Pasted where the channel is absent (the other instance's project): it is created, routing included.
		QVERIFY(ok(QStringLiteral("track.remove"), {{QStringLiteral("track"), control::trackIdOf(source)}}));
		QVERIFY(ok(QStringLiteral("mixer.remove_channel"), {{QStringLiteral("channel"), drums}}));
		QVERIFY(stateOf(DrumsBus).isEmpty());
		const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());
		const int without = channelCount();
		const ControlResult pasted = run(QStringLiteral("project.paste_tracks"));
		QVERIFY2(pasted.ok, qPrintable(pasted.errorMessage));
		QCOMPARE(pasted.result.value(QStringLiteral("created_channels")).toArray().size(), 1);
		QCOMPARE(channelCount(), without + 1);
		QCOMPARE(channelOf(QStringLiteral("Drums")), DrumsBus);
		const QJsonObject made = stateOf(DrumsBus);
		QVERIFY(made.value(QStringLiteral("is_bus")).toBool());
		const QJsonObject hardware = hardwareOf(made.value(QStringLiteral("id")).toString());
		QCOMPARE(hardware.value(QStringLiteral("output")).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("Phones"));
		const QJsonObject insert = hardware.value(QStringLiteral("insert")).toObject();
		QCOMPARE(insert.value(QStringLiteral("send")).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("Phones"));
		QCOMPARE(insert.value(QStringLiteral("return")).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("Line In"));

		QVERIFY(ok(QStringLiteral("control.undo")));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore);
		QCOMPARE(channelCount(), without);
		QVERIFY(stateOf(DrumsBus).isEmpty());
		QVERIFY(ok(QStringLiteral("control.redo")));
		QCOMPARE(channelCount(), without + 1);
		QCOMPARE(channelOf(QStringLiteral("Drums")), DrumsBus);
	}

private:
	//! A project of two routed channels and three tracks, saved as @a file; then the song is emptied.
	void buildSource(const QString& file)
	{
		const int bus = Engine::mixer()->createBusChannel();
		const QString drums = control::channelIdOf(Engine::mixer()->mixerChannel(bus));
		QVERIFY(ok(QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), drums}, {QStringLiteral("name"), DrumsBus}}));
		QVERIFY(ok(QStringLiteral("mixer.set_volume"), {{QStringLiteral("channel"), drums}, {QStringLiteral("volume"), 0.5}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), drums}, {QStringLiteral("output"), QStringLiteral("Phones")}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), drums}, {QStringLiteral("enabled"), true},
			{QStringLiteral("send"), QStringLiteral("Phones")}, {QStringLiteral("return"), QStringLiteral("Line In")}}));
		const QString keys = addChannel(Keys);
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), keys}, {QStringLiteral("output"), QStringLiteral("Main")}}));
		addTrack(Track::Type::Instrument, QStringLiteral("Drums"), drums);
		addTrack(Track::Type::Sample, QStringLiteral("Loop"), keys);
		addTrack(Track::Type::Instrument, QStringLiteral("Plain"), {});
		QVERIFY(Engine::getSong()->saveProjectFile(file));
		Engine::getSong()->clearProject();
	}

	QTemporaryDir m_dir;
	QString m_key;
	QJsonObject m_setupBefore;
};

QTEST_MAIN(ImportTrackIoTest)
#include "ImportTrackIoTest.moc"
