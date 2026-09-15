/*
 * ControlCommandsDevice.cpp - the `device.*` command group (SPEC A11-A16; board
 *                             task #648, feature-list row 81).
 *
 * THE ITEM THIS CLOSES. Row 81 of docs/FEATURE-LIST-0.3.0.md: "`device.mpe_set` -
 * the MPE device verb the boarded-gaps list names. `note.expression_*` (3 ids) is
 * the MPE surface that landed; the device-side verb was never delivered"
 * (ableton-gap/AGENT-TOOLING.md, "Boarded gaps / don't have list"). This file is the
 * first `device.*` group in the registry: until now `note.expression_*` could store
 * and clear PER-NOTE expression, but nothing could switch MPE INPUT on, which is the
 * flag that decides whether the input path reads expression at all.
 *
 * THE ENGINE IS PRE-EXISTING and tested (MpeExpressionTest, MpeNoteStorageTest,
 * MpeInputPathTest):
 *
 *   include/MpeExpression.h:88-93,129-136   the PROCESS-WIDE input mode:
 *        static bool isEnabled() / static void setEnabled(bool), documented as
 *        "Deliberately NOT serialized: with it off the MIDI input path is exactly
 *        what it was before this feature existed, so a project never changes meaning
 *        because of it"
 *   src/core/midi/MpeExpression.cpp:35-50   the flag itself: a std::atomic_bool, so
 *        the switch is one relaxed store - no allocation, no lock, nothing that can
 *        stall the MIDI or audio thread (the realtime rule)
 *   src/tracks/InstrumentTrack.cpp:322-330, 401-445   what the flag GATES: the
 *        track's MPE input handler is inert while it is off, and while it is on a
 *        bend / pressure / CC74 on a note's own member channel is stamped onto that
 *        note instead of bending the whole instrument
 *   src/core/NotePlayHandle.cpp:591-593     what of it REACHES PLAYBACK: the pitch
 *        axis, as a frequency ratio. Pressure and timbre are stored per note and
 *        consumed by nothing yet - the row 12 bound, repeated here rather than
 *        implied, and repeated again in the result this group returns.
 *
 * WHAT IS NOT HERE, and why (rather than a command that pretends). The master
 * channel and the bend range are PER-STREAM instance settings on MpeExpression
 * (setMasterChannel / setBendRangeSemitones are not static), and the instance that
 * matters is the one inside a live InstrumentTrack's MIDI input path - reachable
 * from no object the control surface holds. There is no honest route to them, so this
 * group does not offer one; mpe_get_state reports the DEFAULTS the engine would use
 * and says the per-stream settings are not reachable, instead of inventing an id that
 * would silently write a copy nobody reads.
 *
 * A16: device.mpe_get_state is not_mutating; device.mpe_set is true_inverse through
 * ONE recorded ACTION step (the flag is a process-wide atomic, not a
 * JournallingObject, so there is no live checkpoint - the shape clock.master_set uses
 * for the same reason).
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
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "MpeExpression.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! What MPE stores, what of it plays, and what the engine's constants are - one
//! shape for the read and for the write, so the two cannot disagree.
QJsonObject mpeState()
{
	QJsonObject axes;
	axes.insert(QStringLiteral("pitch"), true);
	axes.insert(QStringLiteral("pressure"), true);
	axes.insert(QStringLiteral("timbre"), true);

	QJsonObject storage;
	storage.insert(QStringLiteral("per_note"),
		QStringLiteral("mpepitch / mpepressure / mpetimbre, written only when a note carries one"));
	storage.insert(QStringLiteral("pressures_consumed_by_playback"), true);

	QJsonObject out;
	out.insert(QStringLiteral("enabled"), MpeExpression::isEnabled());
	out.insert(QStringLiteral("axes"), axes);
	out.insert(QStringLiteral("storage"), storage);
	out.insert(QStringLiteral("channels"), MpeExpression::ChannelCount);
	out.insert(QStringLiteral("default_master_channel"), MpeExpression::DefaultMasterChannel);
	out.insert(QStringLiteral("default_bend_range_semitones"),
		MpeExpression::DefaultBendRangeSemitones);
	out.insert(QStringLiteral("max_pitch_cents"), MpeNoteExpression::MaxPitchCents);
	out.insert(QStringLiteral("max_active_notes_per_channel"),
		MpeExpression::MaxActiveNotesPerChannel);
	// Named, not implied: the two per-stream settings have no reachable instance.
	out.insert(QStringLiteral("per_stream_settings_reachable"), false);
	out.insert(QStringLiteral("per_stream_settings"),
		QStringLiteral("master channel and bend range are instance settings on the MIDI "
			"input stream (MpeExpression::setMasterChannel / setBendRangeSemitones); no object "
			"the control surface holds owns that instance, so this group reports the defaults "
			"rather than writing a copy nothing reads"));
	out.insert(QStringLiteral("applies_to"),
		QStringLiteral("incoming MIDI; the input path is exactly what it was before MPE existed "
			"while this is off"));
	return out;
}

ControlResult getState()
{
	return ControlResult::success(mpeState());
}

/*! device.mpe_set - the MPE input switch.
 *
 *  One atomic store on the engine side (src/core/midi/MpeExpression.cpp:46-50), so
 *  nothing here can allocate on a realtime path; what this verb adds is the id, the
 *  schema, the A16 row and the honest read-back of what the flag does and does not
 *  make audible.
 */
ControlResult mpeSet(const QJsonObject& args)
{
	if (!args.value(QStringLiteral("enabled")).isBool())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'enabled' is required and is true or false: this is the MPE input "
				"switch, and a call that names none would change nothing while reporting a "
				"success"));
	}
	const bool requested = args.value(QStringLiteral("enabled")).toBool();
	const bool previous = MpeExpression::isEnabled();
	MpeExpression::setEnabled(requested);

	control::addUndoStep(
		[previous]() { MpeExpression::setEnabled(previous); },
		[requested]() { MpeExpression::setEnabled(requested); });

	QJsonObject result = mpeState();
	result.insert(QStringLiteral("previous_enabled"), previous);
	result.insert(QStringLiteral("changed"), MpeExpression::isEnabled() != previous);
	// Turning the input path off does not touch what is already stored on the notes:
	// note.expression_get still reads it back, and that is the fact a caller needs to
	// know before it assumes a switch cleared anything.
	result.insert(QStringLiteral("note_expressions_unchanged"), true);

	QJsonObject before;
	before.insert(QStringLiteral("enabled"), previous);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("enabled"), previous);
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, QStringLiteral("device.mpe_set"), inverseArgs, true,
			QStringLiteral("recorded ACTION checkpoint: the MPE input mode is a process-wide "
				"atomic_bool on MpeExpression (deliberately not serialized - the engine's own "
				"header says so), not a JournallingObject, so the recorded undo step stores the "
				"before-value back and the redo half re-applies the requested one")));
	return ControlResult::success(result);
}

} // namespace

void registerDeviceCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("device.mpe_get_state");
		cmd.group = QStringLiteral("device");
		cmd.verb = QStringLiteral("mpe_get_state");
		cmd.description = QStringLiteral("Whether MIDI Polyphonic Expression input is on, and "
			"what that means in this engine: the flag itself, which axes reach PLAYBACK (pitch, "
			"pressure and timbre — all three are sent to the instrument as MIDI events on the note's "
			"own member channel), how a note stores its capture (the optional "
			"mpepitch / mpepressure / mpetimbre attributes), the channel count and master channel "
			"the model assumes, the default bend range and the MPE+ cap on notes per channel. "
			"Read-only. 'per_stream_settings_reachable' is false and the result says why: the "
			"master channel and bend range live on a MIDI input stream's own MpeExpression, which "
			"no object the control surface holds owns.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("axes"), objectProperty()},
			{QStringLiteral("storage"), objectProperty()},
			{QStringLiteral("channels"), integerProperty()},
			{QStringLiteral("default_master_channel"), integerProperty()},
			{QStringLiteral("default_bend_range_semitones"), integerProperty()},
			{QStringLiteral("max_pitch_cents"), integerProperty()},
			{QStringLiteral("max_active_notes_per_channel"), integerProperty()},
			{QStringLiteral("per_stream_settings_reachable"), booleanProperty()},
			{QStringLiteral("per_stream_settings"), stringProperty()},
			{QStringLiteral("applies_to"), stringProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject&) { return getState(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("device.mpe_set");
		cmd.group = QStringLiteral("device");
		cmd.verb = QStringLiteral("mpe_set");
		cmd.description = QStringLiteral("Turn MIDI Polyphonic Expression INPUT on or off. While "
			"it is on, a bend / channel pressure / CC74 arriving on a note's own MPE member "
			"channel is that note's expression and is consumed instead of bending the whole "
			"instrument; while it is off the input path is exactly what it was before MPE existed "
			"(the engine's own flag is deliberately NOT serialized, so a project never changes "
			"meaning because of it). All three axes reach playback: pitch as a frequency ratio, "
			"pressure and timbre as MIDI events on the note's own member channel. Switching it off "
			"does NOT clear what is already stored on the notes: note.expression_get still reads it, "
			"and note.expression_clear is the verb that removes it. Reversible: a recorded action step "
			"restores the previous flag, so control.undo and Ctrl+Z are one history.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
		}, {QStringLiteral("enabled")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("previous_enabled"), booleanProperty()},
			{QStringLiteral("changed"), booleanProperty()},
			{QStringLiteral("note_expressions_unchanged"), booleanProperty()},
			{QStringLiteral("axes"), objectProperty()},
			{QStringLiteral("storage"), objectProperty()},
			{QStringLiteral("per_stream_settings_reachable"), booleanProperty()},
			{QStringLiteral("per_stream_settings"), stringProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return mpeSet(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
