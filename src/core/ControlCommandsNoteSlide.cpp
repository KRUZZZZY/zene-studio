/*
 * ControlCommandsNoteSlide.cpp - the `note.*` SLIDE-note verbs (SPEC A11-A16;
 *                                board task #648, feature-list row 11).
 *
 * THE ENGINE IS PRE-EXISTING and already tested (tests/src/core/SlideNotesTest.cpp);
 * what did not exist was an id:
 *
 *   include/Note.h:128-134    Note::slide() / setSlide(bool) - the FL-style
 *                             portamento flag, serialized as the OPTIONAL "slide"
 *                             attribute "so notes (and whole projects) saved before
 *                             slide notes existed serialize byte-identically"
 *   src/core/Note.cpp:271-276 written only when set
 *   src/core/Note.cpp:322-323 read back as attribute("slide").toInt() - an
 *                             UNCONDITIONAL assignment whose absent-attribute value
 *                             is 0, i.e. "a regular note"
 *   include/NotePlayHandle.h:272-292  what the flag does at play time:
 *                             setSlideSourceKey / hasSlideGlide / slidePitchOffset
 *
 * WHY THE UNCONDITIONAL READ MATTERS (the A16 trap this file states rather than
 * assumes): the attribute is written only when the flag is SET, so a checkpoint
 * taken before a FIRST slide edit carries no "slide" attribute at all. The restore
 * is nonetheless exact because Note::loadSettings assigns the attribute's value
 * with a default of 0 - an absent attribute MEANS "regular note" and the assignment
 * happens either way. That reset-on-absence is what makes the class true_inverse
 * rather than snapshot, and it is the same mechanism note.probability_set relies on
 * for its own optional attribute.
 *
 * UI ABSENCE: the piano roll has no slide-note action and no marker for one - a
 * slide note sounds like a portamento and looks like any other note
 * (docs/KNOWN-LIMITATIONS.md, the row 11 line).
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

#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"
#include "ControlGrooveSupport.h"
#include "ControlNoteShared.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "MidiClip.h"
#include "Note.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The mechanism text both slide verbs record: one fact, one string, so the two
//! rows cannot drift apart.
const QString kSlideCheckpoint = QStringLiteral("ProjectJournal (MidiClip checkpoint: 'slide' is "
	"written only when it is set, but Note::loadSettings assigns attribute(\"slide\").toInt() "
	"UNCONDITIONALLY - an absent attribute means 0, a regular note - so a checkpoint taken before "
	"a FIRST slide edit restores it exactly)");

ControlResult slideSet(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	int index = -1;
	Note* note = resolveNote(clip, args.value(QStringLiteral("note")).toString(), &index, &error);
	if (note == nullptr) { return error; }
	if (!args.value(QStringLiteral("slide")).isBool())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'slide' is required and is true or false: a slide note glides from "
				"the previous note's key to its own over its length, and a regular note does not"));
	}
	const bool slide = args.value(QStringLiteral("slide")).toBool();

	const bool previous = note->slide();
	clip->addJournalCheckPoint();
	note->setSlide(slide);
	clip->dataChanged();

	QJsonObject result = noteState(note, index);
	result.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	result.insert(QStringLiteral("note"), noteId(index));
	result.insert(QStringLiteral("slide"), note->slide());

	QJsonObject before;
	before.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	before.insert(QStringLiteral("note"), noteId(index));
	before.insert(QStringLiteral("slide"), previous);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	inverseArgs.insert(QStringLiteral("note"), noteId(index));
	inverseArgs.insert(QStringLiteral("slide"), previous);
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, QStringLiteral("note.slide_set"), inverseArgs, true,
			kSlideCheckpoint + QStringLiteral("; the recorded inverse op is this same command "
				"with the before-value, which a reader can re-issue by hand")));
	return ControlResult::success(result);
}

/*! note.slide_clear - every slide flag in scope, off, in ONE undo step.
 *
 *  A clip-level verb rather than a loop of note.slide_set calls: N calls would be N
 *  undo steps for one edit, which is the defect SPEC A16 deliverable 3 exists to
 *  prevent (the reason transport.tempo_map_clear is one command too).
 */
ControlResult slideClear(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	NoteScope scope = NoteScope::Clip;
	if (!readScope(args, &scope, &error)) { return error; }

	const NoteVector notes = scopeNotes(*clip, scope, clipId(ref.ordinal));
	int slidesBefore = 0;
	for (const Note* note : clip->notes())
	{
		if (note != nullptr && note->slide()) { ++slidesBefore; }
	}

	clip->addJournalCheckPoint();
	int cleared = 0;
	for (Note* note : notes)
	{
		if (note == nullptr || !note->slide()) { continue; }
		note->setSlide(false);
		++cleared;
	}
	if (cleared > 0) { clip->dataChanged(); }

	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	result.insert(QStringLiteral("track"), trackIdOf(ref.track));
	result.insert(QStringLiteral("scope"), scopeName(scope));
	result.insert(QStringLiteral("slides_before"), slidesBefore);
	result.insert(QStringLiteral("slides_cleared"), cleared);
	// Clearing a selection leaves the rest of the clip's slide notes alone, and
	// this number is how a caller sees that it did.
	result.insert(QStringLiteral("slides_remaining"), slidesBefore - cleared);
	result.insert(QStringLiteral("note_count"), static_cast<int>(clip->notes().size()));

	QJsonObject before;
	before.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	before.insert(QStringLiteral("track"), trackIdOf(ref.track));
	before.insert(QStringLiteral("slides_before"), slidesBefore);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, QStringLiteral("control.undo"), inverseArgs, true,
			kSlideCheckpoint + QStringLiteral("; every cleared flag is part of the note list the "
				"checkpoint carries, so ONE control.undo restores the whole scope without a "
				"per-note call and without re-toggling anything")));
	return ControlResult::success(result);
}

} // namespace

void registerNoteSlideCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("note.slide_set");
		cmd.group = QStringLiteral("note");
		cmd.verb = QStringLiteral("slide_set");
		cmd.description = QStringLiteral("Mark one note as a SLIDE note (FL-style portamento: its "
			"pitch glides from the previous note's key to its own over its length) or clear the "
			"flag. The flag is serialized per note as the optional \"slide\" attribute, so it "
			"survives save/load and a project that has none stays byte-identical to how it "
			"serialized before slide notes existed. What the flag does at play time is the "
			"engine's (NotePlayHandle::hasSlideGlide / slidePitchOffset); this verb only edits the "
			"note. Reversible through the ProjectJournal (MidiClip checkpoint), INCLUDING a first "
			"edit back to a regular note.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("note"), stringProperty()},
			{QStringLiteral("slide"), booleanProperty()},
		}, {QStringLiteral("clip"), QStringLiteral("note"), QStringLiteral("slide")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("note"), stringProperty()},
			{QStringLiteral("slide"), booleanProperty()},
			{QStringLiteral("id"), stringProperty()},
			{QStringLiteral("index"), integerProperty()},
			{QStringLiteral("key"), integerProperty(0, 127)},
			{QStringLiteral("position"), integerProperty(0, kMaxSchemaInteger)},
			{QStringLiteral("length"), integerProperty(0, kMaxSchemaInteger)},
			{QStringLiteral("velocity"), numberProperty()},
			{QStringLiteral("midi_velocity"), integerProperty(0, 127)},
			{QStringLiteral("pan"), numberProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return slideSet(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("note.slide_clear");
		cmd.group = QStringLiteral("note");
		cmd.verb = QStringLiteral("slide_clear");
		cmd.description = QStringLiteral("Clear every slide flag in a clip (or in the current "
			"selection) as ONE edit and ONE undo step, and report how many were cleared and how "
			"many slide notes remain OUTSIDE the scope. Clearing is not deleting: the notes stay "
			"exactly where they are and become regular notes. Reversible through the ProjectJournal "
			"(MidiClip checkpoint).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("scope"), enumProperty({QStringLiteral("clip"),
				QStringLiteral("selection")})},
		}, {QStringLiteral("clip")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("track"), stringProperty()},
			{QStringLiteral("scope"), stringProperty()},
			{QStringLiteral("slides_before"), integerProperty()},
			{QStringLiteral("slides_cleared"), integerProperty()},
			{QStringLiteral("slides_remaining"), integerProperty()},
			{QStringLiteral("note_count"), integerProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return slideClear(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
