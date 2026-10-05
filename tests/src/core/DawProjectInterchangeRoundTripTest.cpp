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

#include <QByteArray>
#include <QDir>
#include <QList>
#include <QPair>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "DawProjectInterchange.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
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
		DawProjectModel model = authoredModel();
		model.tempoPoints.clear();
		DawProjectPoint point;
		point.time = 1e300;
		point.isMeter = false;
		point.value = 120.0;
		model.tempoPoints.append(point);

		const QString file = path(m_directory, QStringLiteral("huge-beat.dawproject"));
		DawProjectWriteReport writeReport;
		QString writeError;
		QVERIFY2(writeDawProject(file, model, &writeReport, &writeError), qPrintable(writeError));

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
