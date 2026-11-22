/*
 * ControlCommandsLibraryClips.cpp - a clip saved into the user library
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
/*! 0.5.0 L11 (WF-022): browser.save_clip - a clip written into the library the browser reads, so it can
 *  be found, tagged, previewed and dragged into another song. A sample clip is written as the audio it
 *  plays (its window, warps, gain and fades - ClipConsolidate's render, a 32-bit float WAV); a MIDI clip
 *  as a Standard MIDI File (type 0, 48 ticks per quarter - the song's own grid - at the song's tempo).
 *  The default folder is <user samples>/Clips, which the browser's Samples source lists; the library
 *  is outside the project, so one undo step deletes the file again (and takes off the tags it added).
 *
 *  Q050-273.1: the same command also saves a POPULATED AUDIO LAUNCHER CELL, addressed by 'track' +
 *  'scene' instead of 'clip'. A cell is not an arrangement Clip - it holds a source PATH - so its
 *  file is written as the library's 32-bit float WAV at the source's own rate, with the source's own
 *  frames: the entry is the MEDIA. The cell's gain, transpose, detune, loop region and warp are
 *  playback settings of the cell, not bytes in the file, so they are deliberately not baked in;
 *  inserting the entry into another cell plays the same frames. The decoded-size bound and the atomic
 *  write live in LibraryCellAudio.cpp, so a compressed file that would decode past the bound is
 *  refused before it is decoded and every failure leaves the destination as it was. Everything else -
 *  the folder, 'name', 'tags', the overwrite refusal and the one undo step - is the clip path's, so
 *  the two routes cannot drift apart.
 *
 *  Q050-273.2: a MIDI launcher cell is saved too. Its music is the PatternStore column the cell
 *  references - every note on that column, across the store's tracks - written as one type-0 SMF at
 *  the song's tempo by LibraryCellMidi.cpp. The file is the notes, not the cell's pattern-track
 *  settings, so inserting the entry elsewhere plays the same music. A column with no notes is
 *  refused: an empty entry is not reusable. */

#include <algorithm>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>

#include "BrowserCatalog.h"
#include "ClipConsolidate.h"
#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "LibraryCellAudio.h"
#include "LibraryCellMidi.h"
#include "MidiSmfWrite.h"
#include "Midi.h"
#include "MidiClip.h"
#include "Note.h"
#include "PathUtil.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "SessionModel.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

//! @a clip's notes as a type-0 Standard MIDI File at @a bpm; empty when the writer refuses them.
std::vector<midismf::Note> smfNotesOf(const MidiClip* clip)
{
	std::vector<midismf::Note> notes;
	notes.reserve(static_cast<size_t>(clip->notes().size()));
	for (const Note* note : clip->notes())
	{
		notes.push_back(midismf::Note{std::max<tick_t>(0, note->pos().getTicks()),
			std::max<tick_t>(1, note->length().getTicks()), note->key(), note->midiVelocity(MidiDefaultVelocity)});
	}
	return notes;
}

//! Write @a clip to @a path (the extension already chosen by its kind); false with @a error set.
bool writeClip(Clip* clip, const QString& path, QString* error)
{
	if (auto* sample = dynamic_cast<SampleClip*>(clip))
	{
		const ClipConsolidate::Result rendered = ClipConsolidate::render({sample}, sample->startPosition().getTicks(),
			sample->endPosition().getTicks(), path);
		*error = rendered.error;
		return rendered.ok;
	}
	// Written whole or not at all: a short write must not leave a truncated file (or replace the old one).
	const midismf::Result written = midismf::write(smfNotesOf(static_cast<MidiClip*>(clip)),
		Engine::getSong()->tempoModel().value());
	if (!written.ok)
	{
		*error = written.error;
		return false;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(written.bytes) != written.bytes.size() || !file.commit())
	{
		*error = QStringLiteral("cannot write %1").arg(path);
		return false;
	}
	return true;
}

bool existsWithoutOverwrite(const QFileInfo& target, const QJsonObject& args)
{
	return target.exists() && !args.value(QStringLiteral("overwrite")).toBool(false);
}

//! The file @a args asks for: its folder ('dir' or <user samples>/Clips), name and the subject's extension.
bool targetPath(const QJsonObject& args, bool audio, const QString& defaultName, QString* path, ControlResult* error)
{
	const QString dir = args.value(QStringLiteral("dir")).toString(ConfigManager::inst()->userSamplesDir() + QStringLiteral("Clips"));
	QString name = args.value(QStringLiteral("name")).toString(defaultName).trimmed();
	if (name.isEmpty()) { name = QStringLiteral("library-entry"); }
	if (!QFileInfo(dir).isAbsolute() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.startsWith(QLatin1Char('.')))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'dir' must be absolute and 'name' a plain file name"));
		return false;
	}
	if (!QDir().mkpath(dir))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot create %1").arg(dir));
		return false;
	}
	*path = QDir(dir).filePath(name + (audio ? QStringLiteral(".wav") : QStringLiteral(".mid")));
	const QFileInfo target(*path);
	if (target.isSymLink())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 is a symbolic link").arg(*path));
		return false;
	}
	if (existsWithoutOverwrite(target, args))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 exists; pass overwrite to replace it").arg(*path));
		return false;
	}
	return true;
}

//! Tag @a path with every tag of @a tags it does not carry yet; the tags added.
QStringList addTags(const QString& path, const QJsonArray& tags)
{
	QStringList added;
	for (const QJsonValue& value : tags)
	{
		const QString tag = value.toString().trimmed();
		if (!tag.isEmpty() && BrowserTagStore::instance().add(browserCanonicalKey(path), tag)) { added << tag; }
	}
	return added;
}

bool tagsAreStrings(const QJsonArray& tags)
{
	for (const QJsonValue& tag : tags)
	{
		if (!tag.isString()) { return false; }
	}
	return true;
}

bool canSaveClip(const Clip* clip)
{
	return dynamic_cast<const SampleClip*>(clip) != nullptr || dynamic_cast<const MidiClip*>(clip) != nullptr;
}

//! What @a path holds right now, and whether it held anything at all: an overwrite
//! destroys the file it replaces, so undo has to put that file back.
struct PreviousFile
{
	QByteArray bytes;
	bool existed = false;
	//! False when the file exists but could not be read: an overwrite then has nothing it can put back.
	bool readable = true;
};

PreviousFile readBackIfAny(const QString& path)
{
	PreviousFile previous;
	// A dangling link counts as something there: it reads as missing, but a save would replace it.
	previous.existed = QFileInfo::exists(path) || QFileInfo(path).isSymLink();
	if (previous.existed)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			previous.readable = false;
			return previous;
		}
		previous.bytes = file.readAll();
		previous.readable = file.error() == QFileDevice::NoError;
	}
	return previous;
}

//! Undo of a save: @a previous back where it was, or the file gone when there was none.
void restoreOrRemove(const PreviousFile& previous, const QString& path)
{
	if (!previous.existed)
	{
		QFile::remove(path);
		return;
	}
	QSaveFile file(path);
	if (file.open(QIODevice::WriteOnly) && file.write(previous.bytes) == previous.bytes.size()) { file.commit(); }
}

/*! What one call saves: an arrangement Clip ('clip'), or the audio a populated LAUNCHER CELL
 *  plays ('track' + 'scene'). The two are different objects - a cell stores a source path - so
 *  the subject keeps the shape each write needs, and the undo/redo closures capture those. */
struct SaveSubject
{
	Clip* clip = nullptr;    //!< the arrangement clip, when the call named one
	int clipId = -1;         //!< its persistent id, captured for redo (a deleted Clip is never dereferenced)
	QString source;          //!< the cell's audio file, absolute; empty for a clip or a MIDI cell
	int pattern = -1;        //!< the MIDI cell's PatternStore column
	QString defaultName;     //!< the entry's name when 'name' is left out
	bool audio = false;      //!< the entry is a .wav (audio) or a .mid (MIDI)
	bool cell = false;       //!< the subject is a launcher cell, not a Clip
};

//! Whether @a target and @a source are the same file (a cell saved onto its own audio): a write there
//! would read one file and rename another over it, and on Windows could not even open the temporary.
bool sameFile(const QString& target, const QString& source)
{
	const QFileInfo a(target);
	const QFileInfo b(source);
	if (a.exists() && b.exists())
	{
		const QString canonicalA = a.canonicalFilePath();
		const QString canonicalB = b.canonicalFilePath();
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
		return canonicalA.compare(canonicalB, Qt::CaseInsensitive) == 0 && !canonicalA.isEmpty();
#else
		return canonicalA == canonicalB && !canonicalA.isEmpty();
#endif
	}
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
	return QDir::cleanPath(a.absoluteFilePath()).compare(QDir::cleanPath(b.absoluteFilePath()), Qt::CaseInsensitive) == 0;
#else
	return QDir::cleanPath(a.absoluteFilePath()) == QDir::cleanPath(b.absoluteFilePath());
#endif
}

//! Whether @a slot at (track, scene) is one this command can save; false with the typed refusal in @a error.
bool cellSavable(const ClipSlot& slot, int track, int scene, ControlResult* error)
{
	if (slot.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the cell at track %1, scene %2 is empty: there is nothing to save").arg(track).arg(scene));
		return false;
	}
	return true;
}

//! The entry's name: the cell's own, else the source file's, else the coordinates.
QString cellEntryName(const ClipSlot& slot, const QString& source, int track, int scene)
{
	QString name = slot.name().trimmed();
	if (name.isEmpty()) { name = QFileInfo(source).completeBaseName(); }
	return name.isEmpty() ? QStringLiteral("track-%1-scene-%2").arg(track).arg(scene) : name;
}

//! The MIDI entry's name: the cell's own, else the pattern track's, else the coordinates.
QString cellMidiEntryName(const ClipSlot& slot, int track, int scene)
{
	QString name = slot.name().trimmed();
	if (name.isEmpty())
	{
		if (PatternTrack* patternTrack = PatternTrack::findPatternTrack(slot.patternId())) { name = patternTrack->name().trimmed(); }
	}
	return name.isEmpty() ? QStringLiteral("track-%1-scene-%2").arg(track).arg(scene) : name;
}

//! A populated launcher cell, addressed by 'track' + 'scene', as a save subject.
bool resolveCellSubject(const QJsonObject& args, SaveSubject* subject, ControlResult* error)
{
	if (!args.contains(QStringLiteral("track")) || !args.contains(QStringLiteral("scene")))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a launcher cell is addressed by BOTH 'track' and 'scene'"));
		return false;
	}
	Song* song = Engine::getSong();
	if (song == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this instance has no song: no project is loaded, so there is no cell to save"));
		return false;
	}
	const SessionModel& model = song->sessionModel();
	const int track = args.value(QStringLiteral("track")).toInt();
	const int scene = args.value(QStringLiteral("scene")).toInt();
	if (track < 0 || track >= model.trackCount() || scene < 0 || scene >= model.sceneCount())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("slot (track %1, scene %2) is outside the grid (%3 tracks x %4 scenes); "
				"session.set_grid resizes it").arg(track).arg(scene).arg(model.trackCount()).arg(model.sceneCount()));
		return false;
	}
	const ClipSlot& slot = model.slot(track, scene);
	if (!cellSavable(slot, track, scene, error)) { return false; }
	subject->cell = true;
	if (slot.type() == ClipSlot::Type::Midi)
	{
		PatternStore* store = Engine::patternStore();
		if (store == nullptr || slot.patternId() < 0 || slot.patternId() >= store->numOfPatterns())
		{
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the cell at track %1, scene %2 references pattern %3, which is not in this project")
					.arg(track).arg(scene).arg(slot.patternId()));
			return false;
		}
		subject->pattern = slot.patternId();
		subject->audio = false;
		subject->defaultName = cellMidiEntryName(slot, track, scene);
		return true;
	}
	const QString source = PathUtil::toAbsolute(slot.audioSource());
	if (!QFileInfo(source).isFile())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the cell's audio file '%1' is not there, so there is nothing to save").arg(slot.audioSource()));
		return false;
	}
	subject->source = source;
	subject->audio = true;
	subject->defaultName = cellEntryName(slot, source, track, scene);
	return true;
}

//! Exactly one address: 'clip' for an arrangement clip, or 'track' + 'scene' for a launcher cell.
bool resolveSaveSubject(const QJsonObject& args, SaveSubject* subject, ControlResult* error)
{
	const bool hasClip = args.contains(QStringLiteral("clip"));
	const bool hasCell = args.contains(QStringLiteral("track")) || args.contains(QStringLiteral("scene"));
	if (hasClip == hasCell)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("exactly one of 'clip' (an arrangement clip) or the pair 'track' + 'scene' "
				"(a launcher cell) is required - neither alone, and never both"));
		return false;
	}
	if (!hasClip) { return resolveCellSubject(args, subject, error); }
	ClipRef ref;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, error)) { return false; }
	if (!canSaveClip(ref.clip))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("only sample and MIDI clips can be saved to the library"));
		return false;
	}
	subject->clip = ref.clip;
	subject->clipId = ref.clip->id();
	subject->audio = dynamic_cast<SampleClip*>(ref.clip) != nullptr;
	subject->defaultName = ref.clip->name().trimmed().isEmpty() ? control::clipIdOf(ref.clip) : ref.clip->name();
	return true;
}

//! Writes the subject to @a path: a Clip, a cell's source audio, or a cell's pattern notes.
bool writeSubject(const SaveSubject& subject, const QString& path, QString* why)
{
	if (!subject.cell) { return writeClip(subject.clip, path, why); }
	if (!subject.audio)
	{
		const librarymidi::CellMidi::Result saved = librarymidi::CellMidi::save(Engine::patternStore(),
			subject.pattern, Engine::getSong()->tempoModel().value(), path);
		*why = saved.error;
		return saved.ok;
	}
	const libraryaudio::CellAudio::Result saved = libraryaudio::CellAudio::save(subject.source, path);
	*why = saved.error;
	return saved.ok;
}

//! Writes @a subject to @a path, first reading what it replaces into @a previous: undo puts that back.
bool writeSubjectKeepingPrevious(const SaveSubject& subject, const QString& path, PreviousFile* previous, QString* why)
{
	*previous = readBackIfAny(path);
	if (!previous->readable)
	{
		*why = QStringLiteral("'%1' exists but cannot be read, so it would not be replaced; nothing was saved").arg(path);
		return false;
	}
	return writeSubject(subject, path, why);
}

//! The redo of a save: the clip, the cell's source or the cell's pattern notes written to @a path again.
void redoSubject(const SaveSubject& subject, const QString& path)
{
	QString unused;
	if (!subject.cell)
	{
		ClipRef again;
		ControlResult ignored;
		if (!resolveClip(control::clipId(subject.clipId), &again, &ignored)) { return; }
		writeClip(again.clip, path, &unused);
		return;
	}
	if (!subject.audio)
	{
		if (Engine::getSong() != nullptr)
		{
			librarymidi::CellMidi::save(Engine::patternStore(), subject.pattern,
				Engine::getSong()->tempoModel().value(), path);
		}
		return;
	}
	libraryaudio::CellAudio::save(subject.source, path);
}

ControlResult saveClip(const QJsonObject& args)
{
	const QJsonArray tags = args.value(QStringLiteral("tags")).toArray();
	if (!tagsAreStrings(tags))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'tags' must contain only strings"));
	}
	SaveSubject subject;
	ControlResult error;
	if (!resolveSaveSubject(args, &subject, &error)) { return error; }
	QString path;
	if (!targetPath(args, subject.audio, subject.defaultName, &path, &error)) { return error; }
	if (subject.cell && subject.audio && sameFile(path, subject.source))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the cell's audio is already '%1': saving it there would replace the file it is reading; "
				"choose another 'name' or 'dir'").arg(path));
	}
	// What the write is about to replace, read before it: undo puts that file back.
	PreviousFile previous;
	QString why;
	if (!writeSubjectKeepingPrevious(subject, path, &previous, &why))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the %1 was not saved: %2").arg(subject.cell ? QStringLiteral("cell") : QStringLiteral("clip"), why));
	}
	const QStringList addedTags = addTags(path, tags);
	addUndoStep([path, addedTags, previous]() {
		for (const QString& tag : addedTags) { BrowserTagStore::instance().remove(browserCanonicalKey(path), tag); }
		restoreOrRemove(previous, path);
	}, [subject, path, addedTags]() {
		redoSubject(subject, path);
		for (const QString& tag : addedTags) { BrowserTagStore::instance().add(browserCanonicalKey(path), tag); }
	});
	return ControlResult::success({{QStringLiteral("path"), path}, {QStringLiteral("kind"), browserKindName(browserKindForPath(path))},
		{QStringLiteral("bytes"), QFileInfo(path).size()}, {QStringLiteral("tags"), QJsonArray::fromStringList(addedTags)}});
}

} // namespace

void registerLibraryClipCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("browser.save_clip");
	cmd.group = QStringLiteral("browser");
	cmd.verb = QStringLiteral("save_clip");
	cmd.description = QStringLiteral("Save a clip into the library, addressed either by 'clip' (an arrangement clip) or by the "
		"pair 'track' + 'scene' (a populated launcher cell), exactly one of the two. A sample clip is written as the audio "
		"it plays; a MIDI clip as a MIDI file at the song's tempo; an audio cell as the audio its source file holds, at the "
		"source's own rate (the cell's gain, transpose, loop and warp are playback settings and are not baked into the entry; "
		"the decoded audio is bounded, and a source past the bound is refused before it is decoded); a MIDI cell as a MIDI "
		"file holding every note of the PatternStore pattern it references, at the song's tempo (the cell's pattern-track "
		"settings are playback settings and are not baked into the entry; a pattern with no notes is refused). Written to "
		"'dir' (absolute; <user samples>/Clips, which the Samples browser lists, when left out) as 'name' (the clip's or the "
		"cell's own name when left out), refused when the file exists unless 'overwrite'; 'tags' are added to it. The "
		"library is outside the project: one undo step deletes the file again.");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("track"), integerProperty(0, 255)},
		{QStringLiteral("scene"), integerProperty(0, 511)}, {QStringLiteral("name"), stringProperty()},
		{QStringLiteral("dir"), stringProperty()}, {QStringLiteral("tags"), arrayProperty()}, {QStringLiteral("overwrite"), booleanProperty()}});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = saveClip;
	registry.registerCommand(cmd);
}

} // namespace lmms
