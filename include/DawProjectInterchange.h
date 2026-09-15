/*
 * DawProjectInterchange.h - the DAWproject container: the writer and the reader
 *                           behind the `dawproject.*` command group.
 *
 * The engine half of feature row 37 (docs/FEATURE-LIST-0.3.0.md section 7:
 * "DAWproject import / export"). DAWproject is a PUBLISHED format, so this
 * module implements a named version of it rather than a dialect: see the
 * VERSION section below, and docs/DAWPROJECT-INTERCHANGE.md for the quotation
 * it was read from.
 *
 * WHAT THIS FILE IS. One in-memory MODEL of a DAWproject document (the structs
 * below), plus the four conversions a round trip is made of: modelFromSong,
 * xmlFromModel, modelFromXml and applyModelToSong. Every conversion reports what
 * it could NOT carry, because the interesting part of an interchange format is
 * the part that is lossy (the LOSSY MAPPINGS section below). A suite of
 * conversions rather than one save/load pair is what makes the round trip
 * checkable the way the release contract asks: the MODEL can be compared, which
 * a file hash cannot do. The per-attribute mapping tables are in
 * docs/DAWPROJECT-INTERCHANGE.md.
 *
 * VERSION. The format is version 1.0 and is stable (the project's README.md,
 * "Status"), and Project.xsd declares version="1.0" on its own <xs:schema>.
 * This module writes version="1.0" and reads version="1.0"; a file declaring
 * another MAJOR version is refused with the version named, rather than read
 * with this module's assumptions. The two integers and the attribute value are
 * one constant each so a reader of the code can see the whole claim, and
 * `dawproject.convention` hands them to a client as data.
 *
 * THE CONTAINER is a ZIP with a `project.xml` entry and an optional
 * `metadata.xml` entry, XML, UTF-8 (README.md, "Format Specification"). This
 * module writes both entries and reads either; the ZIP is written with the
 * STORE method so the container needs no compression dependency - a stored
 * entry is a valid entry in every ZIP reader, and DAWproject's own spec
 * constrains the entries, not the compression method.
 *
 * LOSSY MAPPINGS. Every one is recorded here, in one line each, and in full -
 * with the reasoning - in docs/DAWPROJECT-INTERCHANGE.md, which is the document
 * the release contract asks for when it asks which version was implemented and
 * what it cannot carry. `dawproject.convention` reports the same list as data.
 *
 *   1. IN THE FORMAT, NOT WRITTEN: audio clips and their media, automation,
 *      device/plugin state, sends, fades and clip gain, loop points, scenes and
 *      clip slots. A MidiClip's NOTES are carried; nothing else about it is.
 *   2. IN THIS ENGINE, NOT IN THE FORMAT: take lanes, clip link groups, slide
 *      notes, note probability and detune.
 *   3. A map event carrying BOTH halves becomes TWO points (the format has one
 *      timeline per half), so the mapping is not a bijection. Nothing is lost.
 *   4. FOLDER NESTING: LMMS' track list is flat, so a folder's children import
 *      at the container root; the folder MARKER survives.
 *   5. TRACK TYPES: nine LMMS types onto the format's six-value contentType
 *      list (the exact table is on dawProjectContentTypeForType below).
 *   6. MIXER PAN, AND TWO VOLUME SCALES: <Pan> comes from the TRACK's panning
 *      model (only InstrumentTrack exposes one), and the format's one <Volume>
 *      per Channel must carry LMMS' two - a MixerChannel fader (0..2, unity
 *      1.0) on the bare <Channel>, the track volume (0..200, unity 100) on the
 *      track's own, divided by 100.
 *   7. MIXER ROUTING AND SHARING: `destination` names the strip a track feeds
 *      (written); LMMS' wider MixerRoute graph, its pre/post-fader flags and
 *      WHICH other tracks share a strip are not carried.
 *   8. TEMPO BOUNDS: written as 10..999, and a file outside them is REFUSED.
 *   9. TIME VALUES are beats; a FOREIGN time off LMMS' 48-per-beat grid is
 *      rounded onto it and counted in `rounded_times`.
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

#ifndef LMMS_DAWPROJECT_INTERCHANGE_H
#define LMMS_DAWPROJECT_INTERCHANGE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <utility>
#include <vector>

#include "lmms_export.h"

namespace lmms
{

class Song;

namespace interchange
{

// ---------------------------------------------------------------------------
// The version of the published format this module implements
// ---------------------------------------------------------------------------

//! README.md, "Status": "The format is version 1.0 and is stable."
constexpr int DawProjectMajorVersion = 1;
constexpr int DawProjectMinorVersion = 0;
//! The value of the REQUIRED `version` attribute on the root <Project> element
//! (Project.xsd: project/version is `use="required"`).
constexpr const char* DawProjectVersionAttribute = "1.0";

//! The container's entries (README.md "Format Specification": "Container: ZIP",
//! "Format: XML (project.xml, metadata.xml)", "Text encoding: UTF-8").
constexpr const char* DawProjectProjectEntry = "project.xml";
constexpr const char* DawProjectMetaDataEntry = "metadata.xml";

//! The time unit every time value this module writes declares
//! (Project.xsd simpleType `timeUnit`: beats | seconds).
constexpr const char* DawProjectTimeUnit = "beats";

//! LMMS' own grid, and the one the format's beat coincides with:
//! TimePos::ticksPerQuarterNote() == 48, so one LMMS tick is 1/48 beat.
constexpr int DawProjectTicksPerQuarterNote = 48;

//! The tempo bounds this module writes into <Tempo min max> and enforces on
//! read: the engine's own (TempoMap and Song::setTempo accept 10..999).
constexpr int DawProjectMinTempo = 10;
constexpr int DawProjectMaxTempo = 999;

//! Fraction of a beat one LMMS tick is, as the writer's decimal places use it.
LMMS_EXPORT double dawProjectBeatsFromTicks(qint64 ticks);
//! The tick a beat value lands on - the reader's inverse, rounded to the grid.
LMMS_EXPORT qint64 dawProjectTicksFromBeats(double beats);

/*! The format's `contentType` value for a LMMS Track::Type name, and back.
 *
 * The format's enum is audio|automation|notes|video|markers|tracks (Project.xsd
 * simpleType `contentType`, a space-separated LIST) and LMMS' Track::Type has
 * nine values, so the mapping is many-to-one in one direction:
 *
 *   Instrument -> "notes"      Pattern -> "notes"     Sample -> "audio"
 *   Automation -> "automation" (and HiddenAutomation -> "automation")
 *   Video      -> "video"      Folder  -> "tracks"    Event -> "" (no counterpart)
 *
 * \\a lost is set true when no format value matches - the type is written with
 * an empty contentType and the count is reported, rather than inventing a
 * vocabulary value the schema does not declare. The reverse direction gives the
 * LMMS type a format value is read back as: "audio" -> Sample, "automation" ->
 * Automation, "video" -> Video, "tracks" -> Folder, anything with "notes" (or
 * nothing at all) -> Instrument.
 */
LMMS_EXPORT QString dawProjectContentTypeForType(const QString& typeName, bool* lost);
LMMS_EXPORT QString dawProjectTypeForContentType(const QString& contentType, bool* lost);

// ---------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------

/*! One <Note>. time and duration are BEATS and are CLIP-RELATIVE - the format
 *  places a Note on its clip's own timeline, which is where LMMS' MidiClip
 *  keeps its notes too. */
struct DawProjectNote
{
	double time = 0.0;
	double duration = 0.25;
	int channel = 0;
	int key = 60;
	//! velocity and release velocity, both 0..1 (LMMS' note volume is 0..200).
	double vel = 0.5;
	double rel = 0.5;

	bool operator==(const DawProjectNote& other) const
	{
		return time == other.time && duration == other.duration && channel == other.channel
			&& key == other.key && vel == other.vel && rel == other.rel;
	}
	bool operator!=(const DawProjectNote& other) const { return !(*this == other); }
};

//! One <Clip> of notes. An audio clip is not in the model: see LOSSY #1.
struct DawProjectClip
{
	double time = 0.0;       //!< where the clip sits, in beats
	double duration = 0.0;   //!< its length, in beats
	double playStart = 0.0;  //!< content start offset (LMMS startTimeOffset())
	QString name;
	QString color;           //!< "#rrggbb", empty when the clip has none
	QVector<DawProjectNote> notes;

	int noteCount() const { return notes.size(); }
	bool operator==(const DawProjectClip& other) const
	{
		return time == other.time && duration == other.duration
			&& playStart == other.playStart && name == other.name
			&& color == other.color && notes == other.notes;
	}
	bool operator!=(const DawProjectClip& other) const { return !(*this == other); }
};

/*! One bare <Channel> of <Structure> - a MIXER strip in its own right.
 *
 * <Structure> is a choice of Track | Channel (Project.xsd), and LMMS'
 * MixerChannel is a SUMMING STRIP several tracks may feed, not a property of any
 * one track. `index` is the LMMS MixerChannel index it came from (-1 for a
 * file), the conversion-time key that joins a track to its strip; the file
 * itself joins them by IDREF. docs/DAWPROJECT-INTERCHANGE.md section 4. */
struct DawProjectMixerChannel
{
	QString id;              //!< the document's own id (xs:ID), the IDREF target
	QString name;
	QString role = QStringLiteral("regular");  //!< "master" for index 0, "submix" for a bus
	bool solo = false;
	bool mute = false;
	double volume = 1.0;     //!< linear, 0..2
	int audioChannels = 2;
	int index = -1;          //!< the LMMS MixerChannel index this came from

	bool operator==(const DawProjectMixerChannel& other) const
	{
		return id == other.id && name == other.name && role == other.role && solo == other.solo
			&& mute == other.mute && volume == other.volume
			&& audioChannels == other.audioChannels;
	}
	bool operator!=(const DawProjectMixerChannel& other) const { return !(*this == other); }
};

/*! One <Track> and the <Channel> inside it - the TRACK's own strip.
 *
 * `volume`, `mute`, `solo` and `pan` come from the TRACK's own models, NOT from
 * a mixer channel: conflating the two would write the master fader onto every
 * track, because a fresh LMMS track is assigned mixer channel 0.
 *
 * `destinationChannelId` is the `destination` IDREF - the mixer strip above that
 * this track feeds. That is the one routing fact the format CAN express. */
struct DawProjectTrack
{
	QString id;              //!< the document's own id (xs:ID), also its lane's IDREF
	QString name;
	QString color;           //!< "#rrggbb", empty when the track has none
	QString contentType;     //!< the format's space-separated contentType list
	QString typeName;        //!< the LMMS Track::Type name this was mapped from/to
	bool lostContentType = false;  //!< true when no format value matched the type

	QString channelId;
	bool solo = false;
	bool mute = false;
	double volume = 1.0;     //!< the TRACK's volume (linear 0..2), when it has one
	bool hasVolume = false;  //!< false when the track type exposes no volume model
	double pan = 0.5;        //!< normalized, 0..1
	bool hasPan = false;     //!< false when the format had no <Pan> for this channel
	//! The mixer strip this track feeds: the id of a DawProjectMixerChannel.
	QString destinationChannelId;
	//! The LMMS MixerChannel index behind destinationChannelId (-1 when none,
	//! or when the model came from a file). LOSSY #7: the format's
	//! one-Channel-per-Track model has no way to say "two tracks share this
	//! strip", so the SHARING is not carried and this is a conversion-time note.
	int mixerChannelIndex = -1;

	QVector<DawProjectClip> clips;

	int clipCount() const { return clips.size(); }
	int noteCount() const;
	bool operator==(const DawProjectTrack& other) const;
	bool operator!=(const DawProjectTrack& other) const { return !(*this == other); }
};

//! One point of a tempo or time-signature timeline. `isMeter` picks the arm,
//! because the format has one element type per arm: <RealPoint value> and
//! <TimeSignaturePoint numerator denominator>.
struct DawProjectPoint
{
	double time = 0.0;       //!< beats
	bool isMeter = false;
	double value = 0.0;      //!< bpm, for the tempo arm
	int numerator = 0;
	int denominator = 0;

	bool operator==(const DawProjectPoint& other) const
	{
		return time == other.time && isMeter == other.isMeter && value == other.value
			&& numerator == other.numerator && denominator == other.denominator;
	}
	bool operator!=(const DawProjectPoint& other) const { return !(*this == other); }
};

//! A whole document, in the terms both conversions agree on.
struct DawProjectModel
{
	// <Project version> and <Application name version>
	QString formatVersion = QString::fromLatin1(DawProjectVersionAttribute);
	QString applicationName;
	QString applicationVersion;

	// <Transport>
	double tempo = 120.0;
	int numerator = 4;
	int denominator = 4;

	QVector<DawProjectMixerChannel> mixerChannels;  //!< bare <Channel> of <Structure>
	QVector<DawProjectTrack> tracks;         //!< <Structure>, in order
	QVector<DawProjectPoint> tempoPoints;    //!< <Arrangement>/<TempoAutomation>
	QVector<DawProjectPoint> meterPoints;    //!< <Arrangement>/<TimeSignatureAutomation>

	int trackCount() const { return tracks.size(); }
	int mixerChannelCount() const { return mixerChannels.size(); }
	int clipCount() const;
	int noteCount() const;
	//! Points at which a LMMS tempo-map event carried BOTH halves (LOSSY #2).
	int splitEvents = 0;

	bool operator==(const DawProjectModel& other) const;
	bool operator!=(const DawProjectModel& other) const { return !(*this == other); }
};

/*! A printable, line-per-entity view of the model - what a round trip compares.
 *  Deliberately NOT a hash: a failure prints the two lines that differ, so the
 *  proof says WHAT changed rather than that something did (the shape
 *  SmfInterchangeRoundTripTest uses for the tempo map, generalised to tracks,
 *  clips and notes). */
LMMS_EXPORT QString dawProjectModelDigest(const DawProjectModel& model);

//! The model as JSON, for `dawproject.read` and for a test that must not read
//! the file twice.
LMMS_EXPORT QJsonObject dawProjectModelJson(const DawProjectModel& model);

//! What a conversion could not carry. Every field is a count or a flag a test
//! can assert on, so "lossy" is a measurement rather than a claim.
struct DawProjectLossReport
{
	//! Clips whose type carries content other than notes (SampleClip, ...).
	int audioClipsSkipped = 0;
	int automationClipsSkipped = 0;
	//! Track types with no content counterpart in the format's contentType list.
	int unmappedTrackTypes = 0;
	//! Folder tracks written as a flat track: the relation is not carried.
	int folderChildrenLost = 0;
	//! Tracks whose type has no panning model, so no <Pan> was written.
	int panNotWritten = 0;
	//! Mixer routes other than "everything to master", not carried.
	int routingLost = 0;
	//! Tracks whose assigned MixerChannel is SHARED with another track, or is
	//! not the one the engine would have given them: the format is
	//! one-Channel-per-Track and has no way to say it (LOSSY #7).
	int mixerSharingLost = 0;
	//! Notes or clips whose time was not on LMMS' grid and was rounded.
	int roundedTimes = 0;
	//! Device state (<Devices>): in the format, not written by this module.
	int devicesNotWritten = 0;
	int sendsNotWritten = 0;
	int fadesNotWritten = 0;
	//! LMMS tempo-map events that carried both halves: two points, one event.
	int splitMapEvents = 0;

	//! Every count above, as JSON, for the wire.
	QJsonObject toJson() const;
	//! True when the conversion carried everything it claims to carry.
	bool clean() const;
};

//! What one successful write produced. `bytes` and `sha256` describe the FILE
//! and are for the caller's own identification; the model digest is what the
//! proof compares.
struct DawProjectWriteReport
{
	QString path;
	qint64 bytes = 0;
	QString sha256;
	int trackCount = 0;
	int mixerChannelCount = 0;
	int clipCount = 0;
	int noteCount = 0;
	int tempoPointCount = 0;
	int meterPointCount = 0;
	QString modelDigest;
	DawProjectLossReport loss;
};

//! What one successful read produced.
struct DawProjectReadReport
{
	bool ok = false;
	QString error;                 //!< the refusal, verbatim, when ok is false
	QString formatVersion;
	QString applicationName;
	QString applicationVersion;
	bool hasMetaData = false;
	QString title;                 //!< <MetaData>/<Title>, when the entry is there
	int trackCount = 0;
	int mixerChannelCount = 0;
	int clipCount = 0;
	int noteCount = 0;
	int tempoPointCount = 0;
	int meterPointCount = 0;
	//! Entries the container carried that this module does not use.
	QStringList unusedEntries;
	DawProjectLossReport loss;
};

// ---------------------------------------------------------------------------
// The convention, as data on the wire (`dawproject.convention`)
// ---------------------------------------------------------------------------

/*! The version, container and time conventions this module implements, and the
 *  stated losses, as data rather than as prose in a comment. */
struct DawProjectConvention
{
	QString formatName;
	QString formatVersion;
	int majorVersion = DawProjectMajorVersion;
	int minorVersion = DawProjectMinorVersion;
	QString container;
	QString projectEntry;
	QString metaDataEntry;
	QString textEncoding;
	QString timeUnit;
	int ticksPerQuarterNote = DawProjectTicksPerQuarterNote;
	QString tickRule;
	QString zipMethod;
	QString contentTypeVocabulary;
	QString statedLosses;
};

LMMS_EXPORT const DawProjectConvention& dawProjectConvention();

// ---------------------------------------------------------------------------
// The four conversions
// ---------------------------------------------------------------------------

/*! The session as a DAWproject model. \\a loss receives what could not be
 *  carried. Never fails: a session always describes a document. */
LMMS_EXPORT DawProjectModel dawProjectModelFromSong(Song* song, DawProjectLossReport* loss);

/*! The model as the bytes of the container's project.xml entry (no XML
 *  declaration prefix beyond the format's own, no namespace: Project.xsd
 *  declares no targetNamespace). False - with \\a error set - when the model
 *  holds a value the format cannot express (a tempo outside 10..999, a metre
 *  whose denominator is not a power of two, a negative time). */
LMMS_EXPORT bool dawProjectXmlFromModel(const DawProjectModel& model, QByteArray* xml,
	QString* error);

/*! The model the container's project.xml bytes describe. False - with \\a error
 *  set and \\a model untouched - when the document is not a DAWproject
 *  document, declares another MAJOR version, or holds a value the engine will
 *  not accept. */
LMMS_EXPORT bool dawProjectModelFromXml(const QByteArray& xml, DawProjectModel* model,
	DawProjectReadReport* report, QString* error);

/*! Write \\a model at \\a path as a .dawproject container (ZIP: project.xml and
 *  metadata.xml, UTF-8). False - with \\a error set and no file left behind -
 *  when the path is unusable or the model cannot be expressed. */
LMMS_EXPORT bool writeDawProject(const QString& path, const DawProjectModel& model,
	DawProjectWriteReport* report, QString* error);

/*! Read the container at \\a path. False - with \\a error set - when the file
 *  is not a ZIP, has no project.xml entry, or holds a document this module
 *  refuses. */
LMMS_EXPORT bool readDawProject(const QString& path, DawProjectModel* model,
	DawProjectReadReport* report, QString* error);

/*! Apply \\a model to \\a song: replace its tracks, its tempo map, its global
 *  tempo and metre and each track's mixer channel. Discards the tracks that
 *  were there - the caller's inverse is responsible for them. False - with
 *  \\a error set - when a track type cannot be created. */
LMMS_EXPORT bool applyDawProjectModel(Song* song, const DawProjectModel& model, QString* error);

// ---------------------------------------------------------------------------
// The container (a ZIP with STORE entries, and the two functions that make one)
// ---------------------------------------------------------------------------

//! One entry of a container: its name and its bytes, in the order written.
using DawProjectEntry = std::pair<QString, QByteArray>;

/*! Write \\a entries as a ZIP container. STORE only (no compression): the
 *  format constrains the entries, not the method, and a stored entry needs no
 *  compression dependency to be a valid entry. False - with \\a error set and
 *  no file left behind - when the path is unusable or an entry name is empty.
 */
LMMS_EXPORT bool dawProjectZipWrite(const QString& path,
	const std::vector<DawProjectEntry>& entries, QString* error);

/*! Read every entry of the ZIP at \\a path, in central-directory order.
 *  Bounds-checked against the file's own sizes: a truncated or hostile
 *  container is refused with \\a error rather than read past its end. */
LMMS_EXPORT bool dawProjectZipRead(const QString& path,
	std::vector<DawProjectEntry>* entries, QString* error);

//! The SHA-256 of \\a bytes, lowercase hex - the writer's own file identity.
LMMS_EXPORT QString dawProjectSha256(const QByteArray& bytes);

} // namespace interchange
} // namespace lmms

#endif // LMMS_DAWPROJECT_INTERCHANGE_H
