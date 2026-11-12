/*
 * ControlCommandsClipImportFile.cpp - clip.import_file: a MIDI or audio file becomes a clip
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
/*! 0.5.0 L04 (Q050-031): clip.import_file, the one command behind a file dragged into the arranger - from a
 *  plug-in editor's window, the desktop or the browser - and behind an agent importing the same file:
 *  - a Standard MIDI File becomes ONE MIDI clip holding all its notes, on an instrument track;
 *  - an audio file becomes ONE sample clip on a sample track;
 *  at `position`, on `track`, or - with no `track` - on a NEW track of the right kind named after the file.
 *  EVERYTHING that can fail (the file missing, not a file, too large, of a type nothing here reads, malformed,
 *  empty, past the end of the song; the track of the wrong kind or locked) is checked BEFORE the first change,
 *  so a refusal leaves the project, its undo stack and its tracks exactly as they were - there is no partial
 *  clip to clean up. One call is one undo step. Audio from the system's temporary folder - what a plug-in
 *  writes for a drag and deletes right after - is embedded in the clip (the way a generated or dragged buffer
 *  is), so the clip plays, saves and reloads when the file is gone; any other file is referenced by path, as
 *  the browser's drag does.
 *  0.5.0 L05 (Q050-093): every audio import is analysed (tempo and key, LoopImportWarp.h) and takes a loop mode -
 *  `loop_mode` follow_project or keep_unstretched, or the remembered preference when absent - inside the same one
 *  undo step; clip.source_get_state reads back what was detected and what the clip plays. */

#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlTrackCreation.h"
#include "ControlTrackRouting.h"
#include "ControlVocabulary.h"
#include "EditLocks.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "LoopImportWarp.h"
#include "MidiClip.h"
#include "PathUtil.h"
#include "PluginFactory.h"
#include "ProjectKey.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleDecoder.h"
#include "SampleTrack.h"
#include "SmfNoteRead.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr qint64 MaxAudioFileBytes = 512LL * 1024 * 1024;

//! What the file turned out to be, with everything needed to make its clip - read and decoded in full.
struct Prepared
{
	bool midi = false;
	QString name;
	QString path;
	interchange::SmfNoteRead notes;                  //!< midi
	std::shared_ptr<const SampleBuffer> buffer;      //!< audio
	qint64 lengthTicks = 0;                          //!< the clip's length: exact for MIDI, an estimate for audio
	bool embedded = false;                           //!< audio read from the temporary folder
	LoopSource loop;                                 //!< audio: what the detector found (Q050-093)
	QString analysisError;                           //!< audio: why the detector found nothing at all
	loopwarp::Mode loopMode = loopwarp::Mode::KeepUnstretched;
	bool loopModeFromPreference = true;
};

bool isMidiFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) && file.read(4) == QByteArrayLiteral("MThd");
}

bool isAudioExtension(const QString& suffix)
{
	static const QStringList decoded{QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("flac"),
		QStringLiteral("mp3"), QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("opus")};
	if (decoded.contains(suffix)) { return true; }
	for (const SampleDecoder::AudioType& type : SampleDecoder::supportedAudioTypes())
	{
		if (suffix == QString::fromStdString(type.extension).toLower()) { return true; }
	}
	return false;
}

//! Whether \a path lies in the system's temporary folder, where a plug-in puts a file it deletes after the drag.
bool inTemporaryFolder(const QString& path)
{
	const QString file = QFileInfo(path).canonicalFilePath();
	for (const QString& root : {QDir::tempPath(), QStandardPaths::writableLocation(QStandardPaths::TempLocation)})
	{
		const QString canonical = QFileInfo(root).canonicalFilePath();
		if (!canonical.isEmpty() && file.startsWith(canonical + QLatin1Char('/'))) { return true; }
	}
	return false;
}

ControlResult invalid(const QString& message) { return ControlResult::failure(ControlErrorKind::InvalidArgs, message); }

//! The file's own checks, shared by both kinds. \a error is filled and false answered when it cannot be read.
bool checkFile(const QString& path, ControlResult* error)
{
	const QFileInfo info(path);
	if (!info.exists())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("there is no file at '%1'").arg(path));
		return false;
	}
	if (!info.isFile())
	{
		*error = invalid(QStringLiteral("'%1' is not a file").arg(path));
		return false;
	}
	if (!info.isReadable() || info.size() <= 0)
	{
		*error = invalid(QStringLiteral("'%1' is empty or cannot be read").arg(path));
		return false;
	}
	return true;
}

bool prepareMidi(const QString& path, Prepared* out, ControlResult* error)
{
	if (QFileInfo(path).size() > interchange::SmfNoteMaxBytes)
	{
		*error = invalid(QStringLiteral("'%1' is larger than the MIDI files read here (16 MiB)").arg(path));
		return false;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = invalid(QStringLiteral("'%1' cannot be opened: %2").arg(path, file.errorString()));
		return false;
	}
	out->notes = interchange::readSmfNotes(file.readAll());
	if (!out->notes.ok)
	{
		*error = invalid(QStringLiteral("'%1' was not imported: %2").arg(QFileInfo(path).fileName(), out->notes.error));
		return false;
	}
	out->midi = true;
	// Refused, not clamped: a clamped length would let notes past the song's end through to fillMidi.
	if (out->notes.endTick > static_cast<long long>(MaxSongLength))
	{
		*error = invalid(QStringLiteral("'%1' runs to tick %2, past the song's limit of %3")
			.arg(QFileInfo(path).fileName()).arg(out->notes.endTick).arg(MaxSongLength));
		return false;
	}
	out->lengthTicks = out->notes.endTick;
	return true;
}

bool prepareAudio(const QString& path, Prepared* out, ControlResult* error)
{
	if (QFileInfo(path).size() > MaxAudioFileBytes)
	{
		*error = invalid(QStringLiteral("'%1' is larger than the 512 MiB of audio read here").arg(path));
		return false;
	}
	auto decoded = SampleDecoder::decode(path);
	if (!decoded || decoded->data.empty() || decoded->sampleRate <= 0)
	{
		*error = invalid(QStringLiteral("'%1' was not imported: the audio is damaged, empty or of a kind this build cannot decode")
			.arg(QFileInfo(path).fileName()));
		return false;
	}
	const double ticks = static_cast<double>(decoded->data.size()) / static_cast<double>(Engine::framesPerTick(decoded->sampleRate));
	out->lengthTicks = static_cast<qint64>(std::min<double>(ticks, static_cast<double>(MaxSongLength) + 1.0));
	out->embedded = inTemporaryFolder(path);
	out->buffer = std::make_shared<SampleBuffer>(std::move(decoded->data), decoded->sampleRate,
		out->embedded ? QString() : PathUtil::toShortestRelative(path));
	// Q050-093: never a refusal - a file the detector cannot read is still imported, and says so.
	out->loop = loopwarp::analyse(path, &out->analysisError);
	return true;
}

//! Reads and validates the file into \a out; nothing in the session is touched.
bool prepareFile(const QString& path, Prepared* out, ControlResult* error)
{
	if (!checkFile(path, error)) { return false; }
	out->path = path;
	out->name = QFileInfo(path).completeBaseName();
	if (isMidiFile(path)) { return prepareMidi(path, out, error); }
	if (isAudioExtension(QFileInfo(path).suffix().toLower())) { return prepareAudio(path, out, error); }
	*error = invalid(QStringLiteral("'%1' is neither a MIDI file nor audio this build reads").arg(QFileInfo(path).fileName()));
	return false;
}

//! The track the clip goes on, checked against the file's kind: nullptr with \a error filled when it cannot.
Track* targetTrack(const QJsonObject& args, const Prepared& file, bool* created, ControlResult* error)
{
	*created = false;
	if (!args.contains(QStringLiteral("track"))) { return nullptr; }
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), error);
	if (track == nullptr) { return nullptr; }
	const bool fits = file.midi ? dynamic_cast<InstrumentTrack*>(track) != nullptr : dynamic_cast<SampleTrack*>(track) != nullptr;
	if (track->trackContainer() != Engine::getSong() || !fits)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 cannot hold %2: a MIDI file goes on an instrument track and an audio file on a sample track")
				.arg(track->name(), file.midi ? QStringLiteral("a MIDI file") : QStringLiteral("audio")));
		return nullptr;
	}
	if (EditLocks::instance().trackLocked(track->id()))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 is locked").arg(track->name()));
		return nullptr;
	}
	return track;
}

//! The new track a file with no `track` argument gets: an instrument track with a synth where one is installed.
Track* newTrackFor(const Prepared& file)
{
	Track* track = Track::create(file.midi ? Track::Type::Instrument : Track::Type::Sample, Engine::getSong());
	if (track == nullptr) { return nullptr; }
	track->setName(file.name);
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
	{
		if (!getPluginFactory()->pluginInfo("tripleoscillator").isNull()) { instrument->loadInstrument(QStringLiteral("tripleoscillator")); }
	}
	return track;
}

void fillMidi(MidiClip* clip, const Prepared& file)
{
	for (const interchange::SmfNote& note : file.notes.notes)
	{
		const auto velocity = static_cast<volume_t>(std::max<long>(1, std::lround(note.velocity * 200.0 / 127.0)));
		clip->addNote(Note(TimePos(static_cast<tick_t>(note.length)), TimePos(static_cast<tick_t>(note.start)), note.key, velocity), false);
	}
	clip->rearrangeAllNotes();
	clip->updateLength();
}

QJsonObject clipReport(Clip* clip, Track* track, const Prepared& file, bool created)
{
	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(clip->id()));
	result.insert(QStringLiteral("track"), trackIdOf(track));
	result.insert(QStringLiteral("kind"), file.midi ? QStringLiteral("midi") : QStringLiteral("audio"));
	result.insert(QStringLiteral("path"), file.path);
	result.insert(QStringLiteral("position"), clip->startPosition().getTicks());
	result.insert(QStringLiteral("length"), clip->length().getTicks());
	result.insert(QStringLiteral("track_created"), created);
	result.insert(QStringLiteral("note_count"), file.midi ? static_cast<int>(file.notes.notes.size()) : 0);
	result.insert(QStringLiteral("embedded"), file.embedded);
	return result;
}

//! Q050-093: the loop mode the import takes - the `loop_mode` argument, or the remembered preference.
bool readLoopMode(const QJsonObject& args, Prepared* file, ControlResult* error)
{
	const QString name = args.value(QStringLiteral("loop_mode")).toString(QStringLiteral("preference"));
	file->loopModeFromPreference = name == QStringLiteral("preference");
	if (file->loopModeFromPreference) { file->loopMode = loopwarp::preferredMode(); return true; }
	if (loopwarp::modeFromName(name, &file->loopMode)) { return true; }
	*error = invalid(QStringLiteral("'loop_mode' must be follow_project, keep_unstretched or preference"));
	return false;
}

//! Q050-093: the new sample clip follows the project or keeps its own rate; the report says what was applied.
QJsonObject applyLoopMode(SampleClip* clip, const Prepared& file)
{
	QStringList notes;
	if (!file.loop.analysed) { notes.append(QStringLiteral("the file could not be analysed: %1").arg(file.analysisError)); }
	notes += loopwarp::apply(clip, file.loop, file.loopMode, Engine::getSong()->projectKey());
	QJsonObject loop = loopwarp::clipStateJson(*clip);
	loop.insert(QStringLiteral("loop_mode"), loopwarp::modeName(file.loopMode));
	loop.insert(QStringLiteral("mode_from"), file.loopModeFromPreference ? QStringLiteral("preference") : QStringLiteral("argument"));
	loop.insert(QStringLiteral("notes"), QJsonArray::fromStringList(notes));
	return loop;
}

//! The clip's preflight duration after applyTempo, or its source duration when this import will not follow a tempo.
qint64 resultingLengthTicks(const Prepared& file)
{
	if (file.midi || file.loopMode != loopwarp::Mode::FollowProject || !file.loop.tempoFound) { return file.lengthTicks; }
	const float sourceTempo = static_cast<float>(std::lround(file.loop.bpm));
	const float projectTempo = static_cast<float>(Engine::getSong()->getTempo());
	if (sourceTempo <= 0.0f || projectTempo <= 0.0f) { return file.lengthTicks; }
	const float framesPerTick = Engine::framesPerTick(file.buffer->sampleRate()) * projectTempo / sourceTempo;
	if (framesPerTick <= 0.0f) { return file.lengthTicks; }
	return static_cast<qint64>(static_cast<double>(file.buffer->size()) / framesPerTick);
}

//! Every check of an import, before the first change: the file read in full, the clip's end inside the song, the
//! track (if one is named) of the right kind and not locked. False with \a error filled when any fails.
bool validateImport(const QJsonObject& args, Prepared* file, Track** track, ControlResult* error)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	if (!QFileInfo(path).isAbsolute())
	{
		*error = invalid(QStringLiteral("'path' must be an absolute path"));
		return false;
	}
	const auto position = static_cast<tick_t>(args.value(QStringLiteral("position")).toDouble(0.0));
	if (!readLoopMode(args, file, error)) { return false; }
	if (!prepareFile(path, file, error)) { return false; }
	if (static_cast<qint64>(position) + resultingLengthTicks(*file) > static_cast<qint64>(MaxSongLength))
	{
		*error = invalid(QStringLiteral("a clip at tick %1 would end past the song's limit of %2").arg(position).arg(MaxSongLength));
		return false;
	}
	bool created = false;
	*track = targetTrack(args, *file, &created, error);
	return *track != nullptr || !args.contains(QStringLiteral("track"));
}

//! Records the undo step and describes it in the transaction payload; \a before is the state ahead of the change.
void recordImport(QJsonObject* result, Clip* clip, Track* track, const QJsonObject& before, bool created)
{
	QJsonObject inverseArgs;
	if (created)
	{
		recordCreatedRoutedTrack(track);
		inverseArgs.insert(QStringLiteral("track"), trackIdOf(track));
		result->insert(QStringLiteral("__transaction"), transactionPayload(before, QStringLiteral("track.remove"), inverseArgs, true,
			QStringLiteral("action checkpoint: the recorded undo step removes the created track, and its clip with it")));
		return;
	}
	inverseArgs.insert(QStringLiteral("clip"), clipId(clip->id()));
	result->insert(QStringLiteral("__transaction"), transactionPayload(before, QStringLiteral("clip.delete"), inverseArgs, true,
		QStringLiteral("ProjectJournal (Track checkpoint)")));
}

ControlResult importFile(const QJsonObject& args)
{
	ControlResult error;
	Prepared file;
	Track* track = nullptr;
	if (!validateImport(args, &file, &track, &error)) { return error; }

	// From here on nothing is refused: every check above has passed and the file is read in full.
	QJsonObject before;
	before.insert(QStringLiteral("track_count"), static_cast<int>(Engine::getSong()->tracks().size()));
	const bool created = track == nullptr;
	if (created)
	{
		track = newTrackFor(file);
		if (track == nullptr) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the track could not be created")); }
	}
	else
	{
		before.insert(QStringLiteral("clip_count"), track->numOfClips());
		track->addJournalCheckPoint();
	}
	Clip* clip = track->createClip(TimePos(static_cast<tick_t>(args.value(QStringLiteral("position")).toDouble(0.0))));
	if (clip == nullptr)
	{
		if (created) { removeTrack(track); }
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip could not be created"));
	}
	clip->setName(file.name);
	if (file.midi) { fillMidi(static_cast<MidiClip*>(clip), file); }
	QJsonObject loop;
	if (!file.midi)
	{
		static_cast<SampleClip*>(clip)->setSampleBuffer(file.buffer);
		loop = applyLoopMode(static_cast<SampleClip*>(clip), file);
	}

	QJsonObject result = clipReport(clip, track, file, created);
	if (!file.midi) { result.insert(QStringLiteral("loop"), loop); }
	recordImport(&result, clip, track, before, created);
	Engine::getSong()->setModified();
	return ControlResult::success(result);
}

//! Q050-093: clip.source_get_state - what an import detected in a sample clip's file, and what the clip plays now.
void registerSourceGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.source_get_state");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("source_get_state");
	cmd.description = QStringLiteral("Read a sample clip's loop source: whether clip.import_file analysed its file, the detected "
		"source tempo (BPM) and key (tonic pitch class and scale), the loop mode the import took (follow_project or "
		"keep_unstretched), whether the tempo and the key were followed and the semitones applied - and what the clip plays "
		"now: its warp tempo mode (source = leads at warp_source_tempo, follow = its own rate), stretch mode, transpose and "
		"length in ticks. Writes nothing.");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}}, {QStringLiteral("clip")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("analysed"), booleanProperty()},
		{QStringLiteral("source_tempo"), objectProperty()},
		{QStringLiteral("source_key"), objectProperty()},
		{QStringLiteral("loop_mode"), stringProperty()},
		{QStringLiteral("tempo_followed"), booleanProperty()},
		{QStringLiteral("key_followed"), booleanProperty()},
		{QStringLiteral("transpose_applied"), integerProperty()},
		{QStringLiteral("tempo_mode"), stringProperty()},
		{QStringLiteral("warp_source_tempo"), numberProperty()},
		{QStringLiteral("stretch"), stringProperty()},
		{QStringLiteral("transpose"), integerProperty()},
		{QStringLiteral("length"), integerProperty()},
	});
	cmd.handler = [](const QJsonObject& args) {
		ClipRef ref;
		ControlResult error;
		if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
		const auto* sample = dynamic_cast<const SampleClip*>(ref.clip);
		if (sample == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 is not a sample clip").arg(clipId(ref.id)));
		}
		QJsonObject state = loopwarp::clipStateJson(*sample);
		state.insert(QStringLiteral("clip"), clipId(ref.id));
		return ControlResult::success(state);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerClipImportFileCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.import_file");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("import_file");
	cmd.description = QStringLiteral("Make a clip of a file: a Standard MIDI File becomes one MIDI clip holding all its notes "
		"(on an instrument track), an audio file one sample clip (on a sample track), at 'position' ticks. With 'track' the clip "
		"goes on that track, which must be of the kind the file needs; without it a NEW track of that kind, named after the file, "
		"is created. This is what dropping a file on the arranger runs - from a plug-in editor, the desktop or the browser. "
		"Everything that can fail is checked before anything changes: a missing, empty, malformed or unsupported file, a clip that "
		"would end past the song, a track of the wrong kind or a locked one is refused and the project is left exactly as it was. "
		"Audio read from the temporary folder (a plug-in's drag file, deleted right after) is embedded in the clip; other audio is "
		"referenced by path. One undo step removes the clip (and a track it created). An audio file is also analysed for its "
		"tempo and key (the detect.analyze detector) and takes a loop mode: 'loop_mode' follow_project makes the clip lead at "
		"its detected tempo (rounded to a whole BPM) so its beats land on the project grid, pitch kept, and transposes its "
		"tonic to the project key's tonic; keep_unstretched leaves its own rate and pitch; preference (the default) takes the "
		"remembered choice, config key import/loopwarp. The project's tempo and key are never changed; what could not be "
		"applied is listed in loop.notes. clip.source_get_state reads the clip's detected and applied values back.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("position"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("loop_mode"), enumProperty({QStringLiteral("follow_project"), QStringLiteral("keep_unstretched"),
			QStringLiteral("preference")})},
	}, {QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("kind"), enumProperty({QStringLiteral("midi"), QStringLiteral("audio")})},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("position"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("track_created"), booleanProperty()},
		{QStringLiteral("note_count"), integerProperty()},
		{QStringLiteral("embedded"), booleanProperty()},
		{QStringLiteral("loop"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = importFile;
	registry.registerCommand(cmd);
	registerSourceGetState(registry);
}

} // namespace lmms
