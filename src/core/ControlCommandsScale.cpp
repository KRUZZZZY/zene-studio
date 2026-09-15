/*
 * ControlCommandsScale.cpp - the `scale.*` group's READ half and its two context
 *                             writers: scale.list, scale.get_state, scale.root_set,
 *                             scale.set (SPEC A11-A16; board task #648, feature-list
 *                             row 66).
 *
 * THE ITEM THIS CLOSES. Row 66 of docs/FEATURE-LIST-0.3.0.md: "no `scale.` id exists
 * at either base; the engine (the scale + key vocabulary) is pre-existing, so the
 * work is the group and its proofs". The boarded-gaps list names two of the group's
 * ids by name - `scale.root_set` and `scale.set` (ableton-gap/AGENT-TOOLING.md,
 * "Boarded gaps / don't have list"). scale.snap_notes, the one verb here that EDITS
 * a clip, is in ControlCommandsScaleEdit.cpp (the read/edit split every recent group
 * in this fork uses, because Gate 7 measures a file and the group is one group).
 *
 * THE ENGINE. include/InstrumentFunctions.h:137-185 - ChordTable: 95 named chords and
 * scales, `isScale()` meaning "more than six degrees", each degree a semitone offset
 * from the root, and getScaleByName() returning an EMPTY chord for a name it does not
 * carry (which is how "not found" is reported). Everything this group publishes is
 * that table's own vocabulary; nothing here invents a scale.
 *
 * WHERE THE CONTEXT LIVES, plainly, because it is the one thing a reader could
 * otherwise have to guess: the piano roll's key/scale selector is GUI state owned by
 * its window (PianoRoll::m_keyModel / m_scaleModel, written by
 * PianoRollWindow::saveSettings), and a headless instance has no such window. So
 * scale.root_set / scale.set write THIS GROUP's context - process state, deliberately
 * not serialized, like MpeExpression::isEnabled() (include/MpeExpression.h:88-93).
 * Every other verb here defaults to it when the caller names no root or scale, and
 * scale.get_state is how it is read back.
 *
 * A16: the two reads are not_mutating; the two writers are true_inverse through ONE
 * recorded ACTION step each (the context is not a JournallingObject, so there is no
 * live checkpoint - the shape clock.master_set uses for the same reason).
 *
 * UI ABSENCE: the piano roll's key and scale combo boxes are unchanged and are not
 * wired to this group in either direction; nothing in the interface shows or edits
 * the context (docs/KNOWN-LIMITATIONS.md, the row 66 line).
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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlScaleShared.h"
#include "InstrumentFunctions.h"
#include "MidiClip.h"
#include "NoteTransform.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

using ChordTable = InstrumentFunctionNoteStacking::ChordTable;
using Chord = InstrumentFunctionNoteStacking::Chord;

//! One entry per scale (or chord) the engine knows, resolved against \a root.
QJsonArray scaleEntries(int root, bool includeChords)
{
	QJsonArray out;
	for (const Chord& chord : ChordTable::getInstance().chords())
	{
		if (chord.isEmpty() || chord.isScale() != !includeChords) { continue; }
		std::vector<int> degrees;
		degrees.reserve(static_cast<size_t>(chord.size()));
		for (int i = 0; i < chord.size(); ++i) { degrees.push_back(chord[i]); }
		const std::vector<int> classes = scalePitchClasses(root, degrees);
		QJsonObject entry;
		entry.insert(QStringLiteral("name"), chord.getName());
		entry.insert(QStringLiteral("size"), chord.size());
		entry.insert(QStringLiteral("is_scale"), chord.isScale());
		entry.insert(QStringLiteral("degrees"), scaleDegreesJson(degrees));
		entry.insert(QStringLiteral("pitch_classes"), scaleClassesJson(classes));
		entry.insert(QStringLiteral("mask"), scaleMask(classes));
		out.append(entry);
	}
	return out;
}

ControlResult listScales(const QJsonObject& args)
{
	ResolvedScale resolved;
	ControlResult error;
	// The root matters here: every entry's pitch classes are relative to it.
	if (!resolveScale(args, &resolved, &error)) { return error; }
	const bool includeChords = args.value(QStringLiteral("include_chords")).toBool(false);

	QJsonArray roots;
	for (int index = 0; index < scaleRootNames().size(); ++index)
	{
		roots.append(QJsonObject{{QStringLiteral("index"), index},
			{QStringLiteral("name"), scaleRootNames().at(index)}});
	}
	QJsonObject result = scaleContextState();
	result.insert(QStringLiteral("resolved_root"), resolved.root);
	result.insert(QStringLiteral("resolved_root_name"), scaleRootNames().at(resolved.root));
	result.insert(QStringLiteral("roots"), roots);
	result.insert(QStringLiteral("scales"), scaleEntries(resolved.root, includeChords));
	result.insert(QStringLiteral("include_chords"), includeChords);
	return ControlResult::success(result);
}

ControlResult getState(const QJsonObject& args)
{
	ResolvedScale resolved;
	ControlResult error;
	if (!resolveScale(args, &resolved, &error)) { return error; }

	QJsonObject result = scaleContextState();
	result.insert(QStringLiteral("resolved_root"), resolved.root);
	result.insert(QStringLiteral("resolved_root_name"), scaleRootNames().at(resolved.root));
	result.insert(QStringLiteral("resolved_scale"), resolved.scale);
	result.insert(QStringLiteral("resolved_from_arguments"), resolved.fromArguments);
	result.insert(QStringLiteral("pitch_classes"), scaleClassesJson(resolved.classes));
	result.insert(QStringLiteral("mask"), scaleMask(resolved.classes));

	if (args.contains(QStringLiteral("clip")))
	{
		ClipRef ref;
		MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref,
			&error);
		if (clip == nullptr) { return error; }
		const int total = static_cast<int>(clip->notes().size());
		const int inScale = countNotesInScale(*clip, resolved.classes);
		result.insert(QStringLiteral("clip"), clipId(ref.ordinal));
		result.insert(QStringLiteral("track"), trackIdOf(ref.track));
		result.insert(QStringLiteral("note_count"), total);
		result.insert(QStringLiteral("notes_in_scale"), inScale);
		result.insert(QStringLiteral("notes_out_of_scale"), total - inScale);
	}
	return ControlResult::success(result);
}

/*! Records the inverse of a context edit as ONE action step on the engine's own
 *  undo stack, and returns the transaction payload.
 *
 *  The context is not a JournallingObject, so there is no live checkpoint: what is
 *  recorded is the bounded pair (root, scale name) written back by a step - the
 *  clock.master_set shape, and the reason both writers here are true_inverse rather
 *  than snapshot.
 */
QJsonObject contextTransaction(const QString& command, int previousRoot, const QString& previousScale,
	int requestedRoot, const QString& requestedScale)
{
	control::addUndoStep(
		[previousRoot, previousScale]() {
			scaleContext().root = previousRoot;
			scaleContext().scale = previousScale;
		},
		[requestedRoot, requestedScale]() {
			scaleContext().root = requestedRoot;
			scaleContext().scale = requestedScale;
		});
	QJsonObject before;
	before.insert(QStringLiteral("root"), previousRoot);
	before.insert(QStringLiteral("scale"), previousScale);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("root"), previousRoot);
	inverseArgs.insert(QStringLiteral("scale"), previousScale);
	return transactionPayload(before, command, inverseArgs, true,
		QStringLiteral("recorded ACTION checkpoint: the root/scale context is this group's own "
			"process state and not a JournallingObject, so the recorded undo step writes the "
			"before-state's root and scale back (the shape clock.master_set records a mode and a "
			"port with, for the same reason)"));
}

ControlResult rootSet(const QJsonObject& args)
{
	if (!args.contains(QStringLiteral("root")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'root' is required: this verb sets the context's root, and a call that "
				"names none would change nothing while reporting a success"));
	}
	ControlResult error;
	int root = scaleContext().root;
	if (!readScaleRoot(args, QStringLiteral("root"), &root, &error)) { return error; }

	const int previousRoot = scaleContext().root;
	const QString previousScale = scaleContext().scale;
	scaleContext().root = root;

	QJsonObject result = scaleContextState();
	result.insert(QStringLiteral("previous_root"), previousRoot);
	result.insert(QStringLiteral("__transaction"),
		contextTransaction(QStringLiteral("scale.root_set"), previousRoot, previousScale, root,
			previousScale));
	return ControlResult::success(result);
}

ControlResult scaleSet(const QJsonObject& args)
{
	if (!args.contains(QStringLiteral("scale")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'scale' is required (an empty string clears the context): a call that "
				"names none would change nothing while reporting a success"));
	}
	ControlResult error;
	QString scale;
	if (!readScaleName(args, &scale, &error)) { return error; }

	const int previousRoot = scaleContext().root;
	const QString previousScale = scaleContext().scale;
	scaleContext().scale = scale;

	QJsonObject result = scaleContextState();
	result.insert(QStringLiteral("previous_scale"), previousScale);
	result.insert(QStringLiteral("__transaction"),
		contextTransaction(QStringLiteral("scale.set"), previousRoot, previousScale, previousRoot,
			scale));
	return ControlResult::success(result);
}

//! The context block's own result keys, declared once: every read and every write
//! of this group publishes exactly this shape (scaleContextState() builds it), and a
//! schema that drifted from the result would be a lie an agent plans against.
void insertContextSchema(QJsonObject* schema)
{
	QJsonObject properties = schema->value(QStringLiteral("properties")).toObject();
	properties.insert(QStringLiteral("root"), integerProperty(0, 11));
	properties.insert(QStringLiteral("root_name"), stringProperty());
	properties.insert(QStringLiteral("scale"), stringProperty());
	properties.insert(QStringLiteral("degrees"), arrayProperty());
	properties.insert(QStringLiteral("pitch_classes"), arrayProperty());
	properties.insert(QStringLiteral("mask"), stringProperty());
	properties.insert(QStringLiteral("scale_set"), booleanProperty());
	schema->insert(QStringLiteral("properties"), properties);
}

} // namespace

void registerScaleCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("scale.list");
		cmd.group = QStringLiteral("scale");
		cmd.verb = QStringLiteral("list");
		cmd.description = QStringLiteral("Every scale this engine knows and the twelve keys, with "
			"each scale's degrees (semitone offsets from the root), its pitch classes, its "
			"twelve-character membership mask (index 0 = C, so \"101010110101\" is the major "
			"scale) and its size. The vocabulary is ChordTable's own - 95 named entries, of which "
			"the scales are the ones with more than six degrees; 'include_chords' adds the chord "
			"entries too. 'root' (a key index 0..11 or a name such as \"C#\"/\"Db\") shifts every "
			"pitch class and every mask; absent, the group's own context root answers. Read-only.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("root"), rootArgumentSchema()},
			{QStringLiteral("include_chords"), booleanProperty()},
		});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("roots"), arrayProperty()},
			{QStringLiteral("scales"), arrayProperty()},
			{QStringLiteral("include_chords"), booleanProperty()},
			{QStringLiteral("resolved_root"), integerProperty(0, 11)},
			{QStringLiteral("resolved_root_name"), stringProperty()},
		});
		insertContextSchema(&cmd.resultSchema);
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return listScales(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("scale.get_state");
		cmd.group = QStringLiteral("scale");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("What this group is resolving against: the context's "
			"root, scale name, degrees, pitch classes and mask. Naming 'root' and/or 'scale' "
			"answers for those instead and says so ('resolved_from_arguments'), without changing "
			"the context. A 'clip' argument adds the analysis an arranging caller wants - how many "
			"of its notes are in the scale and how many are out, counted with the engine's own "
			"predicate (NoteTransform::matches) - so \"this part is in key\" is a measurement. A "
			"context with no scale set yet reports an empty 'scale', an empty mask and "
			"scale_set:false. Read-only.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("root"), rootArgumentSchema()},
			{QStringLiteral("scale"), stringProperty()},
			{QStringLiteral("clip"), stringProperty()},
		});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("resolved_root"), integerProperty(0, 11)},
			{QStringLiteral("resolved_root_name"), stringProperty()},
			{QStringLiteral("resolved_scale"), stringProperty()},
			{QStringLiteral("resolved_from_arguments"), booleanProperty()},
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("track"), stringProperty()},
			{QStringLiteral("note_count"), integerProperty()},
			{QStringLiteral("notes_in_scale"), integerProperty()},
			{QStringLiteral("notes_out_of_scale"), integerProperty()},
		});
		insertContextSchema(&cmd.resultSchema);
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return getState(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("scale.root_set");
		cmd.group = QStringLiteral("scale");
		cmd.verb = QStringLiteral("root_set");
		cmd.description = QStringLiteral("Set the CONTEXT's root - the key every later "
			"scale.snap_notes and every scale.get_state resolves against when it names no root. "
			"Accepts a key index 0..11 or a name (\"C\", \"c\", \"C#\", \"Db\", \"D♭\", or the "
			"piano roll's \"C# / Db\" spelling; an octave suffix is ignored, because a root here "
			"is a pitch class). An unknown name is refused rather than defaulted to C. Reversible: "
			"a recorded action step restores the previous root. The context is this group's own "
			"process state and is deliberately not serialized - it is NOT the piano roll's key "
			"selector (docs/KNOWN-LIMITATIONS.md).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("root"), rootArgumentSchema()},
		}, {QStringLiteral("root")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("previous_root"), integerProperty(0, 11)},
		});
		insertContextSchema(&cmd.resultSchema);
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return rootSet(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("scale.set");
		cmd.group = QStringLiteral("scale");
		cmd.verb = QStringLiteral("set");
		cmd.description = QStringLiteral("Set the CONTEXT's scale by NAME - a scale this engine "
			"knows, from scale.list (e.g. \"Major\", \"Minor\", \"Major pentatonic\"). An empty "
			"string CLEARS the context's scale, after which scale.snap_notes refuses instead of "
			"guessing. A name ChordTable does not carry is refused typed (NotFound), not resolved "
			"to an empty scale. Reversible: a recorded action step restores the previous scale. "
			"The context is this group's own process state and is deliberately not serialized - it "
			"is NOT the piano roll's scale selector (docs/KNOWN-LIMITATIONS.md).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("scale"), stringProperty()},
		}, {QStringLiteral("scale")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("previous_scale"), stringProperty()},
		});
		insertContextSchema(&cmd.resultSchema);
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return scaleSet(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
