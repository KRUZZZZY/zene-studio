/*
 * ControlCommandsScaleEdit.cpp - the `scale.*` group's one EDITING verb:
 *                                scale.snap_notes (SPEC A11-A16; board task #648,
 *                                feature-list row 66).
 *
 * Split out of ControlCommandsScale.cpp because Gate 7 measures a FILE and the
 * group is one group - the read/edit split the automation, warp, rack, vca,
 * chain-preset and mastering groups all use. Same group, same
 * registerScaleCommands() family, one id.
 *
 * THE ENGINE, unchanged and already tested: NoteTransform::snapToScale
 * (include/NoteTransform.h:111-115) moves every note whose pitch class is outside
 * the scale to the NEAREST in-scale pitch, a tie resolving downward (i.e. to the
 * lower key), clamped to the MIDI range, and leaves the notes that are already in
 * the scale alone. NoteTransform::pitchClasses() is what turns the scale's degrees
 * into the membership set, so a degree outside 0..11 cannot widen the scale.
 *
 * WHY THE CHECKPOINT IS THE INVERSE (and "snap again" is not): a snap moves exactly
 * the notes that were out of scale, and after it they are IN scale, so re-running it
 * moves nothing and could not bring an old key back. The clip's own journal
 * checkpoint is what restores the keys - the mechanism note.transpose and groove.apply
 * reverse with.
 *
 * A16: true_inverse through the live MidiClip checkpoint.
 *
 * UI ABSENCE: no action, menu entry or shortcut snaps notes to a scale; the piano
 * roll's scale selector only marks semitones (docs/KNOWN-LIMITATIONS.md, row 66).
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
#include "ControlScaleShared.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteTransform.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

ControlResult snapNotes(const QJsonObject& args)
{
	ControlResult error;
	ResolvedScale resolved;
	if (!resolveScale(args, &resolved, &error)) { return error; }
	if (resolved.scale.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("no scale is set: name one on this call ('root' and/or 'scale') or set "
				"the context first with scale.set - this verb will not guess a scale to snap to"));
	}
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	NoteScope scope = NoteScope::Clip;
	if (!readScope(args, &scope, &error)) { return error; }
	const NoteVector notes = scopeNotes(*clip, scope, clipId(ref.id));
	if (notes.empty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has %2 notes and none is selected, so scope 'selection' would edit "
				"nothing").arg(clipId(ref.id)).arg(clip->notes().size()));
	}

	clip->addJournalCheckPoint();
	const int changed = NoteTransform::snapToScale(notes, resolved.classes);
	// Keys moved, and the clip's own order is (position, then key), so the list has
	// to be put back - the call note.move makes after it moves one note.
	clip->rearrangeAllNotes();
	clip->dataChanged();

	// The measurement, not the claim: how many of the clip's notes are in the scale
	// NOW, counted with the engine's own predicate.
	const int inScale = countNotesInScale(*clip, resolved.classes);
	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(ref.id));
	result.insert(QStringLiteral("track"), trackIdOf(ref.track));
	result.insert(QStringLiteral("scope"), scopeName(scope));
	result.insert(QStringLiteral("root"), resolved.root);
	result.insert(QStringLiteral("root_name"), scaleRootNames().at(resolved.root));
	result.insert(QStringLiteral("scale"), resolved.scale);
	result.insert(QStringLiteral("pitch_classes"), scaleClassesJson(resolved.classes));
	result.insert(QStringLiteral("mask"), scaleMask(resolved.classes));
	result.insert(QStringLiteral("note_count"), static_cast<int>(clip->notes().size()));
	result.insert(QStringLiteral("notes_considered"), static_cast<int>(notes.size()));
	result.insert(QStringLiteral("notes_moved"), changed);
	result.insert(QStringLiteral("notes_in_scale_after"), inScale);
	result.insert(QStringLiteral("notes_out_of_scale_after"),
		static_cast<int>(clip->notes().size()) - inScale);

	QJsonObject before;
	before.insert(QStringLiteral("clip"), clipId(ref.id));
	before.insert(QStringLiteral("track"), trackIdOf(ref.track));
	before.insert(QStringLiteral("note_count"), static_cast<int>(clip->notes().size()));
	before.insert(QStringLiteral("root"), resolved.root);
	before.insert(QStringLiteral("scale"), resolved.scale);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("clip"), clipId(ref.id));
	inverseArgs.insert(QStringLiteral("scope"), scopeName(scope));
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, QStringLiteral("control.undo"), inverseArgs, true,
			grooveClipMechanism() + QStringLiteral("; and the snap itself is NOT an inverse: a "
				"note it moved is now IN the scale, so re-running it would move nothing and "
				"would not bring the old key back")));
	return ControlResult::success(result);
}

} // namespace

void registerScaleEditCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("scale.snap_notes");
	cmd.group = QStringLiteral("scale");
	cmd.verb = QStringLiteral("snap_notes");
	cmd.description = QStringLiteral("Move every note of a clip (or of the current selection) "
		"whose pitch class is OUTSIDE the scale to the nearest in-scale pitch, clamped to the MIDI "
		"range, with a tie resolving downward; notes already in the scale are left alone "
		"(NoteTransform::snapToScale, the engine's own rule). The scale is the CONTEXT's unless "
		"this call names 'root' and/or 'scale', and a call with no scale anywhere is REFUSED "
		"rather than snapped to a guessed key. The report says how many notes moved and how many "
		"are still out of scale afterwards, so \"it is in key now\" is a measurement. Reversible "
		"through the ProjectJournal (MidiClip checkpoint) - and NOT by re-running the snap, which "
		"is why the checkpoint is the inverse. 'scope' is 'clip' (default) or 'selection'.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("scope"), enumProperty({QStringLiteral("clip"),
			QStringLiteral("selection")})},
		{QStringLiteral("root"), rootArgumentSchema()},
		{QStringLiteral("scale"), stringProperty()},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("scope"), stringProperty()},
		{QStringLiteral("root"), integerProperty(0, 11)},
		{QStringLiteral("root_name"), stringProperty()},
		{QStringLiteral("scale"), stringProperty()},
		{QStringLiteral("pitch_classes"), arrayProperty()},
		{QStringLiteral("mask"), stringProperty()},
		{QStringLiteral("note_count"), integerProperty()},
		{QStringLiteral("notes_considered"), integerProperty()},
		{QStringLiteral("notes_moved"), integerProperty()},
		{QStringLiteral("notes_in_scale_after"), integerProperty()},
		{QStringLiteral("notes_out_of_scale_after"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return snapNotes(args); };
	registry.registerCommand(cmd);
}

} // namespace lmms
