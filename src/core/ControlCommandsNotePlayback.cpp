/*
 * ControlCommandsNotePlayback.cpp - lyrics, note groups, articulations, global swing and the MIDI playback options
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
/*! 0.5.0 L07 (W3), the commands over how notes are played and what they carry:
 *  - note.lyrics (ED-088): syllables onto the notes in 'scope', in time order ('text', split at
 *    spaces and hyphens), or one text per note ('lyrics') - one undo step;
 *  - note.group / note.ungroup (ED-069): the notes in 'scope' made one group (they are then selected
 *    and moved together), or released - one undo step;
 *  - track.articulations / articulations_set (ED-121): an instrument track's expression map - what
 *    each articulation sends before a note that asks for it (note.edit 'articulation');
 *  - transport.groove (ED-142): the global swing every note on an off-beat is played with;
 *  - transport.midi_options (ED-075, ED-076): chase notes playback starts inside, and the MIDI
 *    playback offset. */

#include <QJsonArray>
#include <QRegularExpression>

#include <cmath>

#include "ArticulationMap.h"
#include "ControlCommandsNoteShared.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlReversibility.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "InstrumentTrack.h"
#include "MidiPlayback.h"
#include "NoteConditions.h"

namespace lmms
{

using namespace control;
using namespace control::notes;

namespace
{

//! The clip and the notes 'scope' names ('selection' when @a selectionFirst and it is left out).
MidiClip* clipAndNotes(const QJsonObject& args, bool selectionFirst, ClipRef* ref, QVector<Note*>* notes, ControlResult* error)
{
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), ref, error);
	if (clip == nullptr) { return nullptr; }
	*notes = scoped(clip, args.value(QStringLiteral("scope")).toString(selectionFirst ? QStringLiteral("selection") : QStringLiteral("clip")));
	if (notes->isEmpty()) { *error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no notes in that scope")); return nullptr; }
	return clip;
}

void setLyric(Note* note, const QString& text)
{
	NoteConditions conditions = note->conditions();
	conditions.lyric = internLyric(text.trimmed());
	note->setConditions(conditions);
}

ControlResult lyricsByNote(MidiClip* clip, const ClipRef& ref, const QJsonArray& lyrics)
{
	std::vector<std::pair<Note*, QString>> writes;
	for (const QJsonValue& value : lyrics)
	{
		if (!value.isObject())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("each lyric entry must be an object with string 'note' and 'text' fields"));
		}
		const QJsonObject entry = value.toObject();
		const QJsonValue noteId = entry.value(QStringLiteral("note"));
		const QJsonValue text = entry.value(QStringLiteral("text"));
		if (!noteId.isString() || !text.isString())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("each lyric entry needs string 'note' and 'text' fields"));
		}
		ControlResult error;
		int index = -1;
		Note* note = resolveNote(clip, noteId.toString(), &index, &error);
		if (note == nullptr) { return error; }
		writes.emplace_back(note, text.toString());
	}
	if (writes.empty()) { return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("set"), 0}}); }
	clip->addJournalCheckPoint();
	for (const auto& [note, text] : writes) { setLyric(note, text); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.lyrics"), {{QStringLiteral("set"), static_cast<int>(writes.size())}}));
}

ControlResult lyrics(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (args.contains(QStringLiteral("lyrics")))
	{
		MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
		return clip == nullptr ? error : lyricsByNote(clip, ref, args.value(QStringLiteral("lyrics")).toArray());
	}
	if (!args.contains(QStringLiteral("text")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("give the lyrics: 'text', or 'lyrics' per note"));
	}
	QVector<Note*> notes;
	MidiClip* clip = clipAndNotes(args, false, &ref, &notes, &error);
	if (clip == nullptr) { return error; }
	// "Hal-le-lu-jah" sings over four notes; a syllable keeps its hyphen so the editor can draw the join.
	QStringList syllables;
	for (const QString& word : args.value(QStringLiteral("text")).toString().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
	{
		const QStringList parts = word.split(QLatin1Char('-'), Qt::SkipEmptyParts);
		for (int i = 0; i < parts.size(); ++i) { syllables << parts[i] + (i + 1 < parts.size() ? QStringLiteral("-") : QString()); }
	}
	clip->addJournalCheckPoint();
	for (int i = 0; i < notes.size(); ++i) { setLyric(notes[i], syllables.value(i)); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.lyrics"),
		{{QStringLiteral("set"), static_cast<int>(std::min(notes.size(), syllables.size()))}, {QStringLiteral("syllables"), syllables.size()}}));
}

ControlResult group(const QJsonObject& args, bool join)
{
	ClipRef ref;
	QVector<Note*> notes;
	ControlResult error;
	MidiClip* clip = clipAndNotes(args, true, &ref, &notes, &error);
	if (clip == nullptr) { return error; }
	if (join && notes.size() < 2) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a group needs two notes or more")); }
	int id = 0;
	for (const Note* note : clip->notes()) { id = std::max(id, note->conditions().group); }
	clip->addJournalCheckPoint();
	for (Note* note : notes)
	{
		NoteConditions conditions = note->conditions();
		conditions.group = join ? id + 1 : 0;
		note->setConditions(conditions);
	}
	return ControlResult::success(finish(ref, clip, join ? QStringLiteral("note.group") : QStringLiteral("note.ungroup"),
		{{QStringLiteral("notes"), notes.size()}, {QStringLiteral("group"), join ? id + 1 : 0}}));
}

InstrumentTrack* instrumentTrack(const QString& id, ControlResult* error)
{
	auto* track = dynamic_cast<InstrumentTrack*>(resolveTrack(id, error));
	if (track == nullptr && error->errorMessage.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'track' is an instrument track"));
	}
	return track;
}

//! Replace the map of the track @a id names (under its lock, as the audio thread reads it).
void writeArticulations(const QString& id, const std::vector<Articulation>& entries)
{
	ControlResult ignored;
	InstrumentTrack* track = instrumentTrack(id, &ignored);
	if (track == nullptr) { return; }
	track->lock();
	track->articulations().set(entries);
	track->unlock();
	Engine::getSong()->setModified();
}

ControlResult articulationsSet(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = instrumentTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	std::vector<Articulation> entries;
	QString why;
	if (!ArticulationMap::fromJson(args.value(QStringLiteral("articulations")).toArray(), &entries, &why)
		|| static_cast<int>(entries.size()) > NoteConditions::MaxArticulations)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, why.isEmpty() ? QStringLiteral("at most 64 articulations") : why);
	}
	const QString id = trackIdOf(track);
	const std::vector<Articulation> before = track->articulations().entries();
	writeArticulations(id, entries);
	addUndoStep([id, before]() { writeArticulations(id, before); }, [id, entries]() { writeArticulations(id, entries); });
	return ControlResult::success({{QStringLiteral("track"), id}, {QStringLiteral("articulations"), track->articulations().toJson()}});
}

ControlResult articulations(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = instrumentTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("articulations"), track->articulations().toJson()}});
}

QJsonObject grooveState()
{
	const MidiPlayback& playback = MidiPlayback::instance();
	return {{QStringLiteral("swing"), static_cast<double>(playback.swing())},
		{QStringLiteral("grid"), playback.swingGrid() == MidiPlayback::DefaultSwingGrid ? QStringLiteral("16th") : QStringLiteral("8th")}};
}

ControlResult groove(const QJsonObject& args)
{
	MidiPlayback& playback = MidiPlayback::instance();
	const float before = playback.swing();
	const int beforeGrid = playback.swingGrid();
	const double swingValue = args.value(QStringLiteral("swing")).toDouble(before);
	if (!std::isfinite(swingValue) || swingValue < 0.0 || swingValue > MidiPlayback::MaxSwing)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'swing' is 0..0.75"));
	}
	const float swing = static_cast<float>(swingValue);
	const QString gridName = args.value(QStringLiteral("grid")).toString();
	const int grid = gridName.isEmpty() ? beforeGrid : (gridName == QLatin1String("8th") ? 2 * MidiPlayback::DefaultSwingGrid : MidiPlayback::DefaultSwingGrid);
	if (swing == before && grid == beforeGrid) { return ControlResult::success(grooveState()); }
	playback.setSwing(swing, grid);
	addUndoStep([before, beforeGrid]() { MidiPlayback::instance().setSwing(before, beforeGrid); },
		[swing, grid]() { MidiPlayback::instance().setSwing(swing, grid); });
	Engine::getSong()->setModified();
	return ControlResult::success(grooveState());
}

ControlResult midiOptions(const QJsonObject& args)
{
	MidiPlayback& playback = MidiPlayback::instance();
	const bool chase = args.value(QStringLiteral("chase")).toBool(playback.chase());
	const int offset = args.value(QStringLiteral("offset_ms")).toInt(playback.offsetMs());
	const bool beforeChase = playback.chase();
	const int beforeOffset = playback.offsetMs();
	if (chase != beforeChase || offset != beforeOffset)
	{
		playback.setPreferences(chase, offset);
		addUndoStep([beforeChase, beforeOffset]() { MidiPlayback::instance().setPreferences(beforeChase, beforeOffset); },
			[chase, offset]() { MidiPlayback::instance().setPreferences(chase, offset); });
	}
	return ControlResult::success({{QStringLiteral("chase"), playback.chase()}, {QStringLiteral("offset_ms"), playback.offsetMs()}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, std::function<ControlResult(const QJsonObject&)> handler)
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerNotePlaybackCommands(ControlRegistry& registry)
{
	MidiPlayback::instance();  // registers its project section before a project loads
	const QJsonObject scope = enumProperty({QStringLiteral("clip"), QStringLiteral("selection")});
	add(registry, "note.lyrics", QStringLiteral("Put lyrics on notes: 'text' is split into syllables at spaces and hyphens and laid "
		"on the notes in 'scope' (the whole clip when left out) in time order - notes past the last syllable lose theirs - or "
		"'lyrics' gives each note its own: a list of {note, text}. The note editor draws them on the notes. One undo step."),
		{{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("text"), stringProperty()}, {QStringLiteral("lyrics"), arrayProperty()},
			{QStringLiteral("scope"), scope}}, {QStringLiteral("clip")}, true, lyrics);
	add(registry, "note.group", QStringLiteral("Make the notes in 'scope' (the selection when left out) one group: selecting one "
		"selects them all, so they move together. One undo step."),
		{{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("scope"), scope}}, {QStringLiteral("clip")}, true,
		[](const QJsonObject& args) { return group(args, true); });
	add(registry, "note.ungroup", QStringLiteral("Release the notes in 'scope' (the selection when left out) from their groups. "
		"One undo step."), {{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("scope"), scope}}, {QStringLiteral("clip")}, true,
		[](const QJsonObject& args) { return group(args, false); });
	add(registry, "track.articulations_set", QStringLiteral("Set an instrument track's articulations (its expression map): a list "
		"of {name, keyswitch?, program?, controller?, value?} - what is sent, just before a note that asks for the articulation "
		"(note.edit 'articulation', 1-based), to switch the instrument: a keyswitch key, a program change, a control change. An "
		"empty list clears the map. One undo step."),
		{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("articulations"), arrayProperty()}},
		{QStringLiteral("track"), QStringLiteral("articulations")}, true, articulationsSet);
	add(registry, "track.articulations", QStringLiteral("An instrument track's articulations, in order (a note's 'articulation' is "
		"the 1-based index into this list)."), {{QStringLiteral("track"), stringProperty()}}, {QStringLiteral("track")}, false, articulations);
	add(registry, "transport.groove", QStringLiteral("The global swing: every note on an off-beat of 'grid' (16th or 8th) is played "
		"later by 'swing' (0..0.75) of a grid step - the notes themselves do not move. Saved with the project. With no "
		"arguments it reports the swing. One undo step."),
		{{QStringLiteral("swing"), numberProperty()}, {QStringLiteral("grid"), enumProperty({QStringLiteral("16th"), QStringLiteral("8th")})}},
		{}, true, groove);
	add(registry, "transport.midi_options", QStringLiteral("How notes are played back: 'chase' sounds a note playback starts, "
		"jumps or loops into the middle of (off by default), and 'offset_ms' (0..500) starts every note that much later, to line "
		"up a slow instrument. Preferences, kept in the config file. With no arguments it reports them. One undo step."),
		{{QStringLiteral("chase"), booleanProperty()}, {QStringLiteral("offset_ms"), integerProperty(0, MidiPlayback::MaxOffsetMs)}},
		{}, true, midiOptions);
}

} // namespace lmms
