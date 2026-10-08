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
 *  is outside the project, so one undo step deletes the file again (and takes off the tags it added). */

#include <algorithm>
#include <limits>
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
#include "Midi.h"
#include "MidiClip.h"
#include "Note.h"
#include "SampleClip.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int TicksPerQuarter = DefaultTicksPerBar / 4;

void putBigEndian(QByteArray* out, quint32 value, int bytes)
{
	for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) { out->append(static_cast<char>((value >> shift) & 0xFF)); }
}

void putVarLen(QByteArray* out, quint32 value)
{
	quint8 groups[5];
	int count = 0;
	do
	{
		groups[count++] = value & 0x7F;
		value >>= 7;
	} while (value != 0 && count < 5);
	while (count > 0)
	{
		--count;
		out->append(static_cast<char>(groups[count] | (count > 0 ? 0x80 : 0)));
	}
}

struct SmfEvent
{
	tick_t tick = 0;
	int order = 0;  //!< note-offs before note-ons at one tick
	quint8 status = 0;
	quint8 key = 0;
	quint8 velocity = 0;
};

//! Whether every note of @a clip ends on a tick a MIDI file can hold, computed wide: a note-off past the largest
//! tick cannot be written, and one clamped onto its own note-on would sort before it and stick.
bool notesFitInSmf(const MidiClip* clip)
{
	for (const Note* note : clip->notes())
	{
		const tick_t start = std::max<tick_t>(0, note->pos().getTicks());
		const qint64 end = static_cast<qint64>(start) + std::max<tick_t>(1, note->length().getTicks());
		if (end > static_cast<qint64>(std::numeric_limits<tick_t>::max())) { return false; }
	}
	return true;
}

//! @a clip's notes as a type-0 Standard MIDI File at @a bpm.
QByteArray smfOf(const MidiClip* clip, int bpm)
{
	std::vector<SmfEvent> events;
	for (const Note* note : clip->notes())
	{
		const auto key = static_cast<quint8>(std::clamp(note->key(), 0, 127));
		const auto velocity = static_cast<quint8>(std::clamp(note->midiVelocity(MidiDefaultVelocity), 1, 127));
		const tick_t start = std::max<tick_t>(0, note->pos().getTicks());
		// notesFitInSmf has refused any clip whose note-off would not fit, so the end is exact.
		const tick_t end = start + std::max<tick_t>(1, note->length().getTicks());
		events.push_back(SmfEvent{start, 1, 0x90, key, velocity});
		events.push_back(SmfEvent{end, 0, 0x80, key, 0});
	}
	std::stable_sort(events.begin(), events.end(), [](const SmfEvent& a, const SmfEvent& b) {
		return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
	});
	QByteArray track;
	putVarLen(&track, 0);
	track.append("\xFF\x51\x03", 3);
	putBigEndian(&track, static_cast<quint32>(60000000 / std::max(1, bpm)), 3);
	tick_t at = 0;
	for (const SmfEvent& event : events)
	{
		putVarLen(&track, static_cast<quint32>(event.tick - at));
		at = event.tick;
		track.append(static_cast<char>(event.status));
		track.append(static_cast<char>(event.key));
		track.append(static_cast<char>(event.velocity));
	}
	putVarLen(&track, 0);
	track.append("\xFF\x2F\x00", 3);
	QByteArray file("MThd", 4);
	putBigEndian(&file, 6, 4);
	putBigEndian(&file, 0, 2);
	putBigEndian(&file, 1, 2);
	putBigEndian(&file, TicksPerQuarter, 2);
	file.append("MTrk", 4);
	putBigEndian(&file, static_cast<quint32>(track.size()), 4);
	return file + track;
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
	if (!notesFitInSmf(static_cast<MidiClip*>(clip)))
	{
		*error = QStringLiteral("a note ends past the largest tick, which a MIDI file cannot hold; nothing was written");
		return false;
	}
	// Written whole or not at all: a short write must not leave a truncated file (or replace the old one).
	const QByteArray smf = smfOf(static_cast<MidiClip*>(clip), Engine::getSong()->tempoModel().value());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(smf) != smf.size() || !file.commit())
	{
		*error = QStringLiteral("cannot write %1").arg(path);
		return false;
	}
	return true;
}

//! The file @a args asks for: its folder ('dir' or <user samples>/Clips), name and the clip's extension.
bool targetPath(const QJsonObject& args, const ClipRef& ref, QString* path, ControlResult* error)
{
	const QString dir = args.value(QStringLiteral("dir")).toString(ConfigManager::inst()->userSamplesDir() + QStringLiteral("Clips"));
	QString name = args.value(QStringLiteral("name")).toString(ref.clip->name()).trimmed();
	if (name.isEmpty()) { name = clipIdOf(ref.clip); }
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
	const bool audio = dynamic_cast<SampleClip*>(ref.clip) != nullptr;
	*path = QDir(dir).filePath(name + (audio ? QStringLiteral(".wav") : QStringLiteral(".mid")));
	if (QFileInfo::exists(*path) && !args.value(QStringLiteral("overwrite")).toBool(false))
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

//! Writes @a clip to @a path, and first reads what it replaces into @a previous. Refused before any write when that file
//! exists but cannot be read: an undo could not put back what the overwrite destroyed.
bool writeKeepingPrevious(Clip* clip, const QString& path, PreviousFile* previous, QString* why)
{
	*previous = readBackIfAny(path);
	if (!previous->readable)
	{
		*why = QStringLiteral("'%1' exists but cannot be read, so it would not be replaced; nothing was saved").arg(path);
		return false;
	}
	return writeClip(clip, path, why);
}

ControlResult saveClip(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	if (dynamic_cast<SampleClip*>(ref.clip) == nullptr && dynamic_cast<MidiClip*>(ref.clip) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("only sample and MIDI clips can be saved to the library"));
	}
	QString path;
	if (!targetPath(args, ref, &path, &error)) { return error; }
	// What the write is about to replace, read before it: undo puts that file back.
	PreviousFile previous;
	QString why;
	if (!writeKeepingPrevious(ref.clip, path, &previous, &why)) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip was not saved: %1").arg(why)); }
	const QStringList tags = addTags(path, args.value(QStringLiteral("tags")).toArray());
	const int id = ref.clip->id();
	addUndoStep([path, tags, previous]() {
		for (const QString& tag : tags) { BrowserTagStore::instance().remove(browserCanonicalKey(path), tag); }
		restoreOrRemove(previous, path);
	}, [id, path, tags]() {
		ClipRef again;
		ControlResult ignored;
		QString unused;
		if (!resolveClip(clipId(id), &again, &ignored) || !writeClip(again.clip, path, &unused)) { return; }
		for (const QString& tag : tags) { BrowserTagStore::instance().add(browserCanonicalKey(path), tag); }
	});
	return ControlResult::success({{QStringLiteral("path"), path}, {QStringLiteral("kind"), browserKindName(browserKindForPath(path))},
		{QStringLiteral("bytes"), QFileInfo(path).size()}, {QStringLiteral("tags"), QJsonArray::fromStringList(tags)}});
}

} // namespace

void registerLibraryClipCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("browser.save_clip");
	cmd.group = QStringLiteral("browser");
	cmd.verb = QStringLiteral("save_clip");
	cmd.description = QStringLiteral("Save a 'clip' into the library: a sample clip as the audio it plays (a WAV), a MIDI clip as a "
		"MIDI file at the song's tempo. Written to 'dir' (absolute; <user samples>/Clips, which the Samples browser lists, when "
		"left out) as 'name' (the clip's name when left out), refused when the file exists unless 'overwrite'; 'tags' are "
		"added to it. The library is outside the project: one undo step deletes the file again.");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("name"), stringProperty()},
		{QStringLiteral("dir"), stringProperty()}, {QStringLiteral("tags"), arrayProperty()}, {QStringLiteral("overwrite"), booleanProperty()}},
		{QStringLiteral("clip")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = saveClip;
	registry.registerCommand(cmd);
}

} // namespace lmms
