/*
 * ControlCommandsMasteringSequence.cpp - the mastering release sequence: songs, order, processing, gaps and fades
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
/*! 0.5.0 L12 (Q050-317.1, stage 1 of 3): the release sequence of a mastering document - the
 *  ordered songs of a release, each with its own processing (one of the engine's own mastering
 *  candidates, or "none"), fade-in, fade-out and the silence that follows it, and the commands
 *  that edit that list.
 *
 *  WHY THE CANDIDATE NAME AND NOT ITS NUMBERS. A song names a candidate from
 *  MasteringJob::defaultCandidates() ("streaming-14", "ebu-r128", ...). The numbers behind the
 *  name (loudness, ceiling, dynamics) are the engine's, derived when a render runs - so a saved
 *  song cannot carry a loudness target that disagrees with the candidate it says it uses, and a
 *  later change to the candidate set moves every song that names it. "none" is a song played as
 *  it is. Rendering is the next stage's acceptance criterion; this one is the state and its
 *  round trip.
 *
 *  EVERY mutating command records ONE undo step that puts the whole previous list back
 *  (control::addUndoStep with the before/after lists, the section.* group's mechanism), so a
 *  reorder, a field edit and a removal are each one Ctrl+Z. An edit that changes nothing records
 *  nothing, like section.set. The reads are not_mutating. */

#include <QFileInfo>
#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "LmmsTypes.h"
#include "MasteringJob.h"
#include "MasteringSequence.h"
#include "ProjectInfo.h"
#include "SampleDecoder.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

QString songId(int id) { return QStringLiteral("song-%1").arg(id); }

//! The candidate names a song may name, "none" first: the engine's own set, not a copy.
QStringList processingNames()
{
	QStringList names{QLatin1String(MasteringSequence::NoProcessing)};
	for (const MasteringCandidate& candidate : MasteringJob::defaultCandidates()) { names << candidate.name; }
	return names;
}

bool knownProcessing(const QString& name)
{
	return processingNames().contains(name);
}

QJsonObject songJson(const MasteringSong& song)
{
	const QFileInfo source(song.source);
	return {{QStringLiteral("song"), songId(song.id)}, {QStringLiteral("source"), song.source}, {QStringLiteral("title"), song.title},
		{QStringLiteral("processing"), song.processing}, {QStringLiteral("fade_in_ms"), song.fadeInMs},
		{QStringLiteral("fade_out_ms"), song.fadeOutMs}, {QStringLiteral("gap_ms"), song.gapMs},
		{QStringLiteral("exists"), source.isFile()}, {QStringLiteral("file_name"), source.fileName()}};
}

QJsonArray listJson()
{
	const QVector<MasteringSong>& songs = MasteringSequence::instance().songs();
	QJsonArray out;
	for (int index = 0; index < songs.size(); ++index)
	{
		QJsonObject entry = songJson(songs.at(index));
		entry.insert(QStringLiteral("index"), index);
		// The gap belongs to the song it follows, but it is only HEARD between two songs: the
		// last one's gap is the silence after the release, which a full-release render does not
		// write. Saying so here is what keeps the page from promising silence it will not get.
		entry.insert(QStringLiteral("gap_applies"), index + 1 < songs.size());
		out.append(entry);
	}
	return out;
}

QJsonObject sequenceJson()
{
	const QVector<MasteringSong>& songs = MasteringSequence::instance().songs();
	return {{QStringLiteral("songs"), listJson()}, {QStringLiteral("count"), songs.size()},
		{QStringLiteral("document_type"), ProjectInfo::instance().documentType()},
		{QStringLiteral("processing_names"), QJsonArray::fromStringList(processingNames())},
		{QStringLiteral("max_songs"), MasteringSequence::MaxSongs}};
}

//! The id the "song" argument names, or -1.
int songIndex(const QJsonObject& args)
{
	return idToIndex(args.value(QStringLiteral("song")).toString(), QStringLiteral("song-"));
}

//! Run @a edit and record the list before and after it as one undo step; the reply carries the
//! sequence as it is now. Nothing changed records nothing (as section.set does).
ControlResult recorded(const std::function<ControlResult()>& edit)
{
	MasteringSequence& sequence = MasteringSequence::instance();
	const QVector<MasteringSong> before = sequence.songs();
	ControlResult result = edit();
	if (!result.ok) { return result; }
	const QVector<MasteringSong> after = sequence.songs();
	result.result.insert(QStringLiteral("sequence"), sequenceJson());
	if (before == after) { return result; }
	addUndoStep([before] { MasteringSequence::instance().restore(before); }, [after] { MasteringSequence::instance().restore(after); });
	Engine::getSong()->setModified();
	return result;
}

//! @a args' optional millisecond field @a key, bounded by @a maximum; false (with the reason) when
//! it is given and out of range, so a typo is a refusal rather than a silent clamp.
bool optionalMs(const QJsonObject& args, const char* key, int current, int maximum, int* out, QString* error)
{
	if (!args.contains(QLatin1String(key))) { *out = current; return true; }
	const QJsonValue value = args.value(QLatin1String(key));
	const int number = value.toInt(-1);
	if (!value.isDouble() || number < 0 || number > maximum)
	{
		*error = QStringLiteral("'%1' is a whole number of milliseconds from 0 to %2").arg(QLatin1String(key)).arg(maximum);
		return false;
	}
	*out = number;
	return true;
}

//! The audio extensions this build decodes: the decoders' own list, plus the container types
//! the engine reads through them. One lookup used by audioSource().
bool decodesSuffix(const QString& suffix)
{
	static const QStringList containers{QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("flac"),
		QStringLiteral("mp3"), QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("opus")};
	if (containers.contains(suffix)) { return true; }
	for (const SampleDecoder::AudioType& type : SampleDecoder::supportedAudioTypes())
	{
		if (suffix == QString::fromStdString(type.extension).toLower()) { return true; }
	}
	return false;
}

//! The audio file @a path names, absolute in @a absolute; false with a typed @a error otherwise.
//! A song pointing at nothing is a release that fails only when someone tries to render it.
bool audioSource(const QString& path, QString* absolute, ControlResult* error)
{
	const QFileInfo file(path);
	if (path.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'source' is the audio file this song is"));
		return false;
	}
	if (!file.isFile())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no audio file '%1'").arg(path));
		return false;
	}
	if (!decodesSuffix(file.suffix().toLower()))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'source' is an audio file this build decodes, not '%1'").arg(file.fileName()));
		return false;
	}
	*absolute = file.absoluteFilePath();
	return true;
}

//! The song @a args names, or nullptr with a typed error.
const MasteringSong* addressed(const QJsonObject& args, ControlResult* error)
{
	const int id = songIndex(args);
	const MasteringSong* found = MasteringSequence::instance().find(id);
	if (found == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no song '%1'").arg(args.value(QStringLiteral("song")).toString()));
	}
	return found;
}

ControlResult listSongs(const QJsonObject&)
{
	return ControlResult::success(sequenceJson());
}

ControlResult addSong(const QJsonObject& args)
{
	QString source;
	ControlResult error;
	if (!audioSource(args.value(QStringLiteral("source")).toString(), &source, &error)) { return error; }
	MasteringSong song;
	song.source = source;
	song.title = args.value(QStringLiteral("title")).toString();
	const QString processing = args.value(QStringLiteral("processing")).toString(QLatin1String(MasteringSequence::NoProcessing));
	if (!knownProcessing(processing))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'processing' is one of %1").arg(processingNames().join(QLatin1Char(','))));
	}
	song.processing = processing;
	QString why;
	if (!optionalMs(args, "fade_in_ms", 0, MasteringSequence::MaxFadeMs, &song.fadeInMs, &why)
		|| !optionalMs(args, "fade_out_ms", 0, MasteringSequence::MaxFadeMs, &song.fadeOutMs, &why)
		|| !optionalMs(args, "gap_ms", 0, MasteringSequence::MaxGapMs, &song.gapMs, &why))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, why);
	}
	if (MasteringSequence::instance().songs().size() >= MasteringSequence::MaxSongs)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("a release holds at most %1 songs").arg(MasteringSequence::MaxSongs));
	}
	return recorded([&] {
		const int id = MasteringSequence::instance().add(song);
		if (id < 0) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the song could not be added")); }
		return ControlResult::success(songJson(*MasteringSequence::instance().find(id)));
	});
}

ControlResult setSong(const QJsonObject& args)
{
	ControlResult error;
	const MasteringSong* found = addressed(args, &error);
	if (found == nullptr) { return error; }
	const int id = found->id;
	MasteringSong changed = *found;
	if (args.contains(QStringLiteral("source")))
	{
		QString source;
		if (!audioSource(args.value(QStringLiteral("source")).toString(), &source, &error)) { return error; }
		changed.source = source;
	}
	changed.title = args.value(QStringLiteral("title")).toString(changed.title);
	if (args.contains(QStringLiteral("processing")))
	{
		const QString processing = args.value(QStringLiteral("processing")).toString();
		if (!knownProcessing(processing))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'processing' is one of %1").arg(processingNames().join(QLatin1Char(','))));
		}
		changed.processing = processing;
	}
	QString why;
	if (!optionalMs(args, "fade_in_ms", changed.fadeInMs, MasteringSequence::MaxFadeMs, &changed.fadeInMs, &why)
		|| !optionalMs(args, "fade_out_ms", changed.fadeOutMs, MasteringSequence::MaxFadeMs, &changed.fadeOutMs, &why)
		|| !optionalMs(args, "gap_ms", changed.gapMs, MasteringSequence::MaxGapMs, &changed.gapMs, &why))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, why);
	}
	if (*MasteringSequence::instance().find(id) == changed)
	{
		return ControlResult::success(sequenceJson());  // an edit to the values it already has
	}
	return recorded([id, changed] {
		MasteringSequence::instance().replace(id, changed);
		return ControlResult::success(songJson(*MasteringSequence::instance().find(id)));
	});
}

ControlResult moveSong(const QJsonObject& args)
{
	ControlResult error;
	const MasteringSong* found = addressed(args, &error);
	if (found == nullptr) { return error; }
	const int id = found->id;
	const int to = args.value(QStringLiteral("to")).toInt(-1);
	if (!args.contains(QStringLiteral("to")) || to < 0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'to' is the 0-based release position the song moves to"));
	}
	return recorded([&] {
		const bool moved = MasteringSequence::instance().move(id, to);
		return ControlResult::success({{QStringLiteral("song"), songId(id)}, {QStringLiteral("index"), MasteringSequence::instance().indexOf(id)},
			{QStringLiteral("moved"), moved}});
	});
}

ControlResult removeSong(const QJsonObject& args)
{
	ControlResult error;
	const MasteringSong* found = addressed(args, &error);
	if (found == nullptr) { return error; }
	const int id = found->id;
	return recorded([&] {
		MasteringSequence::instance().remove(id);
		return ControlResult::success({{QStringLiteral("removed"), songId(id)}});
	});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	const QStringList& required, bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("mastering.") + QLatin1String(verb), description,
		objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

QJsonObject msProperty(int maximum)
{
	return integerProperty(0, maximum);
}

} // namespace

void registerMasteringSequenceCommands(ControlRegistry& registry)
{
	MasteringSequence::instance();  // registers the project section before a project loads
	const QJsonObject song{{QStringLiteral("song"), stringProperty()}};
	QJsonObject edit = song;
	edit.insert(QStringLiteral("source"), stringProperty());
	edit.insert(QStringLiteral("title"), stringProperty());
	edit.insert(QStringLiteral("processing"), stringProperty());
	edit.insert(QStringLiteral("fade_in_ms"), msProperty(MasteringSequence::MaxFadeMs));
	edit.insert(QStringLiteral("fade_out_ms"), msProperty(MasteringSequence::MaxFadeMs));
	edit.insert(QStringLiteral("gap_ms"), msProperty(MasteringSequence::MaxGapMs));

	add(registry, "sequence_list", QStringLiteral("The mastering release sequence: the songs in release order, each with its "
		"stable id (song-<n>), its audio 'source', its 'title', the 'processing' it is mastered with (a name from "
		"mastering.list_candidates, or \"none\"), its fade-in and fade-out in milliseconds, and the 'gap_ms' of silence "
		"that follows it. 'gap_applies' is false for the last song, whose gap is the silence after the release and is not "
		"inserted between two songs. Also names every 'processing' value a song may take, and the document type. "
		"Read-only."), {}, {}, false, listSongs);
	add(registry, "song_add", QStringLiteral("Add an audio file to the end of the mastering release as 'source', with an "
		"optional 'title', 'processing' (a candidate name or \"none\"; default \"none\"), 'fade_in_ms', 'fade_out_ms' and "
		"'gap_ms'. The file must exist and be one this build decodes; the path is stored absolute. One undo step."),
		edit, {QStringLiteral("source")}, true, addSong);
	add(registry, "song_set", QStringLiteral("Change the fields of song 'song' (only the fields given change): 'source', "
		"'title', 'processing', 'fade_in_ms', 'fade_out_ms', 'gap_ms'. The song's id and its place in the release are "
		"kept. One undo step."), edit, {QStringLiteral("song")}, true, setSong);
	add(registry, "song_move", QStringLiteral("Move song 'song' to release position 'to' (0-based); every other song keeps "
		"its order around it. One undo step."),
		{{QStringLiteral("song"), stringProperty()}, {QStringLiteral("to"), integerProperty(0, MasteringSequence::MaxSongs - 1)}},
		{QStringLiteral("song"), QStringLiteral("to")}, true, moveSong);
	add(registry, "song_remove", QStringLiteral("Remove song 'song' from the release sequence. The audio file is not "
		"touched. One undo step."), song, {QStringLiteral("song")}, true, removeSong);
}

} // namespace lmms
