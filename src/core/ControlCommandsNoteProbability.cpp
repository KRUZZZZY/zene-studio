/*
 * ControlCommandsNoteProbability.cpp - `note.probability_set`, the MIDI-depth
 *                                       probability verb (SPEC A11-A16).
 *
 * The engine landed with the MIDI-depth wave (docs/MIDI-DEPTH.md) and never got an
 * id: `note.*` carried nine ids and none of them touched the field. This file is the
 * registration only - it drives the engine exactly as it already stands:
 *
 *   include/Note.h:140-141     float Note::probability() const; void setProbability(float);
 *   src/core/Note.cpp:158-161  m_probability = std::clamp(probability, 0.f, 1.f);
 *   src/core/Note.cpp:282-285  written as the OPTIONAL `prob` attribute, ONLY when != 1.f
 *   src/core/Note.cpp:325-329  read back as attribute("prob", "1") - a reset on absence
 *   src/tracks/InstrumentTrack.cpp:906-911  NoteRandom::passesProbability() consumes it
 *
 * THE PERSISTENCE IS PER NOTE, and the note is a SerializingObject, not a
 * JournallingObject - so the checkpoint has to be the OWNING MidiClip's, exactly as
 * `note.velocity_set` does it. `MidiClip::loadSettings` clears the note list and
 * re-creates every note (src/tracks/MidiClip.cpp:497-507), which is the mechanism the
 * piano roll's own note edits reverse with.
 *
 * THE TRAP, and why this verb is safe from it. A Clip checkpoint captures the clip's
 * XML BEFORE the write, and `prob` is written only when the value is not the default
 * 1.0 - so a checkpoint taken before a FIRST probability edit carries no `prob`
 * attribute at all. The restore is nonetheless exact, because
 * `Note::loadSettings` reads `attribute("prob", "1")`: an absent attribute MEANS 1.0
 * and the assignment is unconditional. That reset-on-absence is what makes the class
 * `true_inverse` rather than `snapshot`; the negative control in
 * `tests/control-verb-inverses.py` neuters it (guarding the read behind
 * `hasAttribute`) and shows the undo fail.
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

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString MechanismClipCheckpoint = QStringLiteral("ProjectJournal (MidiClip checkpoint: "
	"MidiClip::loadSettings clears and re-loads the clip's note list, and Note::loadSettings "
	"reads the optional 'prob' attribute with a default of 1 - so the checkpoint taken before "
	"the write restores the previous probability exactly, including a first edit back to the "
	"default)");

//! The MIDI-depth probability range, in one place: the engine clamps to it
//! (Note::setProbability) and the same closed range is refused with InvalidArgs here.
//! The refusal is SPEC A11's rule - a typed refusal, never a silent clamp - so a caller
//! that asks for 1.5 is told so rather than handed 1.0 and told it succeeded.
constexpr double kMinProbability = 0.0;
constexpr double kMaxProbability = 1.0;

//! Refuses a probability outside [0, 1] before anything is written.
bool checkProbability(double probability, ControlResult* error)
{
	if (probability < kMinProbability || probability > kMaxProbability)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'probability' %1 is outside [0, 1]: 1 is \"always plays\" and 0 is "
				"\"never plays\", and the engine clamps anything else rather than guessing")
				.arg(probability));
		return false;
	}
	return true;
}

void registerNoteProbabilitySet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.probability_set");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("probability_set");
	cmd.description = QStringLiteral("Set the chance, in [0, 1], that a note is played at all in a "
		"take (1 is the default and means \"always\"; 0 means never). The value is per note, so one "
		"clip can hold some 100% notes and some 50% notes, and it is rolled against the project's "
		"own MIDI seed once per note trigger. Reversible through the ProjectJournal (MidiClip "
		"checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
		{QStringLiteral("probability"), numberProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("note"), QStringLiteral("probability")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
		{QStringLiteral("probability"), numberProperty()},
		{QStringLiteral("position"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("key"), integerProperty(0, 127)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
		if (clip == nullptr) { return error; }
		int index = -1;
		Note* note = resolveNote(clip, args.value(QStringLiteral("note")).toString(), &index, &error);
		if (note == nullptr) { return error; }

		const double probability = args.value(QStringLiteral("probability")).toDouble();
		if (!checkProbability(probability, &error)) { return error; }

		const float previous = note->probability();
		clip->addJournalCheckPoint();
		note->setProbability(static_cast<float>(probability));
		clip->dataChanged();

		QJsonObject result = noteState(note, index);
		result.insert(QStringLiteral("clip"), clipId(ref.id));
		// The note's own id (Note::id(), SPEC-stable-ids.md slice 2), never its
		// position in the clip's note list.
		result.insert(QStringLiteral("note"), noteIdOf(note));
		// The field is reported by name as well as through noteState(), because
		// noteState() is the shape roll.get_state publishes for EVERY note and this
		// verb must not change that shape for callers that do not use it.
		result.insert(QStringLiteral("probability"), static_cast<double>(note->probability()));

		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), clipId(ref.id));
		// The inverse addresses the note by ID, so a re-sort of the clip's list
		// (this clip's own, or one a later edit causes) cannot re-point it at a
		// different note - which is exactly what an index-addressed inverse did.
		inverseArgs.insert(QStringLiteral("note"), noteIdOf(note));
		inverseArgs.insert(QStringLiteral("probability"), static_cast<double>(previous));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(
				QJsonObject{{QStringLiteral("clip"), clipId(ref.id)},
					{QStringLiteral("note"), noteIdOf(note)},
					{QStringLiteral("probability"), static_cast<double>(previous)}},
				QStringLiteral("note.probability_set"), inverseArgs, true,
				MechanismClipCheckpoint));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerNoteProbabilityCommands(ControlRegistry& registry)
{
	registerNoteProbabilitySet(registry);
}

} // namespace lmms
