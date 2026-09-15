/*
 * dawproject-roundtrip-proof.cpp - the DAWproject round trip that compares the
 *                                  MODEL, run without the Engine.
 *
 * Feature row 37 of docs/FEATURE-LIST-0.3.0.md. The registered proof is the
 * ctest DawProjectInterchangeRoundTripTest, which constructs a full Engine; this
 * probe measures the half that does NOT need one. The writer, the reader, the
 * container and the model have no engine dependency, so linking
 * DawProjectModel/Zip/Write/Read/ReadTracks is enough to write a real
 * .dawproject container and read it back.
 *
 * It authors the SAME model tests/src/core/DawProjectInterchangeRoundTripTest.cpp
 * authors, and compares the models with the model's own operator== - not the
 * file, not its hash. If a later change renumbers the document, breaks an IDREF
 * or moves a time by a tick, this exits non-zero and prints the two digests,
 * line by line, so the failure names what changed.
 *
 * Build and run it with tools/dawproject-proof.sh (it reads the flags out of
 * <build>/compile_commands.json, so the probe compiles with the project's own
 * flags and no hand-maintained include list).
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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "DawProjectInterchange.h"

using namespace lmms;
using namespace lmms::interchange;

static QTextStream out(stdout);

//! The model the ctest authors, verbatim - including the canonical lowercase
//! type names (LOSSY #10: the document carries the contentType and the name is
//! derived from it, so a fixture spelling it "Instrument" could not round-trip).
static DawProjectModel authoredModel()
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
	trackA.typeName = QStringLiteral("instrument");
	trackA.channelId = QStringLiteral("strip1");
	trackA.hasVolume = true;
	trackA.volume = 0.75;
	trackA.hasPan = true;
	trackA.pan = 0.4;
	trackA.destinationChannelId = QStringLiteral("mixer0");
	trackA.mixerChannelIndex = 0;

	DawProjectClip clipA;
	clipA.time = 0.0;
	clipA.duration = 4.0;
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

static int refusals(DawProjectModel* model)
{
	int failures = 0;

	// A file that does not exist.
	QString error;
	if (readDawProject(QDir::tempPath() + QStringLiteral("/dawp-proof-missing.dawproject"),
			model, nullptr, &error))
	{
		out << "FAIL: a missing file was accepted\n";
		failures++;
	}
	else { out << "REFUSAL (missing file): " << error << "\n"; }

	// A file that is not a ZIP.
	const QString notZip = QDir::tempPath() + QStringLiteral("/dawp-proof-not-a-zip.dawproject");
	QFile text(notZip);
	text.open(QIODevice::WriteOnly);
	text.write("not a zip");
	text.close();
	if (readDawProject(notZip, model, nullptr, &error))
	{
		out << "FAIL: a non-ZIP was accepted\n";
		failures++;
	}
	else { out << "REFUSAL (not a ZIP): " << error << "\n"; }

	// A valid ZIP with no project.xml entry (an end-of-central-directory only).
	const QString emptyZip = QDir::tempPath() + QStringLiteral("/dawp-proof-empty.dawproject");
	QFile empty(emptyZip);
	empty.open(QIODevice::WriteOnly);
	empty.write(QByteArray("PK\x05\x06", 4) + QByteArray(18, '\0'));
	empty.close();
	if (readDawProject(emptyZip, model, nullptr, &error))
	{
		out << "FAIL: a ZIP with no project.xml was accepted\n";
		failures++;
	}
	else { out << "REFUSAL (no project.xml): " << error << "\n"; }

	return failures;
}

//! Write the model, read it back, and report the counts and the version the file
//! carries. Returns the failures so far, or -1 when the file could not be
//! written or read at all (there is nothing to compare in that case).
static int writeAndRead(const DawProjectModel& authored, const QString& file,
	DawProjectModel* read)
{
	DawProjectWriteReport writeReport;
	QString error;
	if (!writeDawProject(file, authored, &writeReport, &error))
	{
		out << "FAIL write: " << error << "\n";
		return -1;
	}
	out << "wrote: path=" << writeReport.path << " bytes=" << writeReport.bytes
		<< " sha256=" << writeReport.sha256.left(16) << "...\n";

	int failures = 0;
	const struct { const char* name; int got; int want; } counts[] = {
		{ "track_count", writeReport.trackCount, 2 },
		{ "clip_count", writeReport.clipCount, 2 },
		{ "note_count", writeReport.noteCount, 2 },
		{ "mixer_channel_count", writeReport.mixerChannelCount, 2 },
		{ "tempo_point_count", writeReport.tempoPointCount, 1 },
		{ "meter_point_count", writeReport.meterPointCount, 1 },
	};
	for (const auto& entry : counts)
	{
		if (entry.got != entry.want)
		{
			out << "FAIL write report " << entry.name << ": " << entry.got << " != " << entry.want
				<< "\n";
			failures++;
		}
	}

	DawProjectReadReport readReport;
	if (!readDawProject(file, read, &readReport, &error))
	{
		out << "FAIL read: " << error << "\n";
		return -1;
	}
	out << "read: formatVersion=" << readReport.formatVersion
		<< " application=" << readReport.applicationName << " " << readReport.applicationVersion
		<< " hasMetaData=" << (readReport.hasMetaData ? "yes" : "no")
		<< " tracks=" << readReport.trackCount << " clips=" << readReport.clipCount
		<< " notes=" << readReport.noteCount << " mixer=" << readReport.mixerChannelCount
		<< " tempoPoints=" << readReport.tempoPointCount
		<< " meterPoints=" << readReport.meterPointCount << "\n";
	if (readReport.formatVersion != QStringLiteral("1.0"))
	{
		out << "FAIL format version: " << readReport.formatVersion << "\n";
		failures++;
	}
	return failures;
}

//! THE MODEL COMPARISON - not the file, not its hash - and the two digest
//! properties that make it mean something: stable across the trip, and MOVING
//! when the model moves. A digest blind to a changed note would satisfy "the
//! models are equal" for two blanks.
static int compareModels(const DawProjectModel& authored, const DawProjectModel& read)
{
	int failures = 0;
	if (read == authored)
	{
		out << "MODEL EQUAL: operator== says the read model is the authored model\n";
	}
	else
	{
		out << "MODEL DIFFERS\n--- authored ---\n" << dawProjectModelDigest(authored)
			<< "\n--- read ---\n" << dawProjectModelDigest(read) << "\n";
		failures++;
	}

	if (dawProjectModelDigest(read) != dawProjectModelDigest(authored))
	{
		out << "FAIL digest instability across the round trip\n";
		failures++;
	}

	DawProjectModel noteMoved = authored;
	noteMoved.tracks[0].clips[0].notes[1].key = 65;
	DawProjectModel tempoMoved = authored;
	tempoMoved.tempo = 141.0;
	const struct { const char* what; const DawProjectModel* model; } probes[] = {
		{ "a changed note key", &noteMoved },
		{ "the tempo", &tempoMoved },
	};
	for (const auto& probe : probes)
	{
		if (dawProjectModelDigest(*probe.model) == dawProjectModelDigest(authored))
		{
			out << "FAIL: the digest is blind to " << probe.what << "\n";
			failures++;
		}
		else { out << "DIGEST SENSITIVE: " << probe.what << " moves the digest\n"; }
	}
	return failures;
}

//! The ids the model carries ARE the document's ids and every IDREF points at
//! them (LOSSY #11) - asserted by name, so a document that gets renumbered fails
//! here rather than only in the blanket comparison - then the three refusals.
static int idsAndRefusals(const DawProjectModel& read)
{
	int failures = 0;
	const QStringList ids{ QStringLiteral("mixer0"), QStringLiteral("mixer1"),
		QStringLiteral("track0"), QStringLiteral("track1") };
	for (const QString& wanted : ids)
	{
		bool found = false;
		for (const DawProjectMixerChannel& channel : read.mixerChannels)
		{
			if (channel.id == wanted) { found = true; }
		}
		for (const DawProjectTrack& track : read.tracks)
		{
			if (track.id == wanted) { found = true; }
		}
		if (!found)
		{
			out << "FAIL: the model's id '" << wanted << "' did not survive the round trip\n";
			failures++;
		}
	}
	if (!read.tracks.isEmpty() && read.tracks[0].destinationChannelId != QStringLiteral("mixer0"))
	{
		out << "FAIL: the track-to-strip IDREF became '" << read.tracks[0].destinationChannelId
			<< "'\n";
		failures++;
	}
	else { out << "IDS AND IDREF: the model's own ids and destination survived\n"; }

	DawProjectModel scratch;
	failures += refusals(&scratch);
	return failures;
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QString file = QDir::tempPath() + QStringLiteral("/dawp-proof.dawproject");
	QFile::remove(file);

	const DawProjectModel authored = authoredModel();
	out << "authored: tracks=" << authored.trackCount() << " clips=" << authored.clipCount()
		<< " notes=" << authored.noteCount() << " mixer=" << authored.mixerChannelCount()
		<< " tempoPoints=" << authored.tempoPoints.size()
		<< " meterPoints=" << authored.meterPoints.size() << "\n";

	DawProjectModel read;
	int failures = writeAndRead(authored, file, &read);
	if (failures < 0)
	{
		out << "failures=1\n";
		out.flush();
		return 1;
	}
	failures += compareModels(authored, read);
	failures += idsAndRefusals(read);

	out << "failures=" << failures << "\n";
	out.flush();
	return failures == 0 ? 0 : 1;
}
