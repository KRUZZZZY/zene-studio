/*
 * ControlCommandsAafImport.cpp - the `interchange.aaf_*` ids: AAF import
 *                                through LibAAF (WF-038).
 *
 * The engine half is include/AafImport.h and its three translation units (the
 * model, the reader, the builder). This file is the SURFACE, shaped like the
 * group's Standard MIDI File ids and like dawproject.* (the same question asked
 * of another format), minus an export - LibAAF reads AAF, it does not write it:
 *
 *   interchange.aaf_convention  whether this build reads AAF, the library and
 *                               its version, the time rule, the media rule, OMF,
 *                               and the stated losses - as data on the wire
 *   interchange.aaf_read        read a file's model back WITHOUT touching the
 *                               session or the disk (a dry run: nothing is
 *                               extracted); the loss report says what would
 *                               not come across
 *   interchange.aaf_import      build the file into the session: sample tracks,
 *                               sample clips and markers, ONE undo step
 *
 * THE THREE IDS ALWAYS REGISTER. A build without LibAAF (ZENE_HAVE_LIBAAF not
 * defined) answers aaf_convention with `available: false`, and aaf_read and
 * aaf_import refuse, typed `requires`, with a message that begins
 * "unavailable". An .omf path is refused by name in every build: LibAAF does
 * not read OMF.
 *
 * REVERSIBILITY. aaf_convention and aaf_read touch no session state (the SMF
 * ids' class). aaf_import ADDS tracks and markers; its undo removes the tracks
 * it made, by their stable ids, and puts the marker list back - an action
 * checkpoint, the project.import_tracks shape, one command one Ctrl+Z. The
 * media it extracted is a file outside the session and stays on disk (as a
 * bounce's render does), so a redo plays the same files again.
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

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <memory>
#include <vector>

#include "AafImport.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "ProjectMarkers.h"
#include "Song.h"
#include "TempoMap.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

namespace aaf = interchange;

QString statedLosses()
{
	return QStringLiteral("video tracks and clips; track gain and pan automation (a constant gain and pan are "
		"carried); solo flags; the channels after the first of a clip made of several mono essences; the "
		"channel selection inside a multichannel file (the whole file plays); a marker's length and comment; "
		"clip-gain curve points past 32 (thinned evenly); clips past the song's length; effects, plug-in "
		"state, sends and buses (LibAAF does not expose them)");
}

ControlResult convention(const QJsonObject&)
{
	return ControlResult::success({{QStringLiteral("available"), aaf::aafImportAvailable()},
		{QStringLiteral("library"), QStringLiteral("LibAAF")},
		{QStringLiteral("library_version"), aaf::aafLibraryVersion()},
		{QStringLiteral("formats_read"), QJsonArray{QStringLiteral("aaf")}},
		{QStringLiteral("omf_supported"), false},
		{QStringLiteral("omf_reason"), QStringLiteral("LibAAF parses AAF's Compound File Binary container; OMF "
			"2.0 is a Bento container it does not read - export AAF from the source application")},
		{QStringLiteral("time_rule"), QStringLiteral("edit units -> frames at the composition's dominant sample "
			"rate (rounded) -> ticks at the project's global tempo, 48 ticks per quarter note; an active tempo "
			"map is not followed")},
		{QStringLiteral("media_rule"), QStringLiteral("embedded essence is extracted to WAV files in 'media_dir' "
			"(default: <project>-aaf-media/<aaf name> beside a saved project, else <aaf name>-aaf-media beside "
			"the AAF); external essence is found by LibAAF (beside the AAF, or in 'media_location'); a file not "
			"found, or in a format this build cannot decode, is listed in missing_media and its clips are not "
			"built")},
		{QStringLiteral("stated_losses"), statedLosses()}});
}

//! The refusals every reading id shares: no such file, an OMF file, no LibAAF in this build.
bool acceptPath(const QString& path, ControlResult* error)
{
	if (!QFileInfo(path).isFile())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such file: %1").arg(path));
		return false;
	}
	if (aaf::aafIsOmfPath(path))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 is an OMF file, which is not "
			"supported: LibAAF reads AAF only (OMF 2.0 is a Bento container it does not parse) - export AAF from "
			"the source application").arg(path));
		return false;
	}
	if (!aaf::aafImportAvailable())
	{
		*error = ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("unavailable: this build was "
			"configured without LibAAF (ZENE_WANT_LIBAAF, libaaf not found), so it cannot read AAF files"));
		return false;
	}
	return true;
}

//! 'media_location', when given, must be an existing directory.
bool readLocation(const QJsonObject& args, aaf::AafReadOptions* options, ControlResult* error)
{
	options->mediaLocation = args.value(QStringLiteral("media_location")).toString();
	if (options->mediaLocation.isEmpty() || QFileInfo(options->mediaLocation).isDir()) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'media_location' %1 is not a directory").arg(options->mediaLocation));
	return false;
}

//! The file read into @a model, or a typed refusal.
bool readFile(const QString& path, const aaf::AafReadOptions& options, aaf::AafModel* model,
	aaf::AafLossReport* loss, ControlResult* error)
{
	QString message;
	if (aaf::readAaf(path, options, model, loss, &message)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs, message);
	return false;
}

int embeddedCount(const aaf::AafModel& model)
{
	int count = 0;
	for (const aaf::AafMedia& entry : model.media) { count += entry.embedded ? 1 : 0; }
	return count;
}

//! The facts aaf_read and aaf_import both answer.
QJsonObject summary(const QString& path, const aaf::AafModel& model, const aaf::AafLossReport& loss)
{
	Song* song = Engine::getSong();
	return {{QStringLiteral("path"), path}, {QStringLiteral("composition_name"), model.compositionName},
		{QStringLiteral("product_name"), model.productName}, {QStringLiteral("sample_rate"), model.sampleRate},
		{QStringLiteral("edit_rate"), model.editRate}, {QStringLiteral("track_count"), model.tracks.size()},
		{QStringLiteral("clip_count"), model.clipCount()}, {QStringLiteral("marker_count"), model.markers.size()},
		{QStringLiteral("embedded_media"), embeddedCount(model)},
		{QStringLiteral("missing_media"), QJsonArray::fromStringList(model.missingMedia())},
		{QStringLiteral("tempo"), static_cast<double>(song->getTempo())},
		{QStringLiteral("tempo_map_active"), song->tempoMap().map().active()},
		{QStringLiteral("loss"), loss.toJson()}};
}

QString mediaDirFor(const QJsonObject& args, const QString& path)
{
	const QString asked = args.value(QStringLiteral("media_dir")).toString();
	return asked.isEmpty() ? aaf::aafDefaultMediaDir(path, Engine::getSong()->projectFileName()) : asked;
}

//! The import's media options: 'media_location' as aaf_read takes it, and 'media_dir', which must be absolute.
bool readImportMedia(const QJsonObject& args, const QString& path, aaf::AafReadOptions* options, ControlResult* error)
{
	if (!readLocation(args, options, error)) { return false; }
	const QString asked = args.value(QStringLiteral("media_dir")).toString();
	if (!asked.isEmpty() && !QFileInfo(asked).isAbsolute())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'media_dir' must be an absolute path"));
		return false;
	}
	options->extract = true;
	options->extractDir = mediaDirFor(args, path);
	return true;
}

ControlResult readOnly(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	ControlResult error;
	aaf::AafReadOptions options;
	if (!acceptPath(path, &error) || !readLocation(args, &options, &error)) { return error; }
	aaf::AafModel model;
	aaf::AafLossReport loss;
	if (!readFile(path, options, &model, &loss, &error)) { return error; }
	QJsonObject result = summary(path, model, loss);
	result.insert(QStringLiteral("media_dir"), mediaDirFor(args, path));
	result.insert(QStringLiteral("model"), aaf::aafModelJson(model));
	return ControlResult::success(result);
}

//! buildAafSession() with the journal off, the way ImportFilter imports a MIDI file: the build writes models (a
//! track's volume, a clip's mute) and each write would check in a checkpoint of an object the step below removes.
aaf::AafBuildReport buildQuietly(const aaf::AafModel& model, const aaf::AafBuildOptions& options, std::vector<int>* made)
{
	ProjectJournal* journal = Engine::projectJournal();
	const bool journalling = journal->isJournalling();
	journal->setJournalling(false);
	const aaf::AafBuildReport report = aaf::buildAafSession(Engine::getSong(), model, options, made);
	journal->setJournalling(journalling);
	return report;
}

//! buildAafSession() as ONE undo step: its undo removes the tracks it made (by stable id - track.remove and
//! its undo free a track and build an equal one) and puts the marker list back; its redo builds again.
std::vector<int> buildAsOneStep(const aaf::AafModel& model, const aaf::AafBuildOptions& options,
	aaf::AafBuildReport* report)
{
	auto made = std::make_shared<std::vector<int>>();
	const QVector<Marker> before = ProjectMarkers::instance().markers();
	*report = buildQuietly(model, options, made.get());
	const QVector<Marker> after = ProjectMarkers::instance().markers();
	aaf::AafBuildOptions again = options;
	again.markers = false;  // the redo puts the very markers back, ids and all
	addUndoStep([made, before]() {
		for (auto it = made->rbegin(); it != made->rend(); ++it) { if (Track* live = trackWithId(*it)) { removeTrack(live); } }
		made->clear();
		ProjectMarkers::instance().restore(before);
	}, [made, after, model, again]() {
		buildQuietly(model, again, made.get());
		ProjectMarkers::instance().restore(after);
	});
	return *made;
}

QJsonObject buildJson(const aaf::AafBuildReport& report, const std::vector<int>& made)
{
	QJsonArray ids;
	for (const int id : made) { ids.append(trackId(id)); }
	return {{QStringLiteral("tracks"), ids}, {QStringLiteral("built_tracks"), report.tracks},
		{QStringLiteral("built_clips"), report.clips}, {QStringLiteral("built_markers"), report.markers},
		{QStringLiteral("skipped_missing_media"), report.missingMediaClips},
		{QStringLiteral("skipped_beyond_song_length"), report.beyondSongLength}};
}

ControlResult importFile(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	ControlResult error;
	aaf::AafReadOptions options;
	if (!acceptPath(path, &error) || !readImportMedia(args, path, &options, &error)) { return error; }
	aaf::AafModel model;
	aaf::AafLossReport loss;
	if (!readFile(path, options, &model, &loss, &error)) { return error; }
	if (model.tracks.isEmpty() && model.markers.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has no audio track and no marker to import").arg(path));
	}
	aaf::AafBuildOptions build;
	build.tempo = Engine::getSong()->getTempo();
	build.shiftFrames = args.value(QStringLiteral("start_at_first_clip")).toBool() ? model.firstClipFrame() : 0;
	aaf::AafBuildReport report;
	const std::vector<int> made = buildAsOneStep(model, build, &report);
	loss.beyondSongLength = report.beyondSongLength;
	loss.trackGainClamps = report.trackGainClamps;
	Engine::getSong()->setModified();
	QJsonObject result = summary(path, model, loss);
	const QJsonObject built = buildJson(report, made);
	for (auto it = built.constBegin(); it != built.constEnd(); ++it) { result.insert(it.key(), it.value()); }
	result.insert(QStringLiteral("media_dir"), options.extractDir);
	result.insert(QStringLiteral("shift_frames"), build.shiftFrames);
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("path"), path}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: the recorded undo step "
			"removes the imported tracks by their stable ids and restores the marker list; its redo builds the "
			"same model again (the extracted media stays on disk)")));
	return ControlResult::success(result);
}

} // namespace

void registerAafImportCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("interchange.aaf_convention"), QStringLiteral("Whether this build "
		"imports AAF and how, as data: 'available' (false in a build configured without LibAAF), the library "
		"and its version, the formats read (AAF; OMF is NOT - LibAAF does not parse its Bento container), the "
		"time rule (edit units -> frames at the composition's sample rate -> ticks at the project tempo), the "
		"media rule (embedded essence extracted to WAV, external essence found by path, a missing file listed) "
		"and the stated losses. Reads nothing; changes nothing."), {}, {}, false, convention);
	defineCommand(registry, QStringLiteral("interchange.aaf_read"), QStringLiteral("Read an AAF file's "
		"composition WITHOUT touching the session or the disk - the dry run of interchange.aaf_import: its "
		"audio tracks (name, gain, pan, mute) and clips (position, length and source offset in frames at the "
		"composition's sample rate, clip gain, gain curve, fades and their curves, mute), markers, the media "
		"each clip plays (embedded, found, or MISSING) and 'loss', what the import would not carry (video, "
		"automation, solo, multichannel splits and the rest). 'media_location' adds a directory LibAAF "
		"searches for external media. An OMF file is refused by name; a build without LibAAF refuses "
		"'unavailable'."), {{QStringLiteral("path"), stringProperty()}, {QStringLiteral("media_location"),
		stringProperty()}}, {QStringLiteral("path")}, false, readOnly);
	defineCommand(registry, QStringLiteral("interchange.aaf_import"), QStringLiteral("Import an AAF file into "
		"the session: one new sample track per audio track (name, constant gain as the volume, constant pan, "
		"mute), one sample clip per clip whose media is there (position, length, sample-accurate source "
		"offset, clip gain, gain curve, fades; crossfades as the fade pair they are) and one marker per "
		"marker, placed at the project tempo. Embedded media is extracted to WAV files in 'media_dir' "
		"(absolute; default beside the project, or beside the AAF for an unsaved project); 'media_location' "
		"adds a directory to search for external media; a missing file is listed in 'missing_media' and its "
		"clips are not built. 'start_at_first_clip' moves everything so the earliest clip starts at the song's "
		"start (an AAF's timeline often begins at timecode 01:00:00:00). Adds to the session, replacing "
		"nothing; one undo step removes what it added. An OMF file is refused by name; a build without LibAAF "
		"refuses 'unavailable'."), {{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("media_dir"), stringProperty()}, {QStringLiteral("media_location"), stringProperty()},
		{QStringLiteral("start_at_first_clip"), booleanProperty()}}, {QStringLiteral("path")}, true, importFile);
}

} // namespace lmms
