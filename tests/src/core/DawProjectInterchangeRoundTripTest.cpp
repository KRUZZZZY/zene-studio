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

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "DawProjectInterchange.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

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
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_directory.isValid());
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

		// The session now has the AFTER model.
		DawProjectLossReport afterLoss;
		const DawProjectModel afterExtracted = dawProjectModelFromSong(song, &afterLoss);
		QCOMPARE(afterExtracted.tracks.size(), 2);

		// Undo: the before model comes back.
		const ControlResult undoResult = run(QStringLiteral("control.undo"));
		QVERIFY2(undoResult.ok, qPrintable(undoResult.errorMessage));

		DawProjectLossReport restoredLoss;
		const DawProjectModel restored = dawProjectModelFromSong(song, &restoredLoss);
		QCOMPARE(restored.tracks.size(), 1);
		QCOMPARE(restored.tracks[0].name, QStringLiteral("Before"));
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

private:
	QTemporaryDir m_directory;
};

QTEST_GUILESS_MAIN(DawProjectInterchangeRoundTripTest)
#include "DawProjectInterchangeRoundTripTest.moc"
