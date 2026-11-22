/*
 * ControlCommandsSessionPatterns.cpp - session.pattern_list: the patterns a launcher cell can play
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

/*! WF-054 (0.5.0): a MIDI launcher cell plays a PATTERN - the pattern editor's numbered patterns,
 *  `session.set_slot {type: "midi", pattern}` - and a person choosing one needs their names, not
 *  their numbers. Read-only.
 *
 *  Q050-273.2: session.import_midi_cell brings a Standard MIDI File into a launcher cell as a NEW
 *  pattern. The notes go into the chosen PatternStore instrument's clip of that pattern; the cell
 *  is pointed at it; one undo step restores the cell's previous reference and removes the pattern
 *  track. This is the other half of the library round trip: browser.save_clip writes a MIDI cell's
 *  pattern as an SMF, and this command puts an SMF back into a cell. */

#include <algorithm>
#include <memory>
#include <vector>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlCommandsSessionShared.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SmfNoteRead.h"
#include "Song.h"
#include "volume.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;

namespace
{

//! What one import made and what it needs to make it again.
struct Imported
{
	int track = 0;
	int scene = 0;
	int instrument = 0;
	QString name;
	QString session;  //!< the <session> block before
	std::vector<interchange::SmfNote> notes;
	long long endTick = 0;
	int pattern = -1;  //!< the pattern track's stable id, found again when a step runs
	int written = 0;
};

//! The volume whose Note::midiVelocity(MidiDefaultVelocity) is @a velocity. velocity is a 7-bit value
//! and the note's volume is 0..200, so several velocities share no volume: this is the smallest volume
//! that emits exactly @a velocity, which keeps a saved cell's velocities stable across repeated round trips.
volume_t volumeForVelocity(int velocity)
{
	const int v = std::clamp(velocity, 1, 127);
	return static_cast<volume_t>(std::clamp((v * 100 + 62) / 63, static_cast<int>(MinVolume), static_cast<int>(MaxVolume)));
}

//! Make the pattern, write the imported notes into the store instrument's clip of it, point the cell at it.
bool build(Imported* c)
{
	Song* song = Engine::getSong();
	PatternStore* store = Engine::patternStore();
	if (song == nullptr || store == nullptr) { return false; }
	if (c->instrument < 0 || c->instrument >= static_cast<int>(store->tracks().size())) { return false; }
	auto* patternTrack = dynamic_cast<PatternTrack*>(Track::create(Track::Type::Pattern, song));
	if (patternTrack == nullptr) { return false; }
	const int pattern = patternTrack->patternIndex();
	store->createClipsForPattern(pattern);
	auto* clip = dynamic_cast<MidiClip*>(store->tracks()[static_cast<std::size_t>(c->instrument)]
		->getClip(static_cast<std::size_t>(pattern)));
	if (clip == nullptr)
	{
		// The store's track has no note clip (not an instrument): nothing could be imported, so no pattern either.
		removeTrack(patternTrack);
		return false;
	}
	for (const interchange::SmfNote& note : c->notes)
	{
		clip->addNote(Note(TimePos(static_cast<tick_t>(note.length)), TimePos(static_cast<tick_t>(note.start)),
			note.key, volumeForVelocity(note.velocity)), false);
	}
	clip->changeLength(TimePos(static_cast<tick_t>(std::max<long long>(1, c->endTick))));
	clip->setAutoResize(false);
	clip->dataChanged();
	const SessionModel before = song->sessionModel();
	ClipSlot& slot = song->sessionModel().slot(c->track, c->scene);
	slot.setPatternReference(pattern);
	slot.setName(c->name);
	patternTrack->setName(c->name);
	c->pattern = patternTrack->id();
	c->written = static_cast<int>(c->notes.size());
	syncLauncherFrom(before);
	return true;
}

void unbuild(Imported* c)
{
	if (Song* song = Engine::getSong()) { restoreSessionBlockAndSync(*song, c->session); }
	if (Track* track = trackWithId(c->pattern)) { removeTrack(track); }
	c->pattern = -1;
}

ControlResult invalid(const QString& message) { return ControlResult::failure(ControlErrorKind::InvalidArgs, message); }

ControlResult importMidiCell(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	auto c = std::make_shared<Imported>();
	c->track = args.value(QStringLiteral("track")).toInt();
	c->scene = args.value(QStringLiteral("scene")).toInt();
	c->instrument = args.value(QStringLiteral("instrument")).toInt(0);
	if (!gridContains(*model, c->track, c->scene, &error)) { return error; }
	PatternStore* store = Engine::patternStore();
	if (store == nullptr || c->instrument < 0 || c->instrument >= static_cast<int>(store->tracks().size())
		|| store->tracks()[static_cast<std::size_t>(c->instrument)]->type() != Track::Type::Instrument)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the pattern store has no instrument %1").arg(c->instrument));
	}
	const QString path = args.value(QStringLiteral("path")).toString();
	if (!QFileInfo(path).isAbsolute()) { return invalid(QStringLiteral("'path' must be an absolute path")); }
	const QFileInfo info(path);
	if (!info.exists())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("there is no file at '%1'").arg(path));
	}
	if (!info.isFile()) { return invalid(QStringLiteral("'%1' is not a file").arg(path)); }
	if (!info.isReadable() || info.size() <= 0)
	{
		return invalid(QStringLiteral("'%1' is empty or cannot be read").arg(path));
	}
	if (info.size() > interchange::SmfNoteMaxBytes)
	{
		return invalid(QStringLiteral("'%1' is larger than the MIDI files read here (16 MiB)").arg(path));
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		return invalid(QStringLiteral("'%1' cannot be opened: %2").arg(path, file.errorString()));
	}
	interchange::SmfNoteRead read = interchange::readSmfNotes(file.readAll());
	if (!read.ok)
	{
		return invalid(QStringLiteral("'%1' was not imported: %2").arg(QFileInfo(path).fileName(), read.error));
	}
	if (read.endTick > static_cast<long long>(MaxSongLength))
	{
		return invalid(QStringLiteral("'%1' runs to tick %2, past the song's limit of %3")
			.arg(QFileInfo(path).fileName()).arg(read.endTick).arg(MaxSongLength));
	}
	c->notes = read.notes;
	c->endTick = read.endTick;
	c->name = args.value(QStringLiteral("name")).toString().trimmed();
	if (c->name.isEmpty()) { c->name = QFileInfo(path).completeBaseName().trimmed(); }
	if (c->name.isEmpty()) { c->name = QStringLiteral("imported MIDI"); }
	c->session = sessionBlockXml(*model);
	if (!build(c.get()))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the pattern could not be made (is pattern-store "
			"track %1 an instrument?)").arg(c->instrument));
	}
	addUndoStep([c]() { unbuild(c.get()); }, [c]() { build(c.get()); });
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("slot"), clipSlotState(c->track, c->scene, model->slot(c->track, c->scene))},
		{QStringLiteral("pattern"), model->slot(c->track, c->scene).patternId()}, {QStringLiteral("notes"), c->written},
		{QStringLiteral("length"), static_cast<double>(c->endTick)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("track"), c->track},
		{QStringLiteral("scene"), c->scene}}, QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the imported undo step restores the <session> block and removes the pattern track "
			"it made; its redo imports the same notes again")));
	return ControlResult::success(result);
}

} // namespace

void registerSessionPatternCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.pattern_list");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("pattern_list");
	cmd.description = QStringLiteral("The patterns a MIDI launcher cell can play: each pattern's number - "
		"session.set_slot's 'pattern' - and the name of its pattern track. Read-only.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("patterns"), arrayProperty()}, {QStringLiteral("count"), integerProperty()}});
	cmd.handler = [](const QJsonObject&) {
		QJsonArray patterns;
		PatternStore* store = Engine::patternStore();
		const int count = store != nullptr ? store->numOfPatterns() : 0;
		for (int pattern = 0; pattern < count; ++pattern)
		{
			const PatternTrack* track = PatternTrack::findPatternTrack(pattern);
			patterns.append(QJsonObject{{QStringLiteral("pattern"), pattern},
				{QStringLiteral("name"), track != nullptr ? track->name() : QString()}});
		}
		return ControlResult::success({{QStringLiteral("patterns"), patterns}, {QStringLiteral("count"), patterns.size()}});
	};
	registry.registerCommand(cmd);

	ControlCommand import;
	import.id = QStringLiteral("session.import_midi_cell");
	import.group = QStringLiteral("session");
	import.verb = QStringLiteral("import_midi_cell");
	import.description = QStringLiteral("Import a Standard MIDI File into a launcher cell ('track', 'scene') as a new pattern: "
		"the file at 'path' (absolute) becomes a new pattern, its notes written into the clip of pattern-store instrument "
		"'instrument' (0, the first, when left out), and the cell plays that pattern. The cell and its pattern track are named "
		"'name' (the file's name when left out). One undo step restores the cell's previous content and removes the pattern.");
	import.argsSchema = objectSchema({{QStringLiteral("track"), integerProperty(0, 255)}, {QStringLiteral("scene"), integerProperty(0, 511)},
		{QStringLiteral("path"), stringProperty()}, {QStringLiteral("instrument"), integerProperty(0, 255)},
		{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("track"), QStringLiteral("scene"), QStringLiteral("path")});
	import.resultSchema = objectProperty();
	import.mutating = true;
	import.handler = importMidiCell;
	registry.registerCommand(import);
}

} // namespace lmms
