/*
 * AafImport.h - AAF import through LibAAF: the reader, the plain model it
 *               returns and the builder that turns the model into tracks.
 *
 * WF-038 (0.5.0 catch-up wave 2, row F4): "AAF, OMF and MXF exchange". The
 * surface is the `interchange.aaf_*` ids in src/core/ControlCommandsAafImport.cpp;
 * this header is the engine half, shaped like the other two interchange imports
 * (include/DawProjectInterchange.h, include/SmfInterchange.h):
 *
 *   readAaf()          file -> AafModel + AafLossReport. Plain data: no LibAAF
 *                      type crosses this header, so the builder and the tests
 *                      never need the library.
 *   buildAafSession()  AafModel -> sample tracks, sample clips and markers in
 *                      the song. The command records it as ONE undo step.
 *
 * THE TIME RULE. An AAF names every position in its track's EDIT UNITS (a
 * rational rate - 30000/1001 for a 29.97 video sequence, 48000/1 for a sample-
 * accurate one). The reader converts each one to FRAMES at the composition's
 * dominant sample rate (aafFramesFromEditUnits), so the model is in one unit;
 * the builder converts frames to LMMS ticks at the PROJECT TEMPO
 * (aafTicksFromFrames: 48 ticks per quarter note). An AAF has no tempo, so
 * the global tempo is the one honest choice; an active tempo map is NOT
 * followed, and the command says so in its reply.
 *
 * WHAT IS CARRIED: audio tracks (name, constant gain as the track volume,
 * constant pan, mute), audio clips (position, length, source offset, constant
 * clip gain, a clip-gain automation curve as the clip's gain envelope, fade-in
 * and fade-out with their curve, clip mute), crossfades (as the fade-out and
 * fade-in pair they are - LibAAF already overlaps the two clips by the
 * transition's length), markers (position, name, colour) and the media:
 * embedded essence is extracted to WAV files, external essence is resolved by
 * path and a file that cannot be found is LISTED, never guessed.
 *
 * WHAT IS NOT (AafLossReport counts each): video tracks and clips, track gain
 * above the sample track's fader range, track gain and pan automation, solo
 * flags (soloing would rewrite the mute of tracks the
 * import did not create), the channels after the first of a clip made of
 * several mono essences, the channel selection inside a multichannel file, a
 * marker's length and comment, gain-envelope points past the envelope's 32,
 * and clips that would land past the song's length. Effects and plug-in state
 * are not in what LibAAF exposes at all.
 *
 * OMF. LibAAF reads AAF (a Compound File Binary container); OMF 2.0 is a Bento
 * container it does not parse, so a .omf path is refused by name.
 *
 * WITHOUT LIBAAF (ZENE_HAVE_LIBAAF not defined) everything here still links:
 * aafImportAvailable() answers false and readAaf() refuses with a message
 * that begins "unavailable". The model, the time rule and the builder do not
 * depend on the library.
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

#ifndef LMMS_AAF_IMPORT_H
#define LMMS_AAF_IMPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <tuple>
#include <vector>

#include "ClipEdits.h"
#include "lmms_export.h"

namespace lmms
{

class Song;

namespace interchange
{

//! LMMS' grid: one quarter note is 48 ticks (DefaultTicksPerBar / 4).
constexpr int AafTicksPerQuarterNote = 48;
//! The most library messages a report keeps (a broken file can log thousands).
constexpr int AafMaxWarnings = 32;
//! The sample rate assumed when a file names none (an empty composition).
constexpr int AafDefaultSampleRate = 48000;

//! One fade ramp: its length in model frames and its curve.
struct AafFade
{
	qint64 frames = 0;
	FadeShape shape = FadeShape::Linear;
	bool operator==(const AafFade& other) const { return frames == other.frames && shape == other.shape; }
};

//! One point of a clip-gain curve: where it is as a fraction (0..1) of the clip's length, and its gain.
struct AafGainPoint
{
	double at = 0.0;
	float gainDb = 0.f;
	bool operator==(const AafGainPoint& other) const { return at == other.at && gainDb == other.gainDb; }
};

//! One audio clip. Every time value is in frames at AafModel::sampleRate.
struct AafClip
{
	QString name;              //!< the source's name (MasterMob name, or the sub-clip's)
	QString mediaName;         //!< the AafMedia entry it plays (AafMedia::name)
	QString sourcePath;        //!< a usable local file; EMPTY when missing, or embedded on a dry run
	qint64 positionFrames = 0; //!< from the composition's start
	qint64 lengthFrames = 0;
	qint64 sourceOffsetFrames = 0; //!< where in the source the clip starts
	double gain = 1.0;         //!< constant clip gain, linear
	QVector<AafGainPoint> envelope; //!< clip-gain automation, in time order
	AafFade fadeIn;
	AafFade fadeOut;
	bool muted = false;

	//! Every field, for comparing (one tuple, so a new field is one more name here - ClipEdits' shape).
	auto fields() const
	{
		return std::tie(name, mediaName, sourcePath, positionFrames, lengthFrames, sourceOffsetFrames, gain,
			envelope, fadeIn, fadeOut, muted);
	}
	bool operator==(const AafClip& other) const { return fields() == other.fields(); }
};

//! One audio track.
struct AafTrack
{
	int number = 0;
	QString name;
	double gain = 1.0;  //!< constant track gain (the fader), linear
	bool hasPan = false;
	double pan = 0.5;   //!< constant pan, 0 = left, 0.5 = centre, 1 = right (LibAAF's scale)
	bool muted = false;
	QVector<AafClip> clips;

	bool operator==(const AafTrack& other) const
	{
		return number == other.number && name == other.name && gain == other.gain && hasPan == other.hasPan
			&& pan == other.pan && muted == other.muted && clips == other.clips;
	}
};

//! One composition marker.
struct AafMarker
{
	qint64 positionFrames = 0;
	QString name;
	QString color; //!< "#rrggbb", or empty
	bool operator==(const AafMarker& other) const
	{
		return positionFrames == other.positionFrames && name == other.name && color == other.color;
	}
};

//! One essence (media file) the composition uses, and what became of it.
struct AafMedia
{
	QString name;
	QString originalUri;  //!< as the AAF names it (external essence)
	QString path;         //!< the usable local file: extracted, or found (empty for embedded on a dry run)
	bool embedded = false;
	//! No usable file: external and not found (or in a format this build cannot decode), or embedded
	//! and the extraction failed. A dry run's embedded media is NOT missing - it would be extracted.
	bool missing = false;
	int channels = 0;
	int sampleRate = 0;
	bool operator==(const AafMedia& other) const
	{
		return name == other.name && originalUri == other.originalUri && path == other.path
			&& embedded == other.embedded && missing == other.missing && channels == other.channels
			&& sampleRate == other.sampleRate;
	}
};

//! The composition, as plain data.
struct AafModel
{
	QString compositionName;
	QString productName;      //!< the application that wrote the file
	int sampleRate = AafDefaultSampleRate; //!< the unit every frame value is in
	QString editRate;         //!< the first audio track's edit rate, "num/den"
	QVector<AafTrack> tracks;
	QVector<AafMarker> markers;
	QVector<AafMedia> media;

	int clipCount() const;
	//! The names (or original paths) of every essence with no usable file.
	QStringList missingMedia() const;
	//! The earliest clip's position, 0 with no clip.
	qint64 firstClipFrame() const;

	bool operator==(const AafModel& other) const
	{
		return compositionName == other.compositionName && productName == other.productName
			&& sampleRate == other.sampleRate && editRate == other.editRate && tracks == other.tracks
			&& markers == other.markers && media == other.media;
	}
};

//! What the file carried that the model does not.
struct AafLossReport
{
	int videoTracks = 0;
	int videoClips = 0;
	int crossfades = 0;            //!< carried, as fade pairs; counted so a caller can see them
	int trackGainAutomation = 0;
	int trackPanAutomation = 0;
	int soloTracks = 0;
	int multiEssenceClips = 0;     //!< a clip of several mono essences: only the first is played
	int channelSelections = 0;     //!< a clip playing one channel of a multichannel file: the whole file plays
	int markerExtras = 0;          //!< markers whose length or comment is dropped
	int envelopePointsDropped = 0;
	int missingMediaClips = 0;     //!< clips whose source file is missing (not built)
	int beyondSongLength = 0;      //!< clips past MaxSongLength (filled in by the builder)
	int trackGainClamps = 0;      //!< track gains clipped to the sample fader range (read-only report or builder)
	QStringList warnings;          //!< LibAAF's own warnings and errors, at most AafMaxWarnings

	QJsonObject toJson() const;
};

//! Fill the loss caused by track gains outside a SampleTrack's fader range.
LMMS_EXPORT void aafPopulateTrackGainClampLoss(const AafModel& model, AafLossReport& loss);

//! How readAaf() treats the media.
struct AafReadOptions
{
	//! Extract embedded essence to WAV files in extractDir. False for a dry run: nothing is written.
	bool extract = false;
	QString extractDir;
	//! An extra directory LibAAF searches for external essence (empty: the AAF's own directory only).
	QString mediaLocation;
};

//! How buildAafSession() places the model.
struct AafBuildOptions
{
	double tempo = 120.0;      //!< the project tempo the frames are converted at
	qint64 shiftFrames = 0;    //!< subtracted from every position (to start the first clip at 0)
	bool markers = true;       //!< also add the markers (the redo of an undo restores them itself)
};

//! What buildAafSession() made.
struct AafBuildReport
{
	int tracks = 0;
	int clips = 0;
	int markers = 0;
	int missingMediaClips = 0;
	int beyondSongLength = 0;
	int trackGainClamps = 0;
};

//! True when this build reads AAF (it was compiled with LibAAF).
LMMS_EXPORT bool aafImportAvailable();
//! The LibAAF version this build was compiled against, or empty without it.
LMMS_EXPORT QString aafLibraryVersion();
//! True when @a path names an OMF file (by extension), which LibAAF does not read.
LMMS_EXPORT bool aafIsOmfPath(const QString& path);

//! @a units edit units at @a rateNum / @a rateDen per second, as frames at @a sampleRate (rounded).
LMMS_EXPORT qint64 aafFramesFromEditUnits(qint64 units, int rateNum, int rateDen, int sampleRate);
//! @a frames at @a sampleRate, as LMMS ticks at @a tempo beats per minute (rounded).
LMMS_EXPORT qint64 aafTicksFromFrames(qint64 frames, int sampleRate, double tempo);

/*! Where an import extracts embedded essence when the caller does not say: beside the project in
 *  "<project>-aaf-media/<aaf name>", or, for a project never saved, beside the AAF in
 *  "<aaf name>-aaf-media" (a temporary directory would lose the media the clips play). */
LMMS_EXPORT QString aafDefaultMediaDir(const QString& aafPath, const QString& projectFile);

/*! Read @a path. False with @a error set when the build has no LibAAF ("unavailable: ..."), when
 *  the path is an OMF file, or when LibAAF cannot open it. Media is extracted only when
 *  @a options asks; a missing external file is not an error - it is listed in @a model's media. */
LMMS_EXPORT bool readAaf(const QString& path, const AafReadOptions& options, AafModel* model,
	AafLossReport* loss, QString* error);

//! The model as the `interchange.aaf_read` reply carries it.
LMMS_EXPORT QJsonObject aafModelJson(const AafModel& model);

/*! Build @a model into @a song: one sample track per AAF track, one sample clip per clip whose media
 *  is there, and (with options.markers) one marker per AAF marker. @a madeTrackIds receives the new
 *  tracks' stable ids (Track::id). Records NO undo step - the command does, around it. */
LMMS_EXPORT AafBuildReport buildAafSession(Song* song, const AafModel& model, const AafBuildOptions& options,
	std::vector<int>* madeTrackIds);

} // namespace interchange

} // namespace lmms

#endif // LMMS_AAF_IMPORT_H
