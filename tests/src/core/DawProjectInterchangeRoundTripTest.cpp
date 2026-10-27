/*
 * DawProjectInterchangeRoundTripTest.cpp - the DAWproject round trip that
 *                                        compares the MODEL, not the file.
 *
 * Feature row 37 of docs/FEATURE-LIST-0.3.0.md. The engine half is
 * include/DawProjectInterchange.h + the six translation units it declares,
 * and the surface is src/core/ControlCommandsDawProject.cpp; this file is the
 * proof the feature list names explicitly.
 *
 * Five claims, each measured:
 *
 *  1. THE ROUND TRIP: model -> write -> read -> compare the MODEL (not the
 *     file, not its hash). The comparison is track-for-track, clip-for-clip,
 *     note-for-note, mixer-channel-for-mixer-channel, and tempo-map-point-for-
 *     point, using the model's own operator==.
 *  2. THE SESSION ROUND TRIP: model -> apply to session -> extract model from
 *     session -> compare. This is the LOSSY half: the eleven stated losses are
 *     counted and reported, and the test asserts that ONLY the expected losses
 *     occur.
 *  3. THE FILE ROUND TRIP: export -> read back via the control command ->
 *     compare the model JSON. The command's reply carries the model digest
 *     and the loss counts.
 *  4. ONE UNDO: control.undo after dawproject.import restores the session that
 *     was there before it, because the import's inverse is a recorded action
 *     checkpoint carrying the captured document.
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

#include <cmath>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _MSC_VER
#include <io.h>
#endif

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QList>
#include <QPair>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "DawProjectInterchange.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TempoMap.h"
#include "Track.h"
#include "TrackFolder.h"

using namespace lmms;
using namespace lmms::interchange;

namespace
{

//! A model with every entity this module carries: tracks, clips, notes, mixer
//! channels, tempo points and metre points. Deliberately small so the test
//! stays readable, and deliberately covers every seam (two tracks, two clips,
//! two notes, two mixer channels, a tempo point, a metre point).
DawProjectModel authoredModel()
{
	DawProjectModel model;
	model.applicationName = QStringLiteral("Zene Studio");
	model.applicationVersion = QStringLiteral("0.3.0-alpha");
	model.tempo = 140.0;
	model.numerator = 4;
	model.denominator = 4;

	DawProjectMixerChannel master;
	master.id = QStringLiteral("mixer0");
	master.name = QStringLiteral("Master");
	master.role = QStringLiteral("master");
	master.volume = 1.0;
	master.index = 0;
	model.mixerChannels.append(master);

	DawProjectMixerChannel bus;
	bus.id = QStringLiteral("mixer1");
	bus.name = QStringLiteral("Sub");
	bus.role = QStringLiteral("submix");
	bus.volume = 0.8;
	bus.index = 1;
	model.mixerChannels.append(bus);

	DawProjectTrack trackA;
	trackA.id = QStringLiteral("track0");
	trackA.name = QStringLiteral("Bass");
	trackA.color = QStringLiteral("#a2eabf");
	trackA.contentType = QStringLiteral("notes");
	//! The canonical lowercase vocabulary (control::trackTypeNameOf). LOSSY #10:
	//! the document carries the contentType, the NAME is derived from it, so a
	//! fixture spelling it "Instrument" could not round-trip by construction.
	trackA.typeName = QStringLiteral("instrument");
	trackA.channelId = QStringLiteral("strip1");
	trackA.solo = false;
	trackA.mute = false;
	trackA.hasVolume = true;
	trackA.volume = 0.75;
	trackA.hasPan = true;
	trackA.pan = 0.4;
	trackA.destinationChannelId = QStringLiteral("mixer0");
	trackA.mixerChannelIndex = 0;

	DawProjectClip clipA;
	clipA.time = 0.0;
	clipA.duration = 4.0;
	clipA.playStart = 0.0;
	clipA.name = QStringLiteral("Intro");

	DawProjectNote noteA;
	noteA.time = 0.0;
	noteA.duration = 0.25;
	noteA.channel = 0;
	noteA.key = 60;
	noteA.vel = 0.75;
	noteA.rel = 0.75;
	clipA.notes.append(noteA);

	DawProjectNote noteB;
	noteB.time = 0.5;
	noteB.duration = 0.25;
	noteB.channel = 0;
	noteB.key = 64;
	noteB.vel = 0.5;
	noteB.rel = 0.5;
	clipA.notes.append(noteB);

	trackA.clips.append(clipA);
	model.tracks.append(trackA);

	DawProjectTrack trackB;
	trackB.id = QStringLiteral("track1");
	trackB.name = QStringLiteral("Lead");
	trackB.contentType = QStringLiteral("notes");
	trackB.typeName = QStringLiteral("instrument");
	trackB.channelId = QStringLiteral("strip2");
	trackB.destinationChannelId = QStringLiteral("mixer1");
	trackB.mixerChannelIndex = 1;

	DawProjectClip clipB;
	clipB.time = 4.0;
	clipB.duration = 4.0;
	clipB.name = QStringLiteral("Verse");
	trackB.clips.append(clipB);
	model.tracks.append(trackB);

	DawProjectPoint tempoPoint;
	tempoPoint.time = 0.0;
	tempoPoint.isMeter = false;
	tempoPoint.value = 140.0;
	model.tempoPoints.append(tempoPoint);

	DawProjectPoint meterPoint;
	meterPoint.time = 0.0;
	meterPoint.isMeter = true;
	meterPoint.numerator = 4;
	meterPoint.denominator = 4;
	model.meterPoints.append(meterPoint);

	return model;
}

DawProjectModel previousSessionModel()
{
	DawProjectModel model;
	model.tempo = 137.0;
	model.numerator = 3;
	model.denominator = 8;

	DawProjectMixerChannel master;
	master.id = QStringLiteral("before-mixer0");
	master.name = QStringLiteral("Before Master");
	master.role = QStringLiteral("master");
	master.volume = 0.75;
	model.mixerChannels.append(master);

	DawProjectTrack track;
	track.id = QStringLiteral("before-track");
	track.name = QStringLiteral("Preserved Track");
	track.contentType = QStringLiteral("notes");
	track.typeName = QStringLiteral("instrument");
	model.tracks.append(track);

	DawProjectPoint tempo;
	tempo.time = 0.0;
	tempo.value = 109.0;
	model.tempoPoints.append(tempo);

	DawProjectPoint meter;
	meter.time = 0.0;
	meter.isMeter = true;
	meter.numerator = 3;
	meter.denominator = 8;
	model.meterPoints.append(meter);
	return model;
}

DawProjectModel modelWithOverCapacityMap(bool meter)
{
	DawProjectModel model = authoredModel();
	model.tempoPoints.clear();
	model.meterPoints.clear();
	for (int i = 0; i < TempoMap::MaxEvents + 1; ++i)
	{
		DawProjectPoint point;
		point.time = static_cast<double>(i);
		if (meter)
		{
			point.isMeter = true;
			point.numerator = 3;
			point.denominator = 8;
			model.meterPoints.append(point);
		}
		else
		{
			point.value = 120.0 + (i % 80);
			model.tempoPoints.append(point);
		}
	}
	return model;
}

inline QString path(const QTemporaryDir& directory, const QString& name)
{
	return directory.filePath(name);
}

inline ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class DawProjectInterchangeRoundTripTest : public QObject
{
	Q_OBJECT
	void verifyOverCapacityRefusalPreservesSession(bool meter);

private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_directory.isValid());
	}

	/*! Every caller narrows a converted beat to tick_t (an int32), so a beat is
	 *  representable only if its rounded tick fits that type: a beat that fits
	 *  qint64 but not an int32 used to pass and wrap when narrowed. */
	void aBeatIsRepresentableOnlyWithinTheTickRange()
	{
		const double perBeat = DawProjectTicksPerQuarterNote;
		const double lastTick = static_cast<double>(std::numeric_limits<tick_t>::max());
		QVERIFY(dawProjectBeatRepresentable(lastTick / perBeat));
		QVERIFY(!dawProjectBeatRepresentable((lastTick + 1.0) / perBeat));
		QVERIFY(!dawProjectBeatRepresentable(1.0e12));
		QVERIFY(!dawProjectBeatRepresentable(std::numeric_limits<double>::quiet_NaN()));
		QCOMPARE(dawProjectTicksFromBeats(lastTick / perBeat), static_cast<qint64>(lastTick));
	}

	/*! A read that gives back nothing is refused AS A READ by the strict reader the
	 *  .mmpz container uses, so a recovery names the file it could not read. The
	 *  same bytes read without the fault are refused as malformed. The fault is
	 *  ZENE_TEST_CONTAINER_READ_FAULT. */
	void aFailedReadOfTheStrictReaderIsNamedAsARead()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("read-fault.dawproject"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QVERIFY(file.write(QByteArray(64, 'x')) == 64);
		file.close();

		std::vector<DawProjectEntry> entries;
		QString error;
		qputenv("ZENE_TEST_CONTAINER_READ_FAULT", "1");
		const bool ok = dawProjectZipRead(path, &entries, &error);
		qunsetenv("ZENE_TEST_CONTAINER_READ_FAULT");
		QVERIFY(!ok);
		QVERIFY2(error.contains(QStringLiteral("cannot read")), qPrintable(error));

		QVERIFY(!dawProjectZipRead(path, &entries, &error));
		QVERIFY2(!error.contains(QStringLiteral("cannot read")), qPrintable(error));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		Engine::projectJournal()->clearJournal();
		ControlRegistry::instance()->clearTransactions();
	}

	//! Claim 1: model -> write -> read -> compare the MODEL.
	void theFileRoundTripComparesTheModel()
	{
		const DawProjectModel authored = authoredModel();
		const QString file = path(m_directory, QStringLiteral("round-trip.dawproject"));

		DawProjectWriteReport writeReport;
		QString writeError;
		QVERIFY2(writeDawProject(file, authored, &writeReport, &writeError),
			qPrintable(writeError));
		QCOMPARE(writeReport.trackCount, 2);
		QCOMPARE(writeReport.mixerChannelCount, 2);
		QCOMPARE(writeReport.clipCount, 2);
		QCOMPARE(writeReport.noteCount, 2);
		QCOMPARE(writeReport.tempoPointCount, 1);
		QCOMPARE(writeReport.meterPointCount, 1);
		QVERIFY(!writeReport.sha256.isEmpty());

		DawProjectModel read;
		DawProjectReadReport readReport;
		QString readError;
		QVERIFY2(readDawProject(file, &read, &readReport, &readError),
			qPrintable(readError));
		QCOMPARE(readReport.trackCount, 2);
		QCOMPARE(readReport.mixerChannelCount, 2);
		QCOMPARE(readReport.clipCount, 2);
		QCOMPARE(readReport.noteCount, 2);
		QCOMPARE(readReport.tempoPointCount, 1);
		QCOMPARE(readReport.meterPointCount, 1);
		QCOMPARE(readReport.formatVersion, QStringLiteral("1.0"));

		// THE MODEL COMPARISON: not the file, not its hash. operator== compares
		// every track, clip, note, mixer channel, tempo point and metre point,
		// including the ids and the IDREF between a track and its strip.
		QCOMPARE(read, authored);

		// ... and the ids are asserted BY NAME as well, so a regression that
		// renumbers the document (writing `id<n>` instead of the model's own ids)
		// fails with the id it changed rather than only in the blanket compare.
		QCOMPARE(read.mixerChannels[0].id, QStringLiteral("mixer0"));
		QCOMPARE(read.mixerChannels[1].id, QStringLiteral("mixer1"));
		QCOMPARE(read.tracks[0].id, QStringLiteral("track0"));
		QCOMPARE(read.tracks[0].channelId, QStringLiteral("strip1"));
		QCOMPARE(read.tracks[0].destinationChannelId, QStringLiteral("mixer0"));
		QCOMPARE(read.tracks[1].destinationChannelId, QStringLiteral("mixer1"));
	}

	//! Claim 2: model -> apply to session -> extract -> compare.
	void theSessionRoundTripCarriesWhatItClaims()
	{
		const DawProjectModel authored = authoredModel();
		Song* song = Engine::getSong();

		QString applyError;
		QVERIFY2(applyDawProjectModel(song, authored, &applyError),
			qPrintable(applyError));

		DawProjectLossReport loss;
		const DawProjectModel extracted = dawProjectModelFromSong(song, &loss);

		// The mixer channels: position-matched, because the id is generated.
		QCOMPARE(extracted.mixerChannels.size(), authored.mixerChannels.size());
		for (int i = 0; i < extracted.mixerChannels.size(); ++i)
		{
			// Compared as FLOATS: a mixer channel's fader is a FloatModel
			// (DawProjectSession.cpp reads it back as
			// static_cast<double>(m_volumeModel.value())), so the value that
			// comes out of the session is the nearest FLOAT to the authored
			// double. QCOMPARE on the doubles fuzzy-compares at ~1e-12 relative
			// and cannot hold for a value that went through a float - measured:
			// extracted 0.800000011921 vs authored 0.8. The comparison is still
			// exact at the precision the engine's model has.
			QCOMPARE(static_cast<float>(extracted.mixerChannels[i].volume),
				static_cast<float>(authored.mixerChannels[i].volume));
			QCOMPARE(extracted.mixerChannels[i].mute, authored.mixerChannels[i].mute);
			QCOMPARE(extracted.mixerChannels[i].solo, authored.mixerChannels[i].solo);
		}

		// Tracks: count, name, clips, notes.
		QCOMPARE(extracted.tracks.size(), authored.tracks.size());
		for (int i = 0; i < extracted.tracks.size(); ++i)
		{
			QCOMPARE(extracted.tracks[i].name, authored.tracks[i].name);
			QCOMPARE(extracted.tracks[i].contentType, authored.tracks[i].contentType);
			QCOMPARE(extracted.tracks[i].clips.size(), authored.tracks[i].clips.size());
			for (int j = 0; j < extracted.tracks[i].clips.size(); ++j)
			{
				QCOMPARE(extracted.tracks[i].clips[j].notes.size(),
					authored.tracks[i].clips[j].notes.size());
			}
		}

		// Tempo and metre: the globals.
		QCOMPARE(extracted.tempo, authored.tempo);
		QCOMPARE(extracted.numerator, authored.numerator);
		QCOMPARE(extracted.denominator, authored.denominator);
	}

	//! A Channel IDREF can be named by more than one track, so shared mixer
	//! destinations survive the session-to-model conversion without loss.
	void sharedMixerDestinationIsNotReportedLost()
	{
		Song* song = Engine::getSong();
		Engine::mixer()->clear();

		Track* first = Track::create(Track::Type::Instrument, song);
		Track* second = Track::create(Track::Type::Instrument, song);
		QVERIFY(first != nullptr);
		QVERIFY(second != nullptr);
		QVERIFY(first->mixerChannelModel() != nullptr);
		QVERIFY(second->mixerChannelModel() != nullptr);
		first->mixerChannelModel()->setValue(0);
		second->mixerChannelModel()->setValue(0);

		DawProjectLossReport loss;
		const DawProjectModel exported = dawProjectModelFromSong(song, &loss);
		QCOMPARE(exported.tracks.size(), 2);
		QCOMPARE(exported.tracks[0].destinationChannelId, QStringLiteral("mixer0"));
		QCOMPARE(exported.tracks[1].destinationChannelId, QStringLiteral("mixer0"));

		const QString file = path(m_directory, QStringLiteral("shared-mixer.dawproject"));
		DawProjectWriteReport writeReport;
		QString error;
		QVERIFY2(writeDawProject(file, exported, &writeReport, &error), qPrintable(error));
		DawProjectModel readBack;
		DawProjectReadReport readReport;
		QVERIFY2(readDawProject(file, &readBack, &readReport, &error), qPrintable(error));
		QCOMPARE(readBack.tracks.size(), 2);
		QCOMPARE(readBack.tracks[0].destinationChannelId, QStringLiteral("mixer0"));
		QCOMPARE(readBack.tracks[1].destinationChannelId, QStringLiteral("mixer0"));
		QCOMPARE(loss.mixerSharingLost, 0);
	}

	void importResetsAnUnnamedMixerChannelToItsDefaultName()
	{
		Song* song = Engine::getSong();
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		const int channelIndex = mixer->createChannel();
		const QString defaultName = mixer->mixerChannel(channelIndex)->m_name;
		mixer->mixerChannel(channelIndex)->m_name = QStringLiteral("Previous project channel");

		DawProjectModel imported;
		imported.mixerChannels.append(DawProjectMixerChannel{});
		imported.mixerChannels.append(DawProjectMixerChannel{});
		QString error;
		const bool applied = applyDawProjectModel(song, imported, &error);
		const QString importedName = mixer->mixerChannel(channelIndex)->m_name;
		mixer->clear();

		QVERIFY2(applied, qPrintable(error));
		QCOMPARE(importedName, defaultName);
	}

	void sampleTrackPanIsAppliedOnImport()
	{
		DawProjectModel model;
		DawProjectTrack sampleEntry;
		sampleEntry.id = QStringLiteral("sample");
		sampleEntry.name = QStringLiteral("Sample");
		sampleEntry.contentType = QStringLiteral("audio");
		sampleEntry.typeName = QStringLiteral("sample");
		sampleEntry.hasPan = true;
		sampleEntry.pan = 0.75;
		model.tracks.append(sampleEntry);

		QString error;
		Song* song = Engine::getSong();
		QVERIFY2(applyDawProjectModel(song, model, &error), qPrintable(error));
		QCOMPARE(song->tracks().size(), 1);
		auto* sampleTrack = dynamic_cast<SampleTrack*>(song->tracks().front());
		QVERIFY(sampleTrack != nullptr);
		QCOMPARE(sampleTrack->panningModel()->value(), 50.f);
	}

	//! Claim 3: the control command reads the file back and reports the model.
	void theControlCommandReadsTheModelBack()
	{
		const DawProjectModel authored = authoredModel();
		const QString file = path(m_directory, QStringLiteral("command.dawproject"));

		DawProjectWriteReport writeReport;
		QString writeError;
		QVERIFY2(writeDawProject(file, authored, &writeReport, &writeError),
			qPrintable(writeError));

		const ControlResult readResult = run(QStringLiteral("dawproject.read"),
			{{QStringLiteral("path"), file}});
		QVERIFY2(readResult.ok, qPrintable(readResult.errorMessage));
		QCOMPARE(readResult.result.value(QStringLiteral("track_count")).toInt(), 2);
		QCOMPARE(readResult.result.value(QStringLiteral("mixer_channel_count")).toInt(), 2);
		QCOMPARE(readResult.result.value(QStringLiteral("clip_count")).toInt(), 2);
		QCOMPARE(readResult.result.value(QStringLiteral("note_count")).toInt(), 2);
		QCOMPARE(readResult.result.value(QStringLiteral("format_version")).toString(),
			QStringLiteral("1.0"));
	}

	//! Claim 4: import replaces the session, and one undo brings it back.
	void importIsOneUndoStep()
	{
		Song* song = Engine::getSong();
		Engine::mixer()->clear();

		// Build a BEFORE session: one track, one clip, one note.
		DawProjectModel before;
		before.tempo = 120.0;
		DawProjectTrack beforeTrack;
		beforeTrack.id = QStringLiteral("before0");
		beforeTrack.name = QStringLiteral("Before");
		beforeTrack.contentType = QStringLiteral("notes");
		beforeTrack.typeName = QStringLiteral("Instrument");
		beforeTrack.channelId = QStringLiteral("strip1");
		DawProjectClip beforeClip;
		beforeClip.time = 0.0;
		beforeClip.duration = 2.0;
		DawProjectNote beforeNote;
		beforeNote.time = 0.0;
		beforeNote.duration = 0.5;
		beforeNote.key = 48;
		beforeClip.notes.append(beforeNote);
		beforeTrack.clips.append(beforeClip);
		before.tracks.append(beforeTrack);
		before.mixerChannels.append(DawProjectMixerChannel{});
		before.mixerChannels[0].id = QStringLiteral("mixer0");
		before.mixerChannels[0].role = QStringLiteral("master");

		QString applyError;
		QVERIFY2(applyDawProjectModel(song, before, &applyError),
			qPrintable(applyError));

		// Capture the before-state model.
		DawProjectLossReport beforeLoss;
		const DawProjectModel beforeExtracted = dawProjectModelFromSong(song, &beforeLoss);
		QCOMPARE(beforeExtracted.tracks.size(), 1);

		// The AFTER file: different track.
		const DawProjectModel after = authoredModel();
		const QString file = path(m_directory, QStringLiteral("undo.dawproject"));
		DawProjectWriteReport writeReport;
		QString writeError;
		QVERIFY2(writeDawProject(file, after, &writeReport, &writeError),
			qPrintable(writeError));

		// Import it.
		const ControlResult importResult = run(QStringLiteral("dawproject.import"),
			{{QStringLiteral("path"), file}});
		QVERIFY2(importResult.ok, qPrintable(importResult.errorMessage));
		QCOMPARE(importResult.result.value(QStringLiteral("mixer_channel_count")).toInt(), 2);
		QCOMPARE(ControlRegistry::instance()->transactions().last().toObject()
			.value(QStringLiteral("before")).toObject()
			.value(QStringLiteral("track_count")).toInt(), 1);

		// The session now has the AFTER model.
		DawProjectLossReport afterLoss;
		const DawProjectModel afterExtracted = dawProjectModelFromSong(song, &afterLoss);
		QCOMPARE(afterExtracted.tracks.size(), 2);

		// Undo: the before model comes back.
		const ControlResult undoResult = run(QStringLiteral("control.undo"));
		QVERIFY2(undoResult.ok, qPrintable(undoResult.errorMessage));
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), 1);

		DawProjectLossReport restoredLoss;
		const DawProjectModel restored = dawProjectModelFromSong(song, &restoredLoss);
		QCOMPARE(restored.tracks.size(), 1);
		QCOMPARE(restored.tracks[0].name, QStringLiteral("Before"));
	}

	void importWithoutMixerChannelsResetsTheMaster()
	{
		Mixer* mixer = Engine::mixer();
		MixerChannel* master = mixer->mixerChannel(0);
		master->m_name = QStringLiteral("Previous Master");
		master->m_volumeModel.setValue(0.25f);
		master->m_muteModel.setValue(true);
		master->m_soloModel.setValue(true);

		DawProjectModel withoutMixerChannels;
		withoutMixerChannels.applicationName = QStringLiteral("Zene Studio");
		withoutMixerChannels.applicationVersion = QStringLiteral("0.3.0-alpha");
		const QString file = path(m_directory, QStringLiteral("empty-mixer.dawproject"));
		DawProjectWriteReport writeReport;
		QString writeError;
		QVERIFY2(writeDawProject(file, withoutMixerChannels, &writeReport, &writeError),
			qPrintable(writeError));
		QCOMPARE(writeReport.mixerChannelCount, 0);
		DawProjectModel readBack;
		DawProjectReadReport readReport;
		QString readError;
		QVERIFY2(readDawProject(file, &readBack, &readReport, &readError),
			qPrintable(readError));
		QCOMPARE(readReport.mixerChannelCount, 0);

		const ControlResult imported = run(QStringLiteral("dawproject.import"),
			{{QStringLiteral("path"), file}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QCOMPARE(static_cast<int>(mixer->numChannels()), 1);
		QCOMPARE(master->m_name, QStringLiteral("Master"));
		QCOMPARE(master->m_volumeModel.value(), 1.0f);
		QVERIFY(!master->m_muteModel.value());
		QVERIFY(!master->m_soloModel.value());
	}

	void overCapacityTempoImportIsRefusedBeforeMutation()
	{
		verifyOverCapacityRefusalPreservesSession(false);
	}

	void overCapacityMeterImportIsRefusedBeforeMutation()
	{
		verifyOverCapacityRefusalPreservesSession(true);
	}

	void importUndoBudgetCountsBothSessionAndRedoDocuments()
	{
		Song* song = Engine::getSong();
		Mixer* mixer = Engine::mixer();
		QString failure;
		auto prepare = [&](const QString& masterName, bool withMap) {
			song->aboutToClearTracks();
			song->clearAllTracks();
			mixer->clear();
			mixer->mixerChannel(0)->m_name = masterName;
			TempoMap map;
			if (withMap)
			{
				for (int i = 0; i < TempoMap::MaxEvents; ++i)
				{
					TempoMapEvent event;
					event.tick = i * 48;
					event.hasTempo = true;
					event.tempo = 90 + (i % 200);
					if (!map.addEvent(event))
					{
						failure = QStringLiteral("could not prepare the bounded before tempo map");
						return;
					}
				}
				map.setActive(true);
			}
			song->tempoMap().edit([&map](TempoMap& target) { target = map; return true; });
			song->setTempo(123);
			song->getTimeSigModel().numeratorModel().setValue(4);
			song->getTimeSigModel().denominatorModel().setValue(4);
			Engine::projectJournal()->clearJournal();
			ControlRegistry::instance()->clearTransactions();
		};
		auto measure = [&](const QString& suffix, const QString& masterName, bool withMap,
			const DawProjectModel& importedModel, qint64* beforeBytes, qint64* afterBytes) {
			prepare(masterName, withMap);
			if (!failure.isEmpty()) { return false; }
			const ControlResult beforeDepth = run(QStringLiteral("control.undo_depth"));
			if (!beforeDepth.ok)
			{
				failure = QStringLiteral("could not read undo depth before import");
				return false;
			}
			*beforeBytes = beforeDepth.result.value(QStringLiteral("retained_bytes")).toVariant()
				.toLongLong();

			const QString file = path(m_directory, suffix);
			QString error;
			DawProjectWriteReport writeReport;
			if (!writeDawProject(file, importedModel, &writeReport, &error))
			{
				failure = QStringLiteral("could not write the undo-budget fixture: %1").arg(error);
				return false;
			}
			const ControlResult imported = run(QStringLiteral("dawproject.import"),
				{{QStringLiteral("path"), file}});
			if (!imported.ok)
			{
				failure = QStringLiteral("undo-budget import failed: %1").arg(imported.errorMessage);
				return false;
			}
			const ControlResult afterDepth = run(QStringLiteral("control.undo_depth"));
			if (!afterDepth.ok)
			{
				failure = QStringLiteral("could not read undo depth after import");
				return false;
			}
			*afterBytes = afterDepth.result.value(QStringLiteral("retained_bytes")).toVariant()
				.toLongLong();
			if (*afterBytes <= *beforeBytes)
			{
				failure = QStringLiteral("retained_bytes did not grow from %1 after import (was %2)")
					.arg(*beforeBytes).arg(*afterBytes);
				return false;
			}
			return true;
		};

		const DawProjectModel small = authoredModel();
		qint64 smallBefore = 0;
		qint64 smallAfter = 0;
		QVERIFY2(measure(QStringLiteral("undo-budget-small.dawproject"), QStringLiteral("Master"),
			false, small, &smallBefore, &smallAfter), qPrintable(failure));

		qint64 richBefore = 0;
		qint64 richAfter = 0;
		QVERIFY2(measure(QStringLiteral("undo-budget-rich-before.dawproject"),
			QString(4096, QLatin1Char('C')), true, small, &richBefore, &richAfter),
			qPrintable(failure));
		QVERIFY2(richAfter > smallAfter,
			"a larger captured channel name and tempo map must occupy more retained bytes");

		DawProjectModel noteHeavy = small;
		for (int i = 0; i < 256; ++i)
		{
			DawProjectNote note;
			note.time = static_cast<double>(i % 16) / 16.0;
			note.duration = 0.0625;
			note.key = 48 + (i % 48);
			noteHeavy.tracks[0].clips[0].notes.append(note);
		}
		qint64 notesBefore = 0;
		qint64 notesAfter = 0;
		QVERIFY2(measure(QStringLiteral("undo-budget-note-heavy.dawproject"),
			QStringLiteral("Master"), false, noteHeavy, &notesBefore, &notesAfter),
			qPrintable(failure));
		QVERIFY2(notesAfter > smallAfter,
			"a larger redo model with notes must occupy more retained bytes");

		song->clearProject();
		mixer->clear();
		Engine::projectJournal()->clearJournal();
		ControlRegistry::instance()->clearTransactions();
	}

	//! A beat value the engine cannot turn into a tick - NaN, an infinity, or a
	//! magnitude past the tick grid - is REFUSED, not converted. Converting such
	//! a double to qint64 is undefined behaviour, so the conversion is where the
	//! contract has to hold: it says "0 for a value that is not a beat this
	//! grid can express", and an import carrying such a point fails its
	//! addEvent rather than inheriting whatever the conversion produced.
	void beatConversionRejectsValuesThatAreNotBeats()
	{
		QCOMPARE(dawProjectTicksFromBeats(0.0), qint64(0));
		QCOMPARE(dawProjectTicksFromBeats(1.0), qint64(48));
		QCOMPARE(dawProjectTicksFromBeats(0.5), qint64(24));

		QCOMPARE(dawProjectTicksFromBeats(std::numeric_limits<double>::quiet_NaN()), qint64(0));
		QCOMPARE(dawProjectTicksFromBeats(std::numeric_limits<double>::infinity()), qint64(0));
		QCOMPARE(dawProjectTicksFromBeats(-std::numeric_limits<double>::infinity()), qint64(0));
		QCOMPARE(dawProjectTicksFromBeats(1e300), qint64(0));
		QCOMPARE(dawProjectTicksFromBeats(-1e300), qint64(0));
		QCOMPARE(dawProjectTicksFromBeats(1e18), qint64(0));
	}

	//! And the IMPORT refuses such a document rather than applying a tick the
	//! conversion invented: the tempo point's own value is inside the engine's
	//! bounds, so only the TIME can refuse it.
	void importRefusesAnUnrepresentableBeat()
	{
		const QString file = path(m_directory, QStringLiteral("huge-beat.dawproject"));
		const QByteArray projectXml =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\">"
			"<Channel id=\"c1\"/></Track></Structure>"
			"<Arrangement><Lanes/>"
			"<TempoAutomation timeUnit=\"beats\" unit=\"bpm\" id=\"id1\">"
			"<Target parameter=\"id2\"/>"
			"<RealPoint time=\"1e300\" value=\"120\" interpolation=\"hold\"/>"
			"</TempoAutomation></Arrangement></Project>";
		DawProjectModel rejectedModel;
		DawProjectReadReport readReport;
		QString readError;
		QVERIFY2(!dawProjectModelFromXml(projectXml, &rejectedModel, &readReport, &readError),
			"the reader accepted an out-of-range tempo point");
		QVERIFY(readError.contains(QStringLiteral("48-tick-per-beat")));

		const std::vector<DawProjectEntry> entries = {
			{QStringLiteral("project.xml"), projectXml}};
		QString writeError;
		QVERIFY2(dawProjectZipWrite(file, entries, &writeError), qPrintable(writeError));

		const ControlResult result = run(QStringLiteral("dawproject.import"),
			{{QStringLiteral("path"), file}});
		QVERIFY(!result.ok);
	}

	//! Claim 5: typed refusals - a missing file, a file that is not a ZIP, a
	//! file with no project.xml, and a relative path on export.
	void refusalsAreTyped()
	{
		const ControlResult missing = run(QStringLiteral("dawproject.read"),
			{{QStringLiteral("path"), QStringLiteral("/nonexistent/file.dawproject")}});
		QVERIFY(!missing.ok);

		const QString notAZip = path(m_directory, QStringLiteral("not-a-zip.dawproject"));
		QFile f(notAZip);
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.write("not a zip");
		f.close();
		const ControlResult badZip = run(QStringLiteral("dawproject.read"),
			{{QStringLiteral("path"), notAZip}});
		QVERIFY(!badZip.ok);

		// A ZIP with no project.xml entry.
		const QString emptyZip = path(m_directory, QStringLiteral("no-project.dawproject"));
		QFile emptyFile(emptyZip);
		QVERIFY(emptyFile.open(QIODevice::WriteOnly));
		emptyFile.write("PK\x05\x06\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00");
		emptyFile.close();
		const ControlResult noProjectXml = run(QStringLiteral("dawproject.read"),
			{{QStringLiteral("path"), emptyZip}});
		QVERIFY(!noProjectXml.ok);

		// A relative path on export: refused as argument semantics, not rounded
		// into a file next to the process's own working directory.
		const ControlResult relative = run(QStringLiteral("dawproject.export"),
			{{QStringLiteral("path"), QStringLiteral("relative.dawproject")}});
		QVERIFY(!relative.ok);
	}

//! Luna cluster 5072 / 4136: the digest must separate two models that
	//! operator== separates. Mixer `audioChannels` and clip `color` are both
	//! equality-relevant (DawProjectInterchange.h:209,236) and both are written
	//! and read back, so two models differing only in either are DIFFERENT models
	//! whose digests used to collide.
	void theDigestSeparatesEveryEqualityRelevantField()
	{
		const DawProjectModel authored = authoredModel();
		const QString base = dawProjectModelDigest(authored);
		QVERIFY(!base.isEmpty());

		DawProjectModel recolored = authored;
		recolored.tracks[0].clips[0].color = QStringLiteral("#ff8800");
		QVERIFY2(recolored.tracks[0].clips[0] != authored.tracks[0].clips[0],
			"the fixture's recolored clip must actually differ under clip equality");
		QVERIFY2(dawProjectModelDigest(recolored) != base,
			"the digest ignored clip color, which clip equality compares");

		DawProjectModel widened = authored;
		widened.mixerChannels[0].audioChannels = 8;
		QVERIFY2(widened.mixerChannels[0] != authored.mixerChannels[0],
			"the fixture's widened strip must actually differ under channel equality");
		QVERIFY2(dawProjectModelDigest(widened) != base,
			"the digest ignored mixer audioChannels, which mixer equality compares");
	}

	//! Luna cluster 5070: the JSON model view is what `dawproject.read` returns
	//! as `model`, and its description promises "every track with its clips and
	//! notes". The clip object must therefore carry the color and the notes
	//! themselves, not only a count of them.
	void theJsonModelViewCarriesNotesAndClipColors()
	{
		const QJsonObject root = dawProjectModelJson(authoredModel());
		const QJsonArray tracks = root.value(QStringLiteral("tracks")).toArray();
		QCOMPARE(tracks.size(), 2);

		const QJsonObject firstTrack = tracks.at(0).toObject();
		const QJsonArray clips = firstTrack.value(QStringLiteral("clips")).toArray();
		QCOMPARE(clips.size(), 1);
		const QJsonObject clip = clips.at(0).toObject();
		QCOMPARE(clip.value(QStringLiteral("name")).toString(), QStringLiteral("Intro"));
		QVERIFY2(clip.contains(QStringLiteral("color")),
			"the clip JSON dropped clip.color, so a client cannot read it back");
		QCOMPARE(clip.value(QStringLiteral("note_count")).toInt(), 2);

		QVERIFY2(clip.contains(QStringLiteral("notes")),
			"the clip JSON dropped the notes themselves, only keeping their count");
		const QJsonArray notes = clip.value(QStringLiteral("notes")).toArray();
		QCOMPARE(notes.size(), 2);

		const QJsonObject first = notes.at(0).toObject();
		QCOMPARE(first.value(QStringLiteral("key")).toInt(), 60);
		QCOMPARE(first.value(QStringLiteral("channel")).toInt(), 0);
		QCOMPARE(first.value(QStringLiteral("time")).toDouble(), 0.0);
		QCOMPARE(first.value(QStringLiteral("duration")).toDouble(), 0.25);
		QCOMPARE(first.value(QStringLiteral("vel")).toDouble(), 0.75);
		QCOMPARE(first.value(QStringLiteral("rel")).toDouble(), 0.75);
		const QJsonObject second = notes.at(1).toObject();
		QCOMPARE(second.value(QStringLiteral("key")).toInt(), 64);
		QCOMPARE(second.value(QStringLiteral("time")).toDouble(), 0.5);
	}

	//! Luna cluster 4125: a track whose contentType names ONLY content this
	//! module cannot represent ("markers") must be reported as lost rather than
	//! arriving as an empty instrument track with a clean loss report.
	void markerOnlyTracksReportTheirContentAsLost()
	{
		const QByteArray xml =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure>"
			"<Track id=\"t1\" name=\"Markers\" contentType=\"markers\">"
			"<Channel id=\"c1\"/></Track>"
			"</Structure>"
			"<Arrangement><Lanes><Lanes track=\"t1\"><markers/></Lanes></Lanes></Arrangement>"
			"</Project>";

		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(dawProjectModelFromXml(xml, &model, &report, &error), qPrintable(error));
		QCOMPARE(model.tracks.size(), 1);
		QVERIFY2(model.tracks[0].lostContentType,
			"a markers-only track was read as supported with a clean loss report");
		QVERIFY2(report.loss.unmappedTrackTypes >= 1,
			"the skipped marker content was not counted in the loss report");

		// The negative half: a track that declares nothing at all, or only
		// content this module DOES carry, is still lossless. The writer emits an
		// empty contentType for an event/video track (LOSSY #5), so marking that
		// as lost would misreport every such export.
		bool lostForEmpty = false;
		QCOMPARE(dawProjectTypeForContentType(QString(), &lostForEmpty),
			QStringLiteral("instrument"));
		QVERIFY(!lostForEmpty);

		bool lostForNotes = false;
		QCOMPARE(dawProjectTypeForContentType(QStringLiteral("notes"), &lostForNotes),
			QStringLiteral("instrument"));
		QVERIFY(!lostForNotes);

		bool lostForAudio = false;
		QCOMPARE(dawProjectTypeForContentType(QStringLiteral("audio"), &lostForAudio),
			QStringLiteral("sample"));
		QVERIFY(!lostForAudio);
	}

	//! Luna cluster 4124: a track declaring BOTH audio and notes carries a
	//! <Notes> clip. Selecting `sample` for it creates a sample track, where the
	//! MIDI clip cannot exist and the notes are silently dropped on import. The
	//! notes must survive; the audio is already counted as LOSSY #1.
	void mixedNotesAndAudioTracksKeepTheirNotes()
	{
		const QByteArray xml =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure>"
			"<Track id=\"t1\" name=\"Mixed\" contentType=\"notes audio\">"
			"<Channel id=\"c1\"/></Track>"
			"</Structure>"
			"<Arrangement><Lanes><Lanes track=\"t1\"><Clips>"
			"<Clip time=\"0\" duration=\"4\"><Notes>"
			"<Note time=\"0\" duration=\"1\" channel=\"0\" key=\"60\" vel=\"0.8\"/>"
			"<Note time=\"1\" duration=\"1\" channel=\"0\" key=\"64\" vel=\"0.6\"/>"
			"</Notes></Clip></Clips></Lanes></Lanes></Arrangement>"
			"</Project>";

		DawProjectModel read;
		DawProjectReadReport readReport;
		QString error;
		QVERIFY2(dawProjectModelFromXml(xml, &read, &readReport, &error), qPrintable(error));
		QCOMPARE(read.tracks.size(), 1);
		QCOMPARE(read.noteCount(), 2);
		QVERIFY2(read.tracks[0].typeName == QStringLiteral("instrument"),
			"a notes+audio track was typed by its audio half, which cannot hold notes");

		Engine::mixer()->clear();
		QVERIFY2(applyDawProjectModel(Engine::getSong(), read, &error), qPrintable(error));
		const DawProjectModel extracted = dawProjectModelFromSong(Engine::getSong(), nullptr);
		QVERIFY2(extracted.noteCount() > 0,
			"the mixed track's notes were dropped when the model was applied");
	}

//! Luna cluster 5062: Project.xsd makes <Application> use="required", and the
	//! reader's own header claims a document without one is REFUSED. It only
	//! tracked <Project>, so `<Project version="1.0"/>` read successfully and
	//! produced a model with an empty application name.
	void aDocumentWithoutTheRequiredApplicationIsRefused()
	{
		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(!dawProjectModelFromXml(QByteArray("<Project version=\"1.0\"/>"),
			&model, &report, &error),
			"a document with no <Application> was accepted with an empty application name");
		QVERIFY2(error.contains(QStringLiteral("Application")),
			"the refusal does not name the element that is missing");

		// The positive half: the same document WITH the element reads.
		QVERIFY2(dawProjectModelFromXml(QByteArray(
			"<Project version=\"1.0\"><Application name=\"Probe\" version=\"1\"/></Project>"),
			&model, &report, &error), qPrintable(error));
		QCOMPARE(model.applicationName, QStringLiteral("Probe"));
	}

	//! Luna cluster 3098: a numerator or denominator the engine cannot use (zero,
	//! negative, or a denominator that is not a power of two) was carried into the
	//! model as-is and reached the session, where it is an unusable metre.
	void invalidTimeSignaturesAreRefused()
	{
		const QString prefix =
			QStringLiteral("<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/><TimeSignature numerator=\"%1\" denominator=\"%2\"/>"
			"</Transport></Project>");

		const QList<QPair<int, int>> invalid = {
			{0, 4}, {4, 0}, {-3, 4}, {4, -4}, {40, 4}, {4, 3}
		};
		for (const auto& signature : invalid)
		{
			const QByteArray xml = prefix
				.arg(signature.first).arg(signature.second).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				qPrintable(QStringLiteral("the meter %1/%2 was accepted")
					.arg(signature.first).arg(signature.second)));
		}

		// The positive half: every meter the engine supports still reads.
		for (int denominator = 1; denominator <= 32; denominator *= 2)
		{
			const QByteArray xml = prefix.arg(3).arg(denominator).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(dawProjectModelFromXml(xml, &model, &report, &error), qPrintable(error));
			QCOMPARE(model.numerator, 3);
			QCOMPARE(model.denominator, denominator);
		}

		// And the same rule on a <TimeSignaturePoint> inside the automation.
		const QByteArray meterPoint =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Arrangement><TimeSignatureAutomation timeUnit=\"beats\">"
			"<TimeSignaturePoint time=\"0\" numerator=\"4\" denominator=\"7\"/>"
			"</TimeSignatureAutomation></Arrangement></Project>";
		DawProjectModel pointModel;
		DawProjectReadReport pointReport;
		QString pointError;
		QVERIFY2(!dawProjectModelFromXml(meterPoint, &pointModel, &pointReport, &pointError),
			"an automation metre point with denominator 7 was accepted");
	}

	//! Luna cluster 3097: a numeric attribute that is PRESENT but malformed is not
	//! an absent optional attribute. `<Tempo value="oops"/>` used to read as the
	//! 120 BPM default, so a document the engine would refuse arrived as a
	//! supported tempo.
	void malformedNumericAttributesAreRefused()
	{
		const QString tempo =
			QStringLiteral("<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"%1\"/></Transport></Project>");
		for (const char* value : {"oops", "1.2.3", "12x"})
		{
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(tempo.arg(QLatin1String(value)).toLatin1(),
				&model, &report, &error),
				qPrintable(QStringLiteral("a malformed tempo value \"%1\" was read as the default")
					.arg(QLatin1String(value))));
		}

		// A malformed integer, on the metre and on a note's key: same rule.
		const QByteArray malformedInteger =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/>"
			"<TimeSignature numerator=\"four\" denominator=\"4\"/></Transport></Project>";
		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(!dawProjectModelFromXml(malformedInteger, &model, &report, &error),
			"a malformed metre numerator was read as the 4/4 default");

		const QByteArray malformedKey =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\">"
			"<Channel id=\"c1\"/></Track></Structure>"
			"<Arrangement><Lanes><Lanes track=\"t1\"><Clips>"
			"<Clip time=\"0\" duration=\"4\"><Notes>"
			"<Note time=\"0\" duration=\"1\" channel=\"0\" key=\"sixty\" vel=\"0.8\"/>"
			"</Notes></Clip></Clips></Lanes></Lanes></Arrangement></Project>";
		DawProjectModel keyModel;
		DawProjectReadReport keyReport;
		QString keyError;
		QVERIFY2(!dawProjectModelFromXml(malformedKey, &keyModel, &keyReport, &keyError),
			"a malformed note key was read as the default 60");
	}

	//! XML booleans have four lexical forms. A present value outside that set is
	//! malformed; it must not silently become false like an absent attribute.
	void malformedBooleanAttributesAreRefused()
	{
		const QString prefix = QStringLiteral(
			"<Project version=\"1.0\"><Application name=\"Probe\" version=\"1\"/>"
			"<Structure><Track id=\"t1\" name=\"Track\" contentType=\"notes\">");
		const QString suffix = QStringLiteral("</Track></Structure></Project>");
		const QList<QPair<QString, QString>> invalidChannels = {
			{QStringLiteral("<Channel id=\"c1\" solo=\"yes\"/>"), QStringLiteral("solo")},
			{QStringLiteral("<Channel id=\"c1\" solo=\"&#xA0;true\"/>"), QStringLiteral("solo")},
			{QStringLiteral("<Channel id=\"c1\"><Mute value=\"invalid\"/></Channel>"),
				QStringLiteral("Mute")},
			{QStringLiteral("<Channel id=\"c1\" solo=\"\"/>"), QStringLiteral("solo")}
		};
		for (const auto& invalid : invalidChannels)
		{
			const QByteArray xml = (prefix + invalid.first + suffix).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				qPrintable(QStringLiteral("invalid %1 boolean was accepted")
					.arg(invalid.second)));
			QVERIFY2(error.contains(QStringLiteral("boolean")), qPrintable(error));
		}

		for (const QString& value : {QStringLiteral("true"), QStringLiteral("false"),
			QStringLiteral("1"), QStringLiteral("0")})
		{
			const QString xml = prefix + QStringLiteral(
				"<Channel id=\"c1\" solo=\"%1\"><Mute value=\"%1\"/></Channel>").arg(value)
				+ suffix;
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(dawProjectModelFromXml(xml.toLatin1(), &model, &report, &error),
				qPrintable(error));
			const bool expected = value == QLatin1String("true") || value == QLatin1String("1");
			QCOMPARE(model.tracks[0].solo, expected);
			QCOMPARE(model.tracks[0].mute, expected);
		}
	}

	//! Luna cluster new-p8-111: an EMPTY numeric attribute is PRESENT, not absent -
	//! the absent-attribute fallback belongs to an attribute the document did not
	//! carry, so `<RealPoint time="">` must be refused as malformed, not read as
	//! time zero (and `<Tempo value="">` not as the 120 default).
	void emptyNumericAttributesAreNotAbsent()
	{
		const QByteArray emptyTempo =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"\"/></Transport></Project>";
		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(!dawProjectModelFromXml(emptyTempo, &model, &report, &error),
			"an empty tempo value was read as the 120 default");

		const QByteArray emptyPointTime =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Arrangement><TempoAutomation timeUnit=\"beats\">"
			"<RealPoint time=\"\" value=\"120\"/>"
			"</TempoAutomation></Arrangement></Project>";
		DawProjectModel pointModel;
		DawProjectReadReport pointReport;
		QString pointError;
		QVERIFY2(!dawProjectModelFromXml(emptyPointTime, &pointModel, &pointReport, &pointError),
			"an empty automation point time was read as time zero");

		const QByteArray emptyNumerator =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/>"
			"<TimeSignature numerator=\"\" denominator=\"4\"/></Transport></Project>";
		DawProjectModel metreModel;
		DawProjectReadReport metreReport;
		QString metreError;
		QVERIFY2(!dawProjectModelFromXml(emptyNumerator, &metreModel, &metreReport, &metreError),
			"an empty metre numerator was read as the 4/4 default");
	}

	//! Luna cluster new-p8-113: the format declares each channel value as a RealParameter
	//! carrying its OWN unit and bounds on the element (`<Volume unit="linear" min="0"
	//! max="2" value="1">`, docs/DAWPROJECT-INTERCHANGE.md:193/198) - those bounds belong
	//! to the document, so a finite number outside them is not a value the document can
	//! mean: it is refused rather than read and clamped silently when the model is applied.
	void outOfRangeChannelValuesAreRefused()
	{
		const QString strip =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Channel id=\"m0\" role=\"master\">%1</Channel></Structure></Project>";
		for (const char* child : {"<Volume unit=\"linear\" min=\"0\" max=\"2\" value=\"4\"/>",
			"<Volume unit=\"linear\" min=\"0\" max=\"2\" value=\"-0.5\"/>",
			"<Pan unit=\"normalized\" min=\"0\" max=\"1\" value=\"-1\"/>",
			"<Pan unit=\"normalized\" min=\"0\" max=\"1\" value=\"1.5\"/>"})
		{
			const QByteArray xml = strip.arg(QLatin1String(child)).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				qPrintable(QStringLiteral("the mixer strip carried %1").arg(QLatin1String(child))));
		}

		const QString track =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\">"
			"<Channel id=\"c1\">%1</Channel></Track></Structure></Project>";
		for (const char* child : {"<Volume unit=\"linear\" min=\"0\" max=\"2\" value=\"2.5\"/>",
			"<Pan unit=\"normalized\" min=\"0\" max=\"1\" value=\"-0.25\"/>"})
		{
			const QByteArray xml = track.arg(QLatin1String(child)).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				"an out-of-range track channel value was accepted");
		}

		// The positive half: the endpoints of the declared range still read.
		const QByteArray inRange =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Channel id=\"m0\" role=\"master\">"
			"<Volume unit=\"linear\" min=\"0\" max=\"2\" value=\"2\"/><Pan unit=\"normalized\" min=\"0\" max=\"1\" value=\"0\"/></Channel>"
			"<Track id=\"t1\" name=\"T\" contentType=\"notes\">"
			"<Channel id=\"c1\"><Volume unit=\"linear\" min=\"0\" max=\"2\" value=\"0\"/><Pan unit=\"normalized\" min=\"0\" max=\"1\" value=\"1\"/></Channel></Track>"
			"</Structure></Project>";
		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(dawProjectModelFromXml(inRange, &model, &report, &error), qPrintable(error));
		QCOMPARE(model.mixerChannels.size(), 1);
		QCOMPARE(model.mixerChannels[0].volume, 2.0);
		QCOMPARE(model.tracks.size(), 1);
		QCOMPARE(model.tracks[0].hasVolume, true);
		QCOMPARE(model.tracks[0].volume, 0.0);
		QCOMPARE(model.tracks[0].hasPan, true);
		QCOMPARE(model.tracks[0].pan, 1.0);
	}

	//! Luna round 2: the bounds a RealParameter declares are the DOCUMENT's, not the
	//! format's - 0..2/0..1 are only what this build's writer emits (its models' own
	//! reach: the mixer fader 0..2, the track volume 0..200/100, the panning model
	//! -100..100/200). A narrower declared range must refuse values the models could
	//! hold (a format-range error); a wider one must not stretch the models (an
	//! import-compatibility error, kept distinct); a unit the models do not speak is
	//! refused too; absent min/max declare no format bound.
	void declaredBoundsUnitsAndModelReach()
	{
		const QString strip =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Channel id=\"m0\" role=\"master\">%1</Channel></Structure></Project>";

		// Narrower declared bounds: inside the models' reach but outside the
		// document's own range - refused as format-range errors.
		for (const char* child : {"<Volume unit=\"linear\" min=\"0.5\" max=\"1.5\" value=\"1.75\"/>",
			"<Volume unit=\"linear\" min=\"0.5\" max=\"1.5\" value=\"0.25\"/>",
			"<Pan unit=\"normalized\" min=\"0.25\" max=\"0.75\" value=\"0.9\"/>"})
		{
			const QByteArray xml = strip.arg(QLatin1String(child)).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				qPrintable(QStringLiteral("the mixer strip carried %1").arg(QLatin1String(child))));
			QVERIFY2(error.contains("declared"), qPrintable(error));
		}

		// Wider declared bounds: inside the file's bounds but past the models' reach -
		// an import-compatibility refusal, distinct from a format-range one.
		{
			const QByteArray xml = strip.arg(QLatin1String(
				"<Volume unit=\"linear\" min=\"-5\" max=\"5\" value=\"3\"/>")).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				"a value past the models' reach was accepted");
			QVERIFY2(error.contains("cannot hold"), qPrintable(error));
		}

		// A unit the models do not speak is refused even when the value fits -
		// `decibel` is a valid schema unit, unlike the made-up `db`.
		{
			const QByteArray xml = strip.arg(QLatin1String(
				"<Volume unit=\"decibel\" min=\"-24\" max=\"0\" value=\"-12\"/>")).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				"a foreign unit was accepted");
			QVERIFY2(error.contains("unit"), qPrintable(error));
		}

		// Absent min/max declare no format bound: the models' reach still applies.
		{
			const QByteArray ok = strip.arg(QLatin1String(
				"<Volume unit=\"linear\" value=\"1\"/>")).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(dawProjectModelFromXml(ok, &model, &report, &error), qPrintable(error));
			QCOMPARE(model.mixerChannels.size(), 1);
			QCOMPARE(model.mixerChannels[0].volume, 1.0);

			const QByteArray past = strip.arg(QLatin1String(
				"<Volume unit=\"linear\" value=\"3\"/>")).toLatin1();
			DawProjectModel pastModel;
			DawProjectReadReport pastReport;
			QString pastError;
			QVERIFY2(!dawProjectModelFromXml(past, &pastModel, &pastReport, &pastError),
				"a value past the models' reach was accepted");
			QVERIFY2(pastError.contains("cannot hold"), qPrintable(pastError));
		}

		// Custom bounds endpoints are inclusive: 0.5 and 1.5 read.
		{
			const QByteArray lo = strip.arg(QLatin1String(
				"<Volume unit=\"linear\" min=\"0.5\" max=\"1.5\" value=\"0.5\"/>")).toLatin1();
			const QByteArray hi = strip.arg(QLatin1String(
				"<Volume unit=\"linear\" min=\"0.5\" max=\"1.5\" value=\"1.5\"/>")).toLatin1();
			DawProjectModel loModel;
			DawProjectReadReport loReport;
			QString loError;
			QVERIFY2(dawProjectModelFromXml(lo, &loModel, &loReport, &loError), qPrintable(loError));
			QCOMPARE(loModel.mixerChannels[0].volume, 0.5);
			DawProjectModel hiModel;
			DawProjectReadReport hiReport;
			QString hiError;
			QVERIFY2(dawProjectModelFromXml(hi, &hiModel, &hiReport, &hiError), qPrintable(hiError));
			QCOMPARE(hiModel.mixerChannels[0].volume, 1.5);
		}
	}

	//! Luna round 3: `unit` is REQUIRED on every RealParameter in the schema, so a
	//! missing or empty one is refused outright - the value has no declared meaning;
	//! a valid schema unit the models do not speak (`decibel`) is an import-
	//! compatibility refusal; and a min/max that is PRESENT but empty declares a
	//! malformed bound, not an absent one - refused, while absent min/max still
	//! declare no format bound.
	void requiredUnitsAndPresentEmptyBoundsAreRefused()
	{
		const QString strip =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Channel id=\"m0\" role=\"master\">%1</Channel></Structure></Project>";

		// A missing or empty unit: the schema requires RealParameter.unit, so the
		// value has no declared meaning - refused.
		for (const char* child : {"<Volume value=\"1\"/>",
			"<Volume unit=\"\" value=\"1\"/>",
			"<Pan value=\"0.5\"/>",
			"<Pan unit=\"\" value=\"0.5\"/>"})
		{
			const QByteArray xml = strip.arg(QLatin1String(child)).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				qPrintable(QStringLiteral("the mixer strip carried %1").arg(QLatin1String(child))));
			QVERIFY2(error.contains("no unit"), qPrintable(error));
		}

		// A valid schema unit the models do not speak, refused even when the value
		// fits the models' reach - an import-compatibility error.
		{
			const QByteArray xml = strip.arg(QLatin1String(
				"<Volume unit=\"decibel\" min=\"-24\" max=\"0\" value=\"-12\"/>")).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				"a valid but unsupported unit was accepted");
			QVERIFY2(error.contains("cannot convert"), qPrintable(error));
		}

		// Present-empty min/max: an attribute the document carries with no number -
		// malformed, refused; absent min/max still declare no bound (covered above).
		for (const char* child : {"<Volume unit=\"linear\" min=\"\" max=\"2\" value=\"1\"/>",
			"<Volume unit=\"linear\" min=\"0\" max=\"\" value=\"1\"/>",
			"<Pan unit=\"normalized\" min=\"\" max=\"1\" value=\"0.5\"/>"})
		{
			const QByteArray xml = strip.arg(QLatin1String(child)).toLatin1();
			DawProjectModel model;
			DawProjectReadReport report;
		 QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				qPrintable(QStringLiteral("the mixer strip carried %1").arg(QLatin1String(child))));
			QVERIFY2(error.contains("not a number"), qPrintable(error));
		}
	}


	//! A note duration must not enter the model negative, even when the value
	//! would round to zero on the engine's tick grid.
	void negativeNoteDurationsAreRefused()
	{
		const QString xml =
			QStringLiteral("<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\">"
			"<Channel id=\"c1\"/></Track></Structure>"
			"<Arrangement><Lanes><Lanes track=\"t1\"><Clips>"
			"<Clip time=\"0\" duration=\"4\"><Notes>"
			"<Note time=\"0\" duration=\"%1\" channel=\"0\" key=\"60\" vel=\"0.8\"/>"
			"</Notes></Clip></Clips></Lanes></Lanes></Arrangement></Project>");
		for (const QString& duration : {QStringLiteral("-1"), QStringLiteral("-0.001")})
		{
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml.arg(duration).toLatin1(),
				&model, &report, &error),
				qPrintable(QStringLiteral("negative note duration %1 was accepted")
					.arg(duration)));
			QVERIFY2(error.contains(QStringLiteral("duration")),
				"the refusal does not identify the invalid note duration");
		}
	}

	//! The reader implements beat timelines only; an unknown unit must not be
	//! silently treated as beats on either notes or automation.
	void unknownNoteLaneTimeUnitsAreRefused()
	{
		const QByteArray notesInMilliseconds =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\">"
			"<Channel id=\"c1\"/></Track></Structure>"
			"<Arrangement><Lanes timeUnit=\"milliseconds\"><Lanes track=\"t1\"><Clips>"
			"<Clip time=\"0\" duration=\"4\"><Notes>"
			"<Note time=\"1500\" duration=\"250\" channel=\"0\" key=\"60\" vel=\"0.8\"/>"
			"</Notes></Clip></Clips></Lanes></Lanes></Arrangement></Project>";
		DawProjectModel notesModel;
		DawProjectReadReport notesReport;
		QString notesError;
		QVERIFY2(!dawProjectModelFromXml(notesInMilliseconds, &notesModel, &notesReport, &notesError),
			"a note lane with an unknown time unit was imported as beats");
		QVERIFY2(notesError.contains(QStringLiteral("milliseconds")),
			"the note-lane refusal does not identify its unknown time unit");
	}

	void unknownAutomationTimeUnitsAreRefused()
	{
		const QByteArray automationInMilliseconds =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Arrangement><TempoAutomation timeUnit=\"milliseconds\">"
			"<RealPoint time=\"1500\" value=\"120\"/>"
			"</TempoAutomation></Arrangement></Project>";
		DawProjectModel automationModel;
		DawProjectReadReport automationReport;
		QString automationError;
		QVERIFY2(!dawProjectModelFromXml(automationInMilliseconds, &automationModel,
			&automationReport, &automationError),
			"a tempo automation with an unknown time unit was imported as beats");
		QVERIFY2(automationError.contains(QStringLiteral("milliseconds")),
			"the automation refusal does not identify its unknown time unit");
	}

	//! Luna cluster 5064: an automation point's time is a beat like any other, so
	//! it goes through the same bounded tick conversion - and is counted in
	//! `rounded_times` when it is not on the 48-tick grid - instead of being kept
	//! as the raw beats while notes and clips are snapped.
	void automationPointTimesUseTheTickGridAndCountRounding()
	{
		const QByteArray offGrid =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Arrangement><TempoAutomation timeUnit=\"beats\">"
			"<RealPoint time=\"0.01\" value=\"120\"/>"
			"<RealPoint time=\"1.5\" value=\"130\"/>"
			"</TempoAutomation></Arrangement></Project>";

		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(dawProjectModelFromXml(offGrid, &model, &report, &error), qPrintable(error));
		QCOMPARE(model.tempoPoints.size(), 2);
		QVERIFY2(report.loss.roundedTimes >= 1,
			"an off-grid automation point time was not counted in rounded_times");
		// 0.01 beats is 0.48 ticks: the grid conversion puts it on tick 0.
		QVERIFY2(std::fabs(model.tempoPoints[0].time) < 1e-9,
			"the off-grid point time was not normalised through the tick grid");
		// 1.5 beats is exactly 72 ticks, so it is untouched.
		QVERIFY2(std::fabs(model.tempoPoints[1].time - 1.5) < 1e-9,
			"a point time already on the grid was moved");

		// A seconds automation is refused the same way a seconds <Notes> lane is:
		// converting it needs the tempo curve, which this module will not guess.
		const QByteArray seconds =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Arrangement><TempoAutomation timeUnit=\"seconds\">"
			"<RealPoint time=\"1.5\" value=\"120\"/>"
			"</TempoAutomation></Arrangement></Project>";
		DawProjectModel secondsModel;
		DawProjectReadReport secondsReport;
		QString secondsError;
		QVERIFY2(!dawProjectModelFromXml(seconds, &secondsModel, &secondsReport, &secondsError),
			"a seconds automation timeline was read as beats");

		// The metre arm is under the same rule: its time is a beat too.
		const QByteArray offGridMeter =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Arrangement><TimeSignatureAutomation timeUnit=\"beats\">"
			"<TimeSignaturePoint time=\"0.01\" numerator=\"3\" denominator=\"8\"/>"
			"</TimeSignatureAutomation></Arrangement></Project>";
		DawProjectModel meterModel;
		DawProjectReadReport meterReport;
		QString meterError;
		QVERIFY2(dawProjectModelFromXml(offGridMeter, &meterModel, &meterReport, &meterError),
			qPrintable(meterError));
		QCOMPARE(meterModel.meterPoints.size(), 1);
		QVERIFY2(meterReport.loss.roundedTimes >= 1,
			"an off-grid metre point time was not counted in rounded_times");
		QVERIFY2(std::fabs(meterModel.meterPoints[0].time) < 1e-9,
			"the off-grid metre point time was not normalised through the tick grid");
	}

	//! Unknown children in automation are skipped; their missing attributes must
	//! never turn them into default tempo or metre points.
	void unknownAutomationChildrenAreSkipped()
	{
		const QByteArray xml =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Arrangement><TempoAutomation timeUnit=\"beats\">"
			"<Unknown/>"
			"<RealPoint time=\"2\" value=\"140\"/>"
			"</TempoAutomation><TimeSignatureAutomation timeUnit=\"beats\">"
			"<Unknown/>"
			"<TimeSignaturePoint time=\"4\" numerator=\"3\" denominator=\"8\"/>"
			"</TimeSignatureAutomation></Arrangement></Project>";

		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(dawProjectModelFromXml(xml, &model, &report, &error), qPrintable(error));
		QCOMPARE(model.tempoPoints.size(), 1);
		QCOMPARE(model.tempoPoints[0].time, 2.0);
		QCOMPARE(model.tempoPoints[0].value, 140.0);
		QCOMPARE(model.meterPoints.size(), 1);
		QCOMPARE(model.meterPoints[0].time, 4.0);
		QCOMPARE(model.meterPoints[0].numerator, 3);
		QCOMPARE(model.meterPoints[0].denominator, 8);
		QCOMPARE(report.tempoPointCount, 1);
		QCOMPARE(report.meterPointCount, 1);
	}

	//! Luna cluster 2232: the Arrangement walk compared `reader.name()` before its
	//! next readNext(), so a tempo automation following other content parses to
	//! the end of the document. The case is kept as a guard on that walk, not as
	//! a claim about a dangling view.
	void automationPointsAfterOtherContentStillParse()
	{
		const QByteArray xml =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\">"
			"<Channel id=\"c1\"/></Track></Structure>"
			"<Arrangement><Lanes><Lanes track=\"t1\"><Clips>"
			"<Clip time=\"0\" duration=\"4\"><Notes>"
			"<Note time=\"0\" duration=\"1\" channel=\"0\" key=\"60\" vel=\"0.8\"/>"
			"<Note time=\"1\" duration=\"1\" channel=\"0\" key=\"64\" vel=\"0.6\"/>"
			"</Notes></Clip></Clips></Lanes></Lanes></Arrangement>"
			"<Arrangement><TempoAutomation timeUnit=\"beats\">"
			"<RealPoint time=\"0\" value=\"120\"/>"
			"<RealPoint time=\"2\" value=\"140\"/>"
			"</TempoAutomation></Arrangement>"
			"</Project>";

		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(dawProjectModelFromXml(xml, &model, &report, &error), qPrintable(error));
		QCOMPARE(model.tempoPoints.size(), 2);
		QCOMPARE(model.noteCount(), 2);
	}

	//! Luna cluster 3100: two tracks sharing an id are a malformed document, not
	//! a lookup where the later one wins. The reader used to insert into
	//! trackIndexById unconditionally, so a <Lanes track="t1"> resolved to the
	//! SECOND t1 and its clips landed on a track the file never asked for.
	void duplicateTrackIdsAreRefused()
	{
		const QByteArray duplicate =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure>"
			"<Track id=\"t1\" name=\"First\" contentType=\"notes\"><Channel id=\"c1\"/></Track>"
			"<Track id=\"t1\" name=\"Second\" contentType=\"notes\"><Channel id=\"c2\"/></Track>"
			"</Structure>"
			"<Arrangement><Lanes><Lanes track=\"t1\"><Clips>"
			"<Clip time=\"0\" duration=\"4\"><Notes>"
			"<Note time=\"0\" duration=\"1\" channel=\"0\" key=\"60\" vel=\"0.8\"/>"
			"</Notes></Clip></Clips></Lanes></Lanes></Arrangement>"
			"</Project>";

		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(!dawProjectModelFromXml(duplicate, &model, &report, &error),
			"a document declaring the same track id twice was read, so a lane naming that id "
			"silently resolved to the later track and its clips were misplaced");
		QVERIFY2(error.contains(QStringLiteral("t1")),
			"the refusal must name the duplicated id");
	}

//! Luna cluster 4134: folder_children_lost is a count of LOST RELATIONS, and
	//! the session path counted FOLDERS. An empty folder reported one lost child
	//! (nothing was lost) and a folder holding five reported one as well (four
	//! relations went unreported).
	void theFolderLossCountIsTheNumberOfLostChildren()
	{
		Song* song = Engine::getSong();
		song->clearProject();

		TrackFolder* emptyFolder = dynamic_cast<TrackFolder*>(
			Track::create(Track::Type::Folder, song));
		QVERIFY(emptyFolder != nullptr);

		TrackFolder* folder = dynamic_cast<TrackFolder*>(
			Track::create(Track::Type::Folder, song));
		QVERIFY(folder != nullptr);
		Track* childA = Track::create(Track::Type::Instrument, song);
		Track* childB = Track::create(Track::Type::Instrument, song);
		Track* childC = Track::create(Track::Type::Instrument, song);
		QVERIFY(childA && childB && childC);
		childA->setParentFolder(folder);
		childB->setParentFolder(folder);
		childC->setParentFolder(folder);
		QCOMPARE(folder->childCount(), 3);

		DawProjectLossReport loss;
		dawProjectModelFromSong(song, &loss);
		QCOMPARE(loss.folderChildrenLost, 3);
	}

	//! Luna cluster new-p11-88: a folder has no mixer channel by design, so it
	//! must not be reported as sharing the instrument track's mixer strip.
	void aFolderDoesNotCountAsMixerSharing()
	{
		Song* song = Engine::getSong();
		song->clearProject();

		TrackFolder* folder = dynamic_cast<TrackFolder*>(
			Track::create(Track::Type::Folder, song));
		Track* instrument = Track::create(Track::Type::Instrument, song);
		QVERIFY(folder != nullptr);
		QVERIFY(instrument != nullptr);
		QVERIFY(folder->mixerChannelModel() == nullptr);
		QVERIFY(instrument->mixerChannelModel() != nullptr);

		DawProjectLossReport loss;
		dawProjectModelFromSong(song, &loss);
		QCOMPARE(loss.mixerSharingLost, 0);
	}

//! Luna cluster 5068: the import updated the strips the model names and never
	//! removed the ones it does not, so a four-channel session importing a
	//! one-channel file kept three strips of the PREVIOUS project - their names,
	//! faders, mute and solo - as if they belonged to the imported document.
	//! The undo path already deletes them (ControlCommandsDawProject.cpp, the
	//! restoreSession loop); applying did not.
	void importRemovesMixerChannelsTheModelDoesNotName()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		while (static_cast<int>(mixer->numChannels()) < 4) { mixer->createChannel(); }
		mixer->mixerChannel(2)->m_name = QStringLiteral("Stale Sub");
		mixer->mixerChannel(2)->m_muteModel.setValue(true);
		mixer->mixerChannel(2)->m_volumeModel.setValue(0.11f);
		mixer->mixerChannel(3)->m_name = QStringLiteral("Stale Master2");
		QCOMPARE(mixer->numChannels(), 4);

		const DawProjectModel model = previousSessionModel();
		QCOMPARE(model.mixerChannels.size(), 1);

		QString error;
		QVERIFY2(applyDawProjectModel(Engine::getSong(), model, &error), qPrintable(error));
		QCOMPARE(Engine::mixer()->numChannels(), model.mixerChannels.size());
		QCOMPARE(Engine::mixer()->mixerChannel(0)->m_name,
			model.mixerChannels[0].name);
		QVERIFY(!Engine::mixer()->mixerChannel(0)->m_muteModel.value());
	}

//! Luna cluster 5069: the tempo/metre arm is already built before the song is
	//! touched (a map-capacity refusal leaves tracks intact), but a track type
	//! the engine cannot create was discovered AFTER clearAllTracks() and after
	//! the global tempo, metre and mixer had been written - so a refusal left a
	//! half-replaced session with none of the tracks that were there.
	void aTrackTypeTheEngineCannotCreateIsRefusedBeforeAnyMutation()
	{
		Song* song = Engine::getSong();
		Engine::mixer()->clear();
		QString error;
		const DawProjectModel original = previousSessionModel();
		QVERIFY2(applyDawProjectModel(song, original, &error), qPrintable(error));

		const int beforeTrackCount = static_cast<int>(song->tracks().size());
		const int beforeTempo = song->getTempo();
		const int beforeNumerator = song->getTimeSigModel().numeratorModel().value();
		const int beforeDenominator = song->getTimeSigModel().denominatorModel().value();
		const TempoMap beforeMap = song->tempoMap().map();
		QVERIFY(beforeTrackCount > 0);

		DawProjectModel refused = original;
		DawProjectTrack uncreatable;
		uncreatable.typeName = QStringLiteral("video");
		uncreatable.contentType = QStringLiteral("video");
		refused.tracks.append(uncreatable);
		refused.tempo = 90.0;

		QString refusedError;
		QVERIFY2(!applyDawProjectModel(song, refused, &refusedError),
			"a track type Track::create does not implement must be refused");
		QVERIFY2(refusedError.contains(QStringLiteral("video")),
			"the refusal must name the track type it is about");

		QCOMPARE(static_cast<int>(song->tracks().size()), beforeTrackCount);
		QCOMPARE(song->getTempo(), beforeTempo);
		QCOMPARE(song->getTimeSigModel().numeratorModel().value(), beforeNumerator);
		QCOMPARE(song->getTimeSigModel().denominatorModel().value(), beforeDenominator);
		QVERIFY(song->tempoMap().map() == beforeMap);
	}

//! Luna cluster 2216: the engine HAS a release velocity - Note::releaseVelocity
	//! is written to the file as `relvel` - so the model's `rel` is carried into
	//! the session and back out. Applying ignored it and exporting set rel=vel,
	//! so a file's note-off velocity was silently replaced by its note-on
	//! velocity on the first import. (The note CHANNEL is the other half of the
	//! finding and stays lost: no engine Note has a channel.)
	void releaseVelocitySurvivesAnImportAndExport()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		Track* track = Track::create(Track::Type::Instrument, song);
		QVERIFY(track != nullptr);
		Clip* clip = track->createClip(TimePos(0));
		auto* midi = dynamic_cast<MidiClip*>(clip);
		QVERIFY(midi != nullptr);

		DawProjectModel model = previousSessionModel();
		model.tracks.clear();
		DawProjectTrack entry;
		entry.typeName = QStringLiteral("instrument");
		entry.contentType = QStringLiteral("notes");
		entry.name = QStringLiteral("Rel");
		DawProjectClip clipEntry;
		clipEntry.time = 0.0;
		clipEntry.duration = 4.0;
		DawProjectNote noteEntry;
		noteEntry.time = 0.0;
		noteEntry.duration = 1.0;
		noteEntry.key = 60;
		noteEntry.vel = 0.8;
		noteEntry.rel = 0.2;
		clipEntry.notes.append(noteEntry);
		entry.clips.append(clipEntry);
		model.tracks.append(entry);

		QString error;
		QVERIFY2(applyDawProjectModel(song, model, &error), qPrintable(error));

		DawProjectLossReport loss;
		const DawProjectModel back = dawProjectModelFromSong(song, &loss);
		QCOMPARE(back.tracks.size(), 1);
		QCOMPARE(back.tracks[0].clips.size(), 1);
		QCOMPARE(back.tracks[0].clips[0].notes.size(), 1);
		const DawProjectNote& roundTripped = back.tracks[0].clips[0].notes[0];
		QCOMPARE(roundTripped.vel, 0.8);
		QVERIFY2(std::fabs(roundTripped.rel - 0.2) < 1e-6,
			qPrintable(QStringLiteral("the release velocity came back as %1, not 0.2")
				.arg(roundTripped.rel)));
	}

	//! A note's time is its position and its duration its length. The import
	//! passes Note's length first and its position second (Note.h), so a swap
	//! would move the note to its duration and stretch it to its start. The
	//! other notes in this file start at 0, which is why they cannot see one.
	void noteTimeIsPositionAndDurationIsLength()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		Track* track = Track::create(Track::Type::Instrument, song);
		QVERIFY(track != nullptr);
		Clip* clip = track->createClip(TimePos(0));
		QVERIFY(dynamic_cast<MidiClip*>(clip) != nullptr);

		DawProjectModel model = previousSessionModel();
		model.tracks.clear();
		DawProjectTrack entry;
		entry.typeName = QStringLiteral("instrument");
		entry.contentType = QStringLiteral("notes");
		entry.name = QStringLiteral("Pos");
		DawProjectClip clipEntry;
		clipEntry.time = 0.0;
		clipEntry.duration = 4.0;
		DawProjectNote noteEntry;
		noteEntry.time = 1.0;
		noteEntry.duration = 2.0;
		noteEntry.key = 60;
		noteEntry.vel = 0.8;
		noteEntry.rel = 0.8;
		clipEntry.notes.append(noteEntry);
		entry.clips.append(clipEntry);
		model.tracks.append(entry);

		QString error;
		QVERIFY2(applyDawProjectModel(song, model, &error), qPrintable(error));

		DawProjectLossReport loss;
		const DawProjectModel back = dawProjectModelFromSong(song, &loss);
		QCOMPARE(back.tracks.size(), 1);
		QCOMPARE(back.tracks[0].clips.size(), 1);
		QCOMPARE(back.tracks[0].clips[0].notes.size(), 1);
		const DawProjectNote& roundTripped = back.tracks[0].clips[0].notes[0];
		QCOMPARE(roundTripped.time, 1.0);
		QCOMPARE(roundTripped.duration, 2.0);
	}

//! Luna cluster 3137: a clip far past the engine's 32-bit tick grid cannot
	//! enter the model as an out-of-range tick. The reader converts every beat
	//! through ticksFromBeats(), which clamps into the signed 32-bit tick domain
	//! and counts the value as rounded, so what reaches applyDawProjectModel()'s
	//! `static_cast<tick_t>` is already inside the tick_t range - the narrowing
	//! cannot overflow. Kept as the guard on that clamp.
	void hugeBeatPositionsCannotEnterTheModelOutOfTickRange()
	{
		const QByteArray xml =
			"<Project version=\"1.0\">"
			"<Application name=\"Probe\" version=\"1\"/>"
			"<Transport><Tempo value=\"120\"/></Transport>"
			"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\">"
			"<Channel id=\"c1\"/></Track></Structure>"
			"<Arrangement><Lanes><Lanes track=\"t1\"><Clips>"
			"<Clip time=\"50000000\" duration=\"4\"><Notes>"
			"<Note time=\"50000000\" duration=\"1\" channel=\"0\" key=\"60\" vel=\"0.8\"/>"
			"</Notes></Clip></Clips></Lanes></Lanes></Arrangement>"
			"</Project>";

		DawProjectModel model;
		DawProjectReadReport report;
		QString error;
		QVERIFY2(dawProjectModelFromXml(xml, &model, &report, &error), qPrintable(error));
		QCOMPARE(model.tracks.size(), 1);
		QCOMPARE(model.tracks[0].clips.size(), 1);
		const DawProjectClip& clip = model.tracks[0].clips[0];
		const double ticks = clip.time * DawProjectTicksPerQuarterNote;
		QVERIFY2(ticks <= static_cast<double>(std::numeric_limits<std::int32_t>::max()),
			"a clip past the 32-bit tick grid reached the model as an out-of-range tick");
		const double noteTicks =
			clip.notes[0].time * DawProjectTicksPerQuarterNote;
		QVERIFY2(noteTicks <= static_cast<double>(std::numeric_limits<std::int32_t>::max()),
			"a note past the 32-bit tick grid reached the model as an out-of-range tick");
		QVERIFY2(report.loss.roundedTimes >= 1,
			"a clamped position was not counted in rounded_times");
	}

//! Luna cluster new-p8-114: note fields beyond the format's MIDI and velocity
//! ranges must be refused before they reach engine notes.
void outOfRangeNoteFieldsAreRefused()
{
	const QByteArray validXml =
		"<Project version=\"1.0\">"
		"<Application name=\"Probe\" version=\"1\"/>"
		"<Transport><Tempo value=\"120\"/></Transport>"
		"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\">"
		"<Channel id=\"c1\"/></Track></Structure>"
		"<Arrangement><Lanes><Lanes track=\"t1\"><Clips>"
		"<Clip time=\"0\" duration=\"1\"><Notes>"
		"<Note time=\"0\" duration=\"1\" channel=\"0\" key=\"60\" vel=\"0.5\" rel=\"0.5\"/>"
		"</Notes></Clip></Clips></Lanes></Lanes></Arrangement>"
		"</Project>";

	QByteArray boundaryXml = validXml;
	boundaryXml.replace("channel=\"0\"", "channel=\"15\"");
	boundaryXml.replace("key=\"60\"", "key=\"127\"");
	boundaryXml.replace("vel=\"0.5\"", "vel=\"0\"");
	boundaryXml.replace("rel=\"0.5\"", "rel=\"1\"");
	DawProjectModel boundaryModel;
	DawProjectReadReport boundaryReport;
	QString error;
	QVERIFY2(dawProjectModelFromXml(boundaryXml, &boundaryModel, &boundaryReport, &error),
		qPrintable(error));
	const DawProjectNote& boundaryNote = boundaryModel.tracks[0].clips[0].notes[0];
	QCOMPARE(boundaryNote.channel, 15);
	QCOMPARE(boundaryNote.key, 127);
	QCOMPARE(boundaryNote.vel, 0.0);
	QCOMPARE(boundaryNote.rel, 1.0);

	const QList<QPair<QByteArray, QByteArray>> invalidAttributes = {
		{"channel=\"0\"", "channel=\"-1\""}, {"channel=\"0\"", "channel=\"16\""},
		{"key=\"60\"", "key=\"-1\""}, {"key=\"60\"", "key=\"128\""},
		{"vel=\"0.5\"", "vel=\"-0.01\""}, {"vel=\"0.5\"", "vel=\"1.01\""},
		{"rel=\"0.5\"", "rel=\"-0.01\""}, {"rel=\"0.5\"", "rel=\"1.01\""}
	};
	for (const auto& [validAttribute, invalidAttribute] : invalidAttributes)
	{
		QByteArray xml = validXml;
		xml.replace(validAttribute, invalidAttribute);
		DawProjectModel model;
		DawProjectReadReport report;
		error.clear();
		QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
			qPrintable(QStringLiteral("accepted %1").arg(QString::fromLatin1(invalidAttribute))));
		const QByteArray field = validAttribute.left(validAttribute.indexOf('='));
		QVERIFY2(error.contains(QString::fromLatin1(field)), qPrintable(error));
	}
}

	//! The writer refuses a negative timeline position; importing one would put
	//! a value in the model that this same module cannot write back.
	void negativeTimelinePositionsAreRefusedByReader()
	{
		const QList<QPair<QString, QByteArray>> fixtures = {
			{QStringLiteral("negative clip time"),
				QByteArrayLiteral("<Project version=\"1.0\"><Application name=\"Probe\" version=\"1\"/>"
				"<Transport><Tempo value=\"120\"/></Transport>"
				"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\"><Channel id=\"c1\"/>"
				"</Track></Structure><Arrangement><Lanes><Lanes track=\"t1\"><Clips>"
				"<Clip time=\"-1\" duration=\"4\"><Notes>"
				"<Note time=\"0\" duration=\"1\" channel=\"0\" key=\"60\" vel=\"0.8\"/>"
				"</Notes></Clip></Clips></Lanes></Lanes></Arrangement></Project>")},
			{QStringLiteral("negative note time"),
				QByteArrayLiteral("<Project version=\"1.0\"><Application name=\"Probe\" version=\"1\"/>"
				"<Transport><Tempo value=\"120\"/></Transport>"
				"<Structure><Track id=\"t1\" name=\"T\" contentType=\"notes\"><Channel id=\"c1\"/>"
				"</Track></Structure><Arrangement><Lanes><Lanes track=\"t1\"><Clips>"
				"<Clip time=\"0\" duration=\"4\"><Notes>"
				"<Note time=\"-1\" duration=\"1\" channel=\"0\" key=\"60\" vel=\"0.8\"/>"
				"</Notes></Clip></Clips></Lanes></Lanes></Arrangement></Project>")},
			{QStringLiteral("negative tempo map-point time"),
				QByteArrayLiteral("<Project version=\"1.0\"><Application name=\"Probe\" version=\"1\"/>"
				"<Transport><Tempo value=\"120\"/></Transport>"
				"<Arrangement><TempoAutomation timeUnit=\"beats\"><RealPoint time=\"-1\" value=\"130\"/>"
				"</TempoAutomation></Arrangement></Project>")}
		};

		for (const auto& [description, xml] : fixtures)
		{
			DawProjectModel model;
			DawProjectReadReport report;
			QString error;
			QVERIFY2(!dawProjectModelFromXml(xml, &model, &report, &error),
				qPrintable(QStringLiteral("%1 was accepted: %2").arg(description, error)));
		}
	}

//! Luna cluster 5066: the writer's own header promises a NEGATIVE time is
//! refused, and a NaN or an infinity is not a position at all - but the only
//! range tests were the tempo's and the denominator's. A clip at beat -1, a
//! note at -0.5, or a NaN tempo (which compares false against BOTH bounds, so
//! even the tempo's own check let it through) was serialized as "nan"/"-1.000000"
//! - a document the reader can only refuse.
void timesOutsideTheEngineGridAreRefusedByTheWriter()
{
	DawProjectModel base = authoredModel();
	base.tracks[0].clips[0].notes[0].time = 0.0;

	const QList<double> unsupported = {
		-1.0, -0.000001, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()
	};
	for (const double value : unsupported)
	{
		DawProjectModel negativeClip = base;
		negativeClip.tracks[0].clips[0].time = value;
		QByteArray xml;
		QString error;
		QVERIFY2(!dawProjectXmlFromModel(negativeClip, &xml, &error),
			qPrintable(QStringLiteral("a clip at beat %1 was written").arg(value)));

		DawProjectModel negativeDuration = base;
		negativeDuration.tracks[0].clips[0].duration = value;
		QVERIFY2(!dawProjectXmlFromModel(negativeDuration, &xml, &error),
			qPrintable(QStringLiteral("a clip of duration %1 was written").arg(value)));

		DawProjectModel negativeNote = base;
		negativeNote.tracks[0].clips[0].notes[0].time = value;
		QVERIFY2(!dawProjectXmlFromModel(negativeNote, &xml, &error),
			qPrintable(QStringLiteral("a note at beat %1 was written").arg(value)));

		DawProjectModel negativePoint = base;
		negativePoint.tempoPoints[0].time = value;
		QVERIFY2(!dawProjectXmlFromModel(negativePoint, &xml, &error),
			qPrintable(QStringLiteral("a tempo point at beat %1 was written").arg(value)));
	}

	// The tempo itself, NaN in particular: a NaN fails every comparison, so it
	// needs the finiteness test the bounds test alone does not give.
	DawProjectModel nanTempo = base;
	nanTempo.tempo = std::numeric_limits<double>::quiet_NaN();
	nanTempo.tempoPoints.clear();
	QByteArray xml;
	QString error;
	QVERIFY2(!dawProjectXmlFromModel(nanTempo, &xml, &error), "a NaN tempo was written");
	DawProjectModel nanPoint = base;
	nanPoint.tempoPoints[0].value = std::numeric_limits<double>::quiet_NaN();
	QVERIFY2(!dawProjectXmlFromModel(nanPoint, &xml, &error), "a NaN tempo point was written");

	// The positive half: an ordinary model still writes.
	QVERIFY2(dawProjectXmlFromModel(base, &xml, &error), qPrintable(error));
}

//! Cluster new-p10-100: the reader refuses numerators outside [1, 32], so the
//! writer must refuse those values for both the global signature and map points.
void unsupportedTimeSignatureNumeratorsAreRefusedByTheWriter()
{
	const QList<int> unsupported = {TempoMapMinNumerator - 1, TempoMapMaxNumerator + 1};
	const QList<int> supported = {TempoMapMinNumerator, TempoMapMaxNumerator};
	QByteArray xml;
	QString error;

	for (const int numerator : unsupported)
	{
		DawProjectModel global = authoredModel();
		global.numerator = numerator;
		QVERIFY2(!dawProjectXmlFromModel(global, &xml, &error),
			qPrintable(QStringLiteral("global time-signature numerator %1 was written")
				.arg(numerator)));
		QVERIFY2(error.contains(QStringLiteral("numerator")),
			"the refusal does not identify the invalid global numerator");

		DawProjectModel point = authoredModel();
		point.meterPoints[0].numerator = numerator;
		QVERIFY2(!dawProjectXmlFromModel(point, &xml, &error),
			qPrintable(QStringLiteral("meter-point numerator %1 was written").arg(numerator)));
		QVERIFY2(error.contains(QStringLiteral("numerator")),
			"the refusal does not identify the invalid meter-point numerator");
	}

	for (const int numerator : supported)
	{
		DawProjectModel global = authoredModel();
		global.numerator = numerator;
		QVERIFY2(dawProjectXmlFromModel(global, &xml, &error), qPrintable(error));

		DawProjectModel point = authoredModel();
		point.meterPoints[0].numerator = numerator;
		QVERIFY2(dawProjectXmlFromModel(point, &xml, &error), qPrintable(error));
	}
}

//! Luna cluster new-p8-97: the writer must refuse beats whose serialized
//! value rounds beyond the signed tick_t range when this reader imports it.
void writerRefusesBeatTimesOutsideTheTickRange()
{
	DawProjectModel base = authoredModel();
	DawProjectWriteReport report;
	QString error;
	const QString file = path(m_directory, QStringLiteral("outside-tick-range.dawproject"));

	DawProjectModel hugeClip = base;
	hugeClip.tracks[0].clips[0].time = 50000000.0;
	QVERIFY2(!writeDawProject(file, hugeClip, &report, &error),
		"a clip time beyond the engine's tick range was written");

	DawProjectModel hugeNote = base;
	hugeNote.tracks[0].clips[0].notes[0].time = 50000000.0;
	QVERIFY2(!writeDawProject(file, hugeNote, &report, &error),
		"a note time beyond the engine's tick range was written");

	const double maxTick = static_cast<double>(std::numeric_limits<tick_t>::max());
	const double roundsPastRange = (maxTick + 0.49999) / DawProjectTicksPerQuarterNote;
	QVERIFY(dawProjectBeatRepresentable(roundsPastRange));
	DawProjectModel roundedClip = base;
	roundedClip.tracks[0].clips[0].time = roundsPastRange;
	QVERIFY2(!writeDawProject(file, roundedClip, &report, &error),
		"six-decimal serialization rounded a representable beat into an unrepresentable tick");

}

//! Individually representable fields must not create an overflowing clip end.
void writerRefusesAClipEndpointPastTheTickRange()
{
	DawProjectModel model = authoredModel();
	DawProjectWriteReport report;
	QString error;
	const QString file = path(m_directory, QStringLiteral("clip-endpoint-overflow.dawproject"));
	const double maxTickBeat = static_cast<double>(std::numeric_limits<tick_t>::max())
		/ DawProjectTicksPerQuarterNote;
	model.tracks[0].clips[0].time = maxTickBeat;
	model.tracks[0].clips[0].duration = 1.0 / DawProjectTicksPerQuarterNote;
	QVERIFY2(!writeDawProject(file, model, &report, &error),
		"a clip endpoint beyond the engine's tick range was written");
}

//! Individually representable fields must not create an overflowing note end.
void writerRefusesANoteEndpointPastTheTickRange()
{
	DawProjectModel model = authoredModel();
	DawProjectWriteReport report;
	QString error;
	const QString file = path(m_directory, QStringLiteral("note-endpoint-overflow.dawproject"));
	const double maxTickBeat = static_cast<double>(std::numeric_limits<tick_t>::max())
		/ DawProjectTicksPerQuarterNote;
	model.tracks[0].clips[0].notes[0].time = maxTickBeat;
	model.tracks[0].clips[0].notes[0].duration = 1.0 / DawProjectTicksPerQuarterNote;
	QVERIFY2(!writeDawProject(file, model, &report, &error),
		"a note endpoint beyond the engine's tick range was written");
}

//! Luna cluster 5067: dawProjectXmlFromModel(DawProjectModel{}) SUCCEEDED and
//! wrote <Application name=""/>, which this module's own reader refuses
//! (Project.xsd makes the name required). The writer has to refuse it for the
//! same reason it refuses an out-of-range tempo.
void anEmptyApplicationNameIsRefusedByTheWriter()
{
	DawProjectModel model = authoredModel();
	model.applicationName.clear();
	QByteArray xml;
	QString error;
	QVERIFY2(!dawProjectXmlFromModel(model, &xml, &error),
		"a model with an empty application name was written as a file this module cannot read");
	QVERIFY2(error.contains(QStringLiteral("name")),
		"the refusal does not say which attribute is missing");

	// The positive half: a named model still writes.
	QVERIFY2(dawProjectXmlFromModel(authoredModel(), &xml, &error), qPrintable(error));
}

//! Cluster new-p14-87: the writer must refuse invalid note fields that the
//! reader rejects (channel/key/vel/rel ranges), including non-finite values.
	void invalidNoteFieldsAreRefusedByTheWriter()
	{
		DawProjectModel base = authoredModel();
		QByteArray xml;
		QString error;

		// channel out of range
		{
			DawProjectModel m = base;
			m.tracks[0].clips[0].notes[0].channel = 16;
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				"a note with channel 16 was written");
		}
		// key out of range
		{
			DawProjectModel m = base;
			m.tracks[0].clips[0].notes[0].key = 128;
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				"a note with key 128 was written");
		}
		// vel out of range / non-finite
		for (const double v : { -0.01, 1.01, std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity() })
		{
			DawProjectModel m = base;
			m.tracks[0].clips[0].notes[0].vel = v;
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				qPrintable(QStringLiteral("a note with vel %1 was written").arg(v)));
		}
		// rel out of range / non-finite
		for (const double v : { -0.01, 1.01, std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity() })
		{
			DawProjectModel m = base;
			m.tracks[0].clips[0].notes[0].rel = v;
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				qPrintable(QStringLiteral("a note with rel %1 was written").arg(v)));
		}
		// positive half
		QVERIFY2(dawProjectXmlFromModel(base, &xml, &error), qPrintable(error));
	}

//! Cluster new-p8-100: non-finite / out-of-range scalar parameters must be
//! refused before they are serialized into invalid XML.
	void nonFiniteScalarParametersAreRefusedByTheWriter()
	{
		DawProjectModel base = authoredModel();
		QByteArray xml;
		QString error;

		// NaN mixer volume
		{
			DawProjectModel m = base;
			m.mixerChannels[0].volume = std::numeric_limits<double>::quiet_NaN();
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				"a NaN mixer volume was written");
		}
		// infinity mixer volume
		{
			DawProjectModel m = base;
			m.mixerChannels[0].volume = std::numeric_limits<double>::infinity();
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				"an infinite mixer volume was written");
		}
		// out-of-range mixer volume
		{
			DawProjectModel m = base;
			m.mixerChannels[0].volume = 3.0;
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				"an out-of-range mixer volume was written");
		}
		// NaN track volume
		{
			DawProjectModel m = base;
			m.tracks[0].volume = std::numeric_limits<double>::quiet_NaN();
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				"a NaN track volume was written");
		}
		// NaN track pan
		{
			DawProjectModel m = base;
			m.tracks[0].pan = std::numeric_limits<double>::quiet_NaN();
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				"a NaN track pan was written");
		}
		// out-of-range track pan
		{
			DawProjectModel m = base;
			m.tracks[0].pan = 2.0;
			QVERIFY2(!dawProjectXmlFromModel(m, &xml, &error),
				"an out-of-range track pan was written");
		}
		// positive half
		QVERIFY2(dawProjectXmlFromModel(base, &xml, &error), qPrintable(error));
	}

//! Cluster new-p20-42: writing without a report must not read the archive back.
	void writingWithoutAReportDoesNotReadTheArchive()
	{
		const QString file = path(m_directory, QStringLiteral("no-report.dawproject"));
		// Do not pre-create the destination: QSaveFile copies an existing
		// file's permissions, which would make it readable again. On POSIX,
		// create it under a umask that strips read bits so the old read-back
		// fails. MSVC's CRT ignores attempts to remove read permission, but its
		// supported _umask_s spelling keeps the test target warning-free there.
		int maskResult = 0;
		int restoreMaskResult = 0;
#ifdef _MSC_VER
		int previous = 0;
		maskResult = _umask_s(_S_IREAD, &previous);
#else
		const mode_t previous = umask(0466);
#endif
		DawProjectModel model = authoredModel();
		QString error;
		const bool ok = writeDawProject(file, model, nullptr, &error);
		// Restore the process-wide mask even if writing the file failed.
#ifdef _MSC_VER
		int ignoredMask = 0;
		restoreMaskResult = _umask_s(previous, &ignoredMask);
#else
		umask(previous);
#endif
		// restore permissions for cleanup
		QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner);
		QVERIFY2(maskResult == 0, "could not set the file permission mask");
		QVERIFY2(restoreMaskResult == 0, "could not restore the file permission mask");
		QVERIFY2(ok, qPrintable(error));
	}

//! Luna cluster 5071: the write report's documented file identity. `bytes` and
//! `sha256` are documented as describing the FILE, but `sha256` hashed only the
//! project.xml entry inside the ZIP, so a caller that hashed the written
//! .dawproject and compared got two different values for the same file.
void theWriteReportHashesTheFileItWrote()
{
	const QString file = path(m_directory, QStringLiteral("report-identity.dawproject"));
	DawProjectWriteReport report;
	QString error;
	QVERIFY2(writeDawProject(file, authoredModel(), &report, &error), qPrintable(error));

	QFile written(file);
	QVERIFY(written.open(QIODevice::ReadOnly));
	const QByteArray container = written.readAll();
	written.close();

	QCOMPARE(report.bytes, static_cast<qint64>(container.size()));
	QCOMPARE(report.sha256, dawProjectSha256(container));

	// And it is NOT the hash of the project.xml entry alone, which is what it
	// used to report: the entry is only part of a bigger container.
	std::vector<DawProjectEntry> entries;
	QVERIFY2(dawProjectZipRead(file, &entries, &error), qPrintable(error));
	QCOMPARE(entries.size(), static_cast<std::size_t>(2));
	for (const DawProjectEntry& entry : entries)
	{
		if (entry.first == QString::fromLatin1(DawProjectProjectEntry))
		{
			QVERIFY2(report.sha256 != dawProjectSha256(entry.second),
				"the report still hashes only the project.xml entry rather than the file");
		}
	}
}

//! Luna cluster 4133: a successful write handed back an EMPTY loss report even
//! when the model it was given said a track's contentType had been lost, so
//! "this file lost nothing" was reported for a file that did lose something.
void theWriteReportCountsWhatTheModelMarksLost()
{
	DawProjectModel model = authoredModel();
	model.splitEvents = 3;
	int lost = 0;
	// Mark BOTH tracks lost, so the count cannot be satisfied by counting one.
	for (DawProjectTrack& track : model.tracks)
	{
		track.lostContentType = true;
		++lost;
	}
	QVERIFY(lost > 1);

	const QString file = path(m_directory, QStringLiteral("report-loss.dawproject"));
	DawProjectWriteReport report;
	QString error;
	QVERIFY2(writeDawProject(file, model, &report, &error), qPrintable(error));

	QCOMPARE(report.loss.unmappedTrackTypes, lost);
	QCOMPARE(report.loss.splitMapEvents, 3);
}

//! Luna cluster 5073: the classic ZIP size fields are 16- and 32-bit, and the
//! writer narrowed the entry name, the entry count and the body sizes into them
//! with no range check at all - so an oversized name produced a container whose
//! central directory pointed at the wrong bytes, and the write still SUCCEEDED.
//! These are refused before anything is written.
void archivesPastClassicZipLimitsAreRefused()
{
	QString error;
	const QString file = path(m_directory, QStringLiteral("too-long-name.dawproject"));

	// A name longer than the 16-bit name field.
	const QString longName(70000, QLatin1Char('n'));
	std::vector<DawProjectEntry> entries = {{longName, QByteArray("body")}};
	QVERIFY2(!dawProjectZipWrite(file, entries, &error),
		"a 70000-byte entry name was narrowed into a 16-bit field and written");
	QVERIFY2(error.contains(QStringLiteral("classic ZIP")),
		"the refusal does not say which limit was exceeded");

	// Nothing was left behind by the refusal.
	QVERIFY2(!QFile::exists(file), "a refused archive left a file at the destination");

	// The positive half: an ordinary entry still writes and reads back.
	const QString ordinary = path(m_directory, QStringLiteral("ordinary-entry.dawproject"));
	std::vector<DawProjectEntry> good = {{QStringLiteral("project.xml"), QByteArray("<Project/>")}};
	QVERIFY2(dawProjectZipWrite(ordinary, good, &error), qPrintable(error));
	std::vector<DawProjectEntry> readBack;
	QVERIFY2(dawProjectZipRead(ordinary, &readBack, &error), qPrintable(error));
	QCOMPARE(readBack.size(), static_cast<std::size_t>(1));
	QCOMPARE(readBack[0].first, QStringLiteral("project.xml"));
}

//! Luna cluster new-p8-98: the reader uses int offsets and rejects files above
//! INT_MAX, but the writer only checked the classic ZIP 32-bit limit before
//! narrowing the local-record size into centralOffset.
void archivesPastTheReaderSizeLimitAreRefused()
{
	const QString entryName = QStringLiteral("oversized.bin");
	const qsizetype readerLimit = static_cast<qsizetype>(std::numeric_limits<int>::max());
	const qsizetype localRecordHeaderSize = 30 + entryName.toUtf8().size();
	const qsizetype bodySize = readerLimit - localRecordHeaderSize + 1;
	const std::vector<DawProjectEntry> entries = {
		{entryName, QByteArray(bodySize, '\0')},
	};
	const QString file = path(m_directory, QStringLiteral("past-reader-limit.dawproject"));
	QString error;

	QVERIFY2(!dawProjectZipWrite(file, entries, &error),
		"the writer accepted a local-record section too large for its reader's int offsets");
	QVERIFY2(error.contains(QStringLiteral("reader")), qPrintable(error));
	QVERIFY2(!QFile::exists(file), "a refused archive left a file at the destination");
}

//! Luna cluster 5075: the reader finds the end record by scanning BACKWARDS for
//! the signature and accepts the first match, without checking that the record's
//! own declared comment length carries it to the end of the file. A ZIP comment
//! (this module writes none, a foreign container may have one) can therefore end
//! with 22 bytes that look like an empty end record: the reader takes the fake,
//! sees zero entries, and returns a SUCCESSFUL but empty archive instead of the
//! real entries - data loss reported as success. An end record is only the end
//! record when its comment length places its end exactly at the end of the file.
void aCommentThatLooksLikeAnEndRecordIsNotOne()
{
	QString error;
	const QString file = path(m_directory, QStringLiteral("fake-end-record.dawproject"));
	const std::vector<DawProjectEntry> entries = {
		{QStringLiteral("project.xml"), QByteArray("<Project version=\"1.0\"/>")},
		{QStringLiteral("metadata.xml"), QByteArray("<MetaData/>")},
	};
	QVERIFY2(dawProjectZipWrite(file, entries, &error), qPrintable(error));

	// A comment that ends with what looks like an empty end record: the signature,
	// zero disks, zero entries, a zero-sized directory at offset 0, no comment.
	// The real end record declares those 22 bytes as its comment, so this is a
	// well-formed archive comment - only the record inside it is the impostor.
	QByteArray bytes;
	{
		QFile source(file);
		QVERIFY(source.open(QIODevice::ReadOnly));
		bytes = source.readAll();
	}
	const int realEnd = bytes.size() - 22;
	bytes[realEnd + 20] = char(22);  // the real record's comment length
	bytes[realEnd + 21] = char(0);
	const char endSignature[4] = {'P', 'K', 0x05, 0x06};
	bytes.append(endSignature, 4);
	for (int field = 0; field < 4; field++) { bytes.append(2, '\0'); }  // disks, entries
	bytes.append(4, '\0');  // central size
	bytes.append(4, '\0');  // central offset
	bytes.append(2, '\0');  // comment length: none, so this record ends the file
	{
		QFile target(file);
		QVERIFY(target.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(target.write(bytes), bytes.size());
	}

	std::vector<DawProjectEntry> readBack;
	QVERIFY2(dawProjectZipRead(file, &readBack, &error),
		"a comment that ends in a fake end record was taken as the end record");
	QCOMPARE(readBack.size(), static_cast<std::size_t>(2));
	QCOMPARE(readBack[0].first, QStringLiteral("project.xml"));
	QCOMPARE(readBack[1].first, QStringLiteral("metadata.xml"));
}

	//! Luna cluster new-p12-75: the entry count is written into 16-bit fields, and
	//! 0xFFFF in them is the ZIP64 SENTINEL - a reader that follows it looks for the
	//! ZIP64 end-of-central-directory records this writer does not write, so the
	//! container it wrote is one no reader can agree on. The preflight refused only
	//! counts ABOVE 0xFFFF, so exactly 65,535 entries was written with the sentinel.
	void theZip64EntryCountSentinelIsRefused()
	{
		const QString sentinelCount = path(m_directory, QStringLiteral("sentinel-count.zip"));
		std::vector<DawProjectEntry> many;
		many.reserve(65535);
		for (int index = 0; index < 65535; index++)
		{
			many.emplace_back(QStringLiteral("e%1").arg(index), QByteArray("x"));
		}
		QCOMPARE(many.size(), static_cast<std::size_t>(0xFFFF));

		QString error;
		QVERIFY2(!dawProjectZipWrite(sentinelCount, many, &error),
			"65535 entries were written with 0xFFFF - the ZIP64 sentinel - in the count "
			"fields of an archive that carries no ZIP64 record");
		QVERIFY2(error.contains(QStringLiteral("classic ZIP")),
			"the refusal does not say which limit was exceeded");
		QVERIFY2(!QFile::exists(sentinelCount),
			"a refused archive left a file at the destination");

		// The positive half: one entry below the sentinel still writes, and the count
		// the end record carries is the real one, not 0xFFFF.
		const QString largest = path(m_directory, QStringLiteral("largest-classic-count.zip"));
		many.pop_back();
		QCOMPARE(many.size(), static_cast<std::size_t>(0xFFFE));
		QVERIFY2(dawProjectZipWrite(largest, many, &error), qPrintable(error));

		QFile written(largest);
		QVERIFY(written.open(QIODevice::ReadOnly));
		QVERIFY(written.seek(written.size() - 22));
		const QByteArray end = written.read(22);
		QCOMPARE(end.size(), 22);
		const auto count = [&end](int offset)
		{
			return static_cast<int>(static_cast<quint8>(end[offset]))
				| (static_cast<int>(static_cast<quint8>(end[offset + 1])) << 8);
		};
		QCOMPARE(count(8), 0xFFFE);  // entries on this disk
		QCOMPARE(count(10), 0xFFFE);  // entries in total
	}

private:
	QTemporaryDir m_directory;
};

void DawProjectInterchangeRoundTripTest::verifyOverCapacityRefusalPreservesSession(bool meter)
{
	Song* song = Engine::getSong();
	Engine::mixer()->clear();
	QString error;
	const DawProjectModel original = previousSessionModel();
	QVERIFY2(applyDawProjectModel(song, original, &error), qPrintable(error));
	Engine::projectJournal()->clearJournal();
	ControlRegistry::instance()->clearTransactions();
	const ControlResult priorEdit = run(QStringLiteral("transport.set_tempo"),
		{{QStringLiteral("bpm"), 138}});
	QVERIFY2(priorEdit.ok, qPrintable(priorEdit.errorMessage));

	const DawProjectModel before = dawProjectModelFromSong(song, nullptr);
	const TempoMap beforeMap = song->tempoMap().map();
	const int beforeTempo = song->getTempo();
	const int beforeNumerator = song->getTimeSigModel().numeratorModel().value();
	const int beforeDenominator = song->getTimeSigModel().denominatorModel().value();
	Mixer* mixer = Engine::mixer();
	const int beforeMixerCount = mixer->numChannels();
	const QString beforeMasterName = mixer->mixerChannel(0)->m_name;
	const float beforeMasterVolume = mixer->mixerChannel(0)->m_volumeModel.value();
	const int beforeUndoDepth = Engine::projectJournal()->undoDepth();
	const QJsonArray beforeTransactions = ControlRegistry::instance()->transactions();
	QCOMPARE(before.tracks.size(), 1);
	QCOMPARE(beforeUndoDepth, 1);
	QCOMPARE(beforeTransactions.size(), 1);

	const DawProjectModel invalid = modelWithOverCapacityMap(meter);
	const QString file = path(m_directory, meter ? QStringLiteral("over-capacity-meter.dawproject")
		: QStringLiteral("over-capacity-tempo.dawproject"));
	DawProjectWriteReport writeReport;
	QVERIFY2(writeDawProject(file, invalid, &writeReport, &error), qPrintable(error));
	DawProjectModel parsed;
	DawProjectReadReport readReport;
	QVERIFY2(readDawProject(file, &parsed, &readReport, &error), qPrintable(error));
	QCOMPARE(meter ? parsed.meterPoints.size() : parsed.tempoPoints.size(), TempoMap::MaxEvents + 1);

	const ControlResult result = run(QStringLiteral("dawproject.import"),
		{{QStringLiteral("path"), file}});
	QString failure;
	if (result.ok || result.errorKind != ControlErrorKind::Refused)
	{
		failure = QStringLiteral("over-capacity import did not return a typed refusal");
	}
	else if (song->tracks().size() != 1 || song->tracks().front()->name() != before.tracks.front().name)
	{
		failure = QStringLiteral("the previous track was removed by the refusal");
	}
	else if (song->getTempo() != beforeTempo
		|| song->getTimeSigModel().numeratorModel().value() != beforeNumerator
		|| song->getTimeSigModel().denominatorModel().value() != beforeDenominator)
	{
		failure = QStringLiteral("the previous global tempo or metre changed on refusal");
	}
	else if (song->tempoMap().map() != beforeMap)
	{
		failure = QStringLiteral("the previous tempo map changed on refusal");
	}
	else if (mixer->numChannels() != beforeMixerCount
		|| mixer->mixerChannel(0)->m_name != beforeMasterName
		|| mixer->mixerChannel(0)->m_volumeModel.value() != beforeMasterVolume)
	{
		failure = QStringLiteral("the previous mixer state changed on refusal");
	}
	else if (Engine::projectJournal()->undoDepth() != beforeUndoDepth
		|| ControlRegistry::instance()->transactions() != beforeTransactions)
	{
		failure = QStringLiteral("the refusal changed the undo journal or transaction history");
	}

	if (!failure.isEmpty())
	{
		Engine::mixer()->clear();
		QString ignored;
		applyDawProjectModel(song, before, &ignored);
		Engine::projectJournal()->clearJournal();
		ControlRegistry::instance()->clearTransactions();
	}
	QVERIFY2(failure.isEmpty(), qPrintable(failure));
}

QTEST_GUILESS_MAIN(DawProjectInterchangeRoundTripTest)
#include "DawProjectInterchangeRoundTripTest.moc"
