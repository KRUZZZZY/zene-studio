/*
 * ControlCommandsNoteStep.cpp - note.step_get_state and note.step_set: the melodic step view of an
 *                               ordinary MIDI clip (Q050-294.1, stage 1 of 3)
 *
 * A melodic step sequencer is a GRID VIEW of a MidiClip, not a second pattern format: a step is a
 * column of `step_ticks` ticks, a cell is a (step, pitch) pair, and every cell is one ordinary
 * Note in the clip's own note list - so the piano roll, note.list and roll.get_state show exactly
 * what the step view shows, and old projects open unchanged. The three per-cell settings the
 * step view edits are fields the engine already carries and saves:
 *
 *   velocity  Note::getVolume() / setVolume()        the engine's 0..200 note volume (default 100)
 *   chance    Note::probability() / setProbability() 0..1, 1 = always (default; the optional `prob` attribute)
 *   ratchet   NoteConditions::repeats + 1            1 = one attack (default); n = n evenly spaced attacks
 *                                                    inside the note (the optional `rep` attribute, played by
 *                                                    InstrumentTrack::triggerNote - not a display value)
 *
 * note.step_get_state reads that view (not_mutating). note.step_set edits it: a batch of cell edits
 * that is validated whole before anything is written, then taken as ONE MidiClip journal checkpoint,
 * so ONE control.undo restores the exact prior note list (ids included). A batch that changes
 * nothing takes no checkpoint and records no undo step (control::checkpointNoteEdit's rule).
 *
 * Scale awareness: with a scale set (scale.set, or `root`/`scale` named on the call) a pitch the
 * step view WRITES - a new cell, or an existing note given a new `key` - is snapped to the nearest
 * in-scale key (`scale_mode` snap, a tie going down), refused (`strict`), or left alone (`off`).
 * A note that is out of scale and is not given a new key is never touched by the scale: its
 * velocity, chance and ratchet stay editable.
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

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "ControlCommandsNoteShared.h"
#include "ControlEdit.h"
#include "ControlNoteShared.h"
#include "ControlRegistry.h"
#include "ControlScaleShared.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteConditions.h"
#include "NoteTransform.h"

namespace lmms
{

using namespace control;
using namespace control::notes;

namespace
{

//! A sixteenth note in the engine's 192-ticks-per-bar time (TimePos::ticksPerBar() / 16).
constexpr int DefaultStepTicks = 12;
constexpr int MaxStepTicks = 3072;
//! Ratchet n is n attacks: n - 1 repeats, and NoteConditions::MaxRepeats (16) is the engine's bound on those.
constexpr int MaxStepRatchet = 16;
static_assert(MaxStepRatchet - 1 <= NoteConditions::MaxRepeats, "a ratchet is stored as repeats = ratchet - 1");

enum class ScaleMode { Snap, Strict, Off };

//! The scale one call resolves against: the context, or the root / scale the call names.
struct StepScale
{
	ResolvedScale resolved;
	ScaleMode mode = ScaleMode::Snap;

	bool active() const { return !resolved.degrees.empty() && mode != ScaleMode::Off; }
};

//! The wire form of the grid: its resolution and the scale the cells are read against.
struct Grid
{
	int stepTicks = DefaultStepTicks;
	StepScale scale;
};

bool readStepTicks(const QJsonObject& args, int* out, ControlResult* error)
{
	const QJsonValue value = args.value(QStringLiteral("step_ticks"));
	if (value.isUndefined()) { return true; }
	const double ticks = value.toDouble();
	if (!value.isDouble() || ticks != std::floor(ticks) || ticks < 1 || ticks > MaxStepTicks)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'step_ticks' must be a whole number of ticks in [1, %1] (default %2, a sixteenth note)")
				.arg(MaxStepTicks).arg(DefaultStepTicks));
		return false;
	}
	*out = static_cast<int>(ticks);
	return true;
}

bool readScaleMode(const QJsonObject& args, ScaleMode* out, ControlResult* error)
{
	const QJsonValue value = args.value(QStringLiteral("scale_mode"));
	if (value.isUndefined()) { return true; }
	const QString mode = value.toString();
	if (mode == QStringLiteral("snap")) { *out = ScaleMode::Snap; return true; }
	if (mode == QStringLiteral("strict")) { *out = ScaleMode::Strict; return true; }
	if (mode == QStringLiteral("off")) { *out = ScaleMode::Off; return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'scale_mode' is snap, strict or off"));
	return false;
}

bool readGrid(const QJsonObject& args, Grid* grid, ControlResult* error)
{
	return readStepTicks(args, &grid->stepTicks, error) && resolveScale(args, &grid->scale.resolved, error)
		&& readScaleMode(args, &grid->scale.mode, error);
}

// ---------------------------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------------------------

//! 1-based position of @a key's pitch class among the scale's degrees from its root; 0 when out of scale.
int scaleDegreeOf(int key, const ResolvedScale& scale)
{
	const int offset = ((key - scale.root) % 12 + 12) % 12;
	const auto it = std::find(scale.degrees.begin(), scale.degrees.end(), offset);
	return it == scale.degrees.end() ? 0 : static_cast<int>(it - scale.degrees.begin()) + 1;
}

QJsonObject stepNoteJson(const Note* note, const Grid& grid)
{
	const int position = note->pos().getTicks();
	const int key = note->key();
	const bool scaleSet = !grid.scale.resolved.degrees.empty();
	const int degree = scaleSet ? scaleDegreeOf(key, grid.scale.resolved) : 0;
	return {{QStringLiteral("note"), noteIdOf(note)}, {QStringLiteral("step"), position / grid.stepTicks},
		{QStringLiteral("offset"), position % grid.stepTicks}, {QStringLiteral("position"), position},
		{QStringLiteral("length"), note->length().getTicks()}, {QStringLiteral("key"), key},
		{QStringLiteral("pitch_class"), ((key % 12) + 12) % 12},
		{QStringLiteral("velocity"), static_cast<int>(note->getVolume())},
		{QStringLiteral("chance"), static_cast<double>(note->probability())},
		{QStringLiteral("ratchet"), note->conditions().repeats + 1},
		{QStringLiteral("muted"), note->muted()},
		{QStringLiteral("in_scale"), scaleSet ? QJsonValue(degree > 0) : QJsonValue(QJsonValue::Null)},
		{QStringLiteral("scale_degree"), degree}};
}

QJsonObject scaleBlock(const ResolvedScale& scale)
{
	return {{QStringLiteral("root"), scale.root}, {QStringLiteral("root_name"), scaleRootNames().at(scale.root)},
		{QStringLiteral("scale"), scale.scale}, {QStringLiteral("scale_set"), !scale.degrees.empty()},
		{QStringLiteral("degrees"), scaleDegreesJson(scale.degrees)},
		{QStringLiteral("pitch_classes"), scaleClassesJson(scale.classes)},
		{QStringLiteral("mask"), scaleMask(scale.classes)}};
}

//! The occupied columns of the grid, in step order: each lists its notes' ids and pitches, lowest pitch first.
QJsonArray columnsJson(const MidiClip* clip, const Grid& grid)
{
	std::map<int, std::vector<const Note*>> byStep;
	for (const Note* note : clip->notes()) { byStep[note->pos().getTicks() / grid.stepTicks].push_back(note); }
	QJsonArray columns;
	for (auto& [step, column] : byStep)
	{
		std::stable_sort(column.begin(), column.end(), [](const Note* a, const Note* b) { return a->key() < b->key(); });
		QJsonArray ids;
		QJsonArray pitches;
		for (const Note* note : column)
		{
			ids.append(noteIdOf(note));
			pitches.append(note->key());
		}
		columns.append(QJsonObject{{QStringLiteral("step"), step}, {QStringLiteral("position"), step * grid.stepTicks},
			{QStringLiteral("notes"), ids}, {QStringLiteral("pitches"), pitches}});
	}
	return columns;
}

QJsonObject stepState(const ClipRef& ref, const MidiClip* clip, const Grid& grid)
{
	QJsonArray notes;
	for (const Note* note : clip->notes()) { notes.append(stepNoteJson(note, grid)); }
	const int length = clip->length().getTicks();
	return {{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("step_ticks"), grid.stepTicks},
		{QStringLiteral("steps"), std::max(1, (length + grid.stepTicks - 1) / grid.stepTicks)},
		{QStringLiteral("clip_length"), length}, {QStringLiteral("scale"), scaleBlock(grid.scale.resolved)},
		{QStringLiteral("count"), notes.size()}, {QStringLiteral("notes"), notes},
		{QStringLiteral("columns"), columnsJson(clip, grid)}};
}

ControlResult getStepState(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	Grid grid;
	if (clip == nullptr || !readGrid(args, &grid, &error)) { return error; }
	return ControlResult::success(stepState(ref, clip, grid));
}

// ---------------------------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------------------------

//! One entry of `edits` as the caller wrote it, checked for type and range but not yet resolved.
struct StepEdit
{
	int index = 0;
	QString noteId;                //!< addresses an existing note, or
	std::optional<int> step;       //!< addresses a cell with `key`
	std::optional<int> key;
	std::optional<int> velocity;
	std::optional<double> chance;
	std::optional<int> ratchet;
	std::optional<int> length;
	bool remove = false;
};

bool badField(ControlResult* error, int index, const QString& field, const QString& rule)
{
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("edits[%1].%2 %3").arg(index).arg(field, rule));
	return false;
}

bool readWhole(const QJsonObject& entry, const char* name, const StepEdit& edit, std::pair<int, int> range,
	std::optional<int>* out, ControlResult* error)
{
	const QJsonValue value = entry.value(QLatin1String(name));
	if (value.isUndefined()) { return true; }
	const double v = value.toDouble();
	if (!value.isDouble() || v != std::floor(v) || v < range.first || v > range.second)
	{
		return badField(error, edit.index, QLatin1String(name),
			QStringLiteral("must be a whole number in [%1, %2]").arg(range.first).arg(range.second));
	}
	*out = static_cast<int>(v);
	return true;
}

bool readChance(const QJsonObject& entry, StepEdit* edit, ControlResult* error)
{
	const QJsonValue value = entry.value(QStringLiteral("chance"));
	if (value.isUndefined()) { return true; }
	const double v = value.toDouble();
	// isfinite first: a NaN fails both range comparisons and would slip through them.
	if (!value.isDouble() || !std::isfinite(v) || v < 0.0 || v > 1.0)
	{
		return badField(error, edit->index, QStringLiteral("chance"),
			QStringLiteral("must be a number in [0, 1]: 1 always plays, 0 never plays"));
	}
	edit->chance = v;
	return true;
}

bool checkEntryKeys(const QJsonObject& entry, int index, ControlResult* error)
{
	const QJsonValue noteId = entry.value(QStringLiteral("note"));
	if (!noteId.isUndefined() && (!noteId.isString() || noteId.toString().isEmpty()))
	{
		return badField(error, index, QStringLiteral("note"), QStringLiteral("must be a non-empty note id string"));
	}
	static const QStringList known{QStringLiteral("note"), QStringLiteral("step"), QStringLiteral("key"),
		QStringLiteral("velocity"), QStringLiteral("chance"), QStringLiteral("ratchet"), QStringLiteral("length"),
		QStringLiteral("remove")};
	for (auto it = entry.begin(); it != entry.end(); ++it)
	{
		if (!known.contains(it.key())) { return badField(error, index, it.key(), QStringLiteral("is not a step field")); }
	}
	return true;
}

//! A cell is named by step and key, a note by its id; never both, and never neither.
bool checkAddressing(const StepEdit& edit, ControlResult* error)
{
	if (!edit.noteId.isEmpty() && edit.step.has_value())
	{
		return badField(error, edit.index, QStringLiteral("step"), QStringLiteral("cannot be given beside 'note': name a note or a cell"));
	}
	if (edit.noteId.isEmpty() && !(edit.step.has_value() && edit.key.has_value()))
	{
		return badField(error, edit.index, QStringLiteral("note"), QStringLiteral("or both 'step' and 'key' must name the cell to edit"));
	}
	return true;
}

bool readRemove(const QJsonObject& entry, StepEdit* edit, ControlResult* error)
{
	const QJsonValue value = entry.value(QStringLiteral("remove"));
	if (value.isUndefined()) { return true; }
	if (!value.isBool()) { return badField(error, edit->index, QStringLiteral("remove"), QStringLiteral("must be true or false")); }
	edit->remove = value.toBool();
	return true;
}

bool readStepEdit(const QJsonObject& entry, int index, StepEdit* edit, ControlResult* error)
{
	edit->index = index;
	edit->noteId = entry.value(QStringLiteral("note")).toString();
	if (!checkEntryKeys(entry, index, error) || !readChance(entry, edit, error) || !readRemove(entry, edit, error)) { return false; }
	const bool ranges = readWhole(entry, "step", *edit, {0, MaxSongLength}, &edit->step, error)
		&& readWhole(entry, "key", *edit, {0, NumKeys - 1}, &edit->key, error)
		&& readWhole(entry, "velocity", *edit, {MinVolume, MaxVolume}, &edit->velocity, error)
		&& readWhole(entry, "ratchet", *edit, {1, MaxStepRatchet}, &edit->ratchet, error)
		&& readWhole(entry, "length", *edit, {1, MaxSongLength}, &edit->length, error);
	return ranges && checkAddressing(*edit, error);
}

//! What an edit resolved to: the note it changes (null for a new cell) and every value that note will have.
struct Target
{
	Note* note = nullptr;
	bool create = false;
	bool remove = false;
	int index = 0;
	int position = 0;
	int length = 0;
	int key = 0;
	int velocity = DefaultVolume;
	float chance = 1.f;
	int repeats = 0;
};

//! The note of @a clip at exactly @a position and @a key, or null (twins are possible; the first answers).
Note* findCell(const MidiClip* clip, int position, int key)
{
	for (Note* note : clip->notes())
	{
		if (note->pos().getTicks() == position && note->key() == key) { return note; }
	}
	return nullptr;
}

//! The key a PITCH WRITE ends up on under the scale mode; false (refused) when strict and out of scale.
bool scaledKey(int key, const StepScale& scale, int index, int* out, ControlResult* error)
{
	*out = key;
	if (!scale.active()) { return true; }
	const int snapped = NoteTransform::snapKeyToScale(key, scale.resolved.classes);
	if (snapped == key) { return true; }
	if (scale.mode == ScaleMode::Strict)
	{
		return badField(error, index, QStringLiteral("key"),
			QStringLiteral("%1 is not in the scale (scale_mode strict; snap would use %2)").arg(key).arg(snapped));
	}
	*out = snapped;
	return true;
}

//! The values an existing @a note keeps unless @a edit overrides them.
Target targetOfNote(Note* note, const StepEdit& edit)
{
	Target t;
	t.note = note;
	t.index = edit.index;
	t.position = note->pos().getTicks();
	t.length = note->length().getTicks();
	t.key = note->key();
	t.velocity = static_cast<int>(note->getVolume());
	t.chance = note->probability();
	t.repeats = note->conditions().repeats;
	return t;
}

void overrideTarget(Target* t, const StepEdit& edit)
{
	if (edit.velocity) { t->velocity = *edit.velocity; }
	if (edit.chance) { t->chance = static_cast<float>(*edit.chance); }
	if (edit.ratchet) { t->repeats = *edit.ratchet - 1; }
	if (edit.length) { t->length = *edit.length; }
}

//! A note-addressed edit: the note by id, and a changed `key` goes through the scale; the same key stays as it is.
bool resolveNoteTarget(MidiClip* clip, const StepEdit& edit, const Grid& grid, Target* out, ControlResult* error)
{
	int noteIndex = -1;
	Note* note = resolveNote(clip, edit.noteId, &noteIndex, error);
	if (note == nullptr) { return false; }
	*out = targetOfNote(note, edit);
	out->remove = edit.remove;
	if (edit.key && !edit.remove && *edit.key != note->key() && !scaledKey(*edit.key, grid.scale, edit.index, &out->key, error)) { return false; }
	overrideTarget(out, edit);
	return true;
}

//! A cell-addressed edit: the note already in the cell (exact key, then the key the scale would give), else a new one.
bool resolveCellTarget(MidiClip* clip, const StepEdit& edit, const Grid& grid, Target* out, ControlResult* error)
{
	const qint64 position = static_cast<qint64>(*edit.step) * grid.stepTicks;
	if (position > MaxSongLength) { return badField(error, edit.index, QStringLiteral("step"), QStringLiteral("is past the song limit")); }
	Note* existing = findCell(clip, static_cast<int>(position), *edit.key);
	int key = *edit.key;
	if (existing == nullptr && !edit.remove)
	{
		if (!scaledKey(*edit.key, grid.scale, edit.index, &key, error)) { return false; }
		existing = findCell(clip, static_cast<int>(position), key);
	}
	if (existing != nullptr) { *out = targetOfNote(existing, edit); }
	else
	{
		out->index = edit.index;
		out->create = true;
		out->position = static_cast<int>(position);
		out->length = grid.stepTicks;
		out->key = key;
	}
	out->remove = edit.remove;
	overrideTarget(out, edit);
	return true;
}

bool checkEnd(const Target& t, ControlResult* error)
{
	if (static_cast<qint64>(t.position) + t.length <= MaxSongLength) { return true; }
	return badField(error, t.index, QStringLiteral("length"), QStringLiteral("ends past the song limit"));
}

//! Whether this target changes the clip: a new cell, a removal, or any value that differs from the note's own.
bool changesClip(const Target& t)
{
	if (t.create) { return !t.remove; }
	if (t.remove) { return true; }
	return t.length != t.note->length().getTicks() || t.key != t.note->key()
		|| t.velocity != static_cast<int>(t.note->getVolume()) || t.chance != t.note->probability()
		|| t.repeats != t.note->conditions().repeats;
}

//! Records the note or cell @a target writes; false when an earlier entry of the batch already names it (two
//! writes to one cell would have no single answer).
bool claim(const Target& target, std::set<const Note*>* notes, std::set<std::pair<int, int>>* cells)
{
	if (target.note != nullptr) { return notes->insert(target.note).second; }
	return cells->insert({target.position, target.key}).second;
}

//! Resolve the whole batch; nothing is written unless every entry passes.
bool resolveTargets(MidiClip* clip, const QJsonArray& edits, const Grid& grid, std::vector<Target>* out, ControlResult* error)
{
	if (edits.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'edits' is empty"));
		return false;
	}
	std::set<const Note*> notes;
	std::set<std::pair<int, int>> cells;
	for (int i = 0; i < edits.size(); ++i)
	{
		StepEdit edit;
		Target target;
		if (!readStepEdit(edits.at(i).toObject(), i, &edit, error)) { return false; }
		const bool resolved = edit.noteId.isEmpty() ? resolveCellTarget(clip, edit, grid, &target, error)
			: resolveNoteTarget(clip, edit, grid, &target, error);
		if (!resolved || !checkEnd(target, error)) { return false; }
		if (!claim(target, &notes, &cells))
		{
			return badField(error, i, QStringLiteral("note"), QStringLiteral("repeats a note or cell an earlier entry edits"));
		}
		out->push_back(target);
	}
	return true;
}

//! Write one target: the new cell is added, an existing note is changed field by field, a removal comes out.
//! Returns the note left in the clip (null after a removal).
Note* applyTarget(MidiClip* clip, const Target& t)
{
	Note* note = t.note;
	if (t.remove)
	{
		if (note != nullptr) { clip->removeNote(note); }
		return nullptr;
	}
	if (t.create)
	{
		note = clip->addNote(Note(TimePos(t.length), TimePos(t.position), t.key, static_cast<volume_t>(t.velocity)), false);
	}
	else
	{
		note->setLength(TimePos(t.length));
		note->setKey(t.key);
		note->setVolume(static_cast<volume_t>(t.velocity));
	}
	note->setProbability(t.chance);
	NoteConditions conditions = note->conditions();
	conditions.repeats = t.repeats;
	note->setConditions(conditions);
	return note;
}

//! What a batch did, for the reply.
struct StepOutcome
{
	int edited = 0;
	int added = 0;
	int removed = 0;
	QJsonArray notes;
};

//! Writes every target that changes the clip (the checkpoint is already taken) and counts what happened.
StepOutcome applyTargets(MidiClip* clip, const std::vector<Target>& targets)
{
	StepOutcome outcome;
	for (const Target& target : targets)
	{
		if (!changesClip(target)) { continue; }
		const Note* note = applyTarget(clip, target);
		++outcome.edited;
		if (note == nullptr) { outcome.removed += target.remove && !target.create ? 1 : 0; continue; }
		outcome.added += target.create ? 1 : 0;
		outcome.notes.append(noteIdOf(note));
	}
	return outcome;
}

ControlResult setSteps(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	Grid grid;
	std::vector<Target> targets;
	if (clip == nullptr || !readGrid(args, &grid, &error)
		|| !resolveTargets(clip, args.value(QStringLiteral("edits")).toArray(), grid, &targets, &error))
	{
		return error;
	}
	const bool changes = std::any_of(targets.begin(), targets.end(), changesClip);
	// One checkpoint for the whole batch, and none for a batch that changes nothing: a step that undoes
	// nothing would sit over the edit behind it and spend the caller's next undo.
	checkpointNoteEdit(clip, changes);
	const StepOutcome done = applyTargets(clip, targets);
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.step_set"),
		{{QStringLiteral("step_ticks"), grid.stepTicks}, {QStringLiteral("edited"), done.edited},
			{QStringLiteral("added"), done.added}, {QStringLiteral("removed"), done.removed},
			{QStringLiteral("notes"), done.notes}},
		changes));
}

QJsonObject gridArgs()
{
	return {{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("step_ticks"), integerProperty(1, MaxStepTicks)},
		{QStringLiteral("root"), rootArgumentSchema()}, {QStringLiteral("scale"), stringProperty()}};
}

void registerStepGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.step_get_state");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("step_get_state");
	cmd.description = QStringLiteral("Read a MIDI clip as a melodic step grid: 'step_ticks' (default 12, a sixteenth) ticks a "
		"column, every note with its step, offset inside the step, key, velocity (0..200), chance (0..1, 1 always), ratchet "
		"(attacks, 1 one), length and - when a scale is set (scale.set) or 'root' / 'scale' are named - whether it is in the "
		"scale and its scale degree; and the occupied columns with the ids and pitches stacked in each. The notes are the "
		"clip's ordinary notes (note.list, roll.get_state): nothing is stored apart from them. Not mutating.");
	cmd.argsSchema = objectSchema(gridArgs(), {QStringLiteral("clip")});
	cmd.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("step_ticks"), integerProperty(1, MaxStepTicks)}, {QStringLiteral("steps"), integerProperty()},
		{QStringLiteral("clip_length"), integerProperty()}, {QStringLiteral("scale"), objectProperty()},
		{QStringLiteral("count"), integerProperty()}, {QStringLiteral("notes"), arrayProperty()},
		{QStringLiteral("columns"), arrayProperty()}});
	cmd.mutating = false;
	cmd.handler = getStepState;
	registry.registerCommand(cmd);
}

void registerStepSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.step_set");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("step_set");
	cmd.description = QStringLiteral("Edit a MIDI clip's melodic steps as ONE edit and ONE undo step. 'edits' is a list of "
		"{note | step + key, velocity?, chance?, ratchet?, length?, key?, remove?}: a cell is a step (column, counted from 0 in "
		"'step_ticks' ticks, default 12) and a key; an entry on an empty cell adds a note there (length one step, velocity "
		"100, chance 1, ratchet 1 unless given), an entry on a note or occupied cell changes the fields named and keeps the "
		"rest, 'remove' takes it out. velocity 0..200, chance 0..1 (1 always, 0 never), ratchet 1..16 (attacks evenly spaced "
		"across the note; 1 is one). With a scale set (or 'root' / 'scale' named) a pitch the call writes - a new cell, or "
		"'key' on a note - follows 'scale_mode': snap (default; nearest in-scale key, a tie down), strict (refused when out "
		"of scale) or off; a note left on its key keeps it, so out-of-scale notes stay editable. The whole batch is validated "
		"first: one bad entry refuses it and changes nothing. A batch that changes nothing records no undo step. Reversible "
		"through the ProjectJournal (one MidiClip checkpoint); control.undo restores the exact prior note list.");
	QJsonObject args = gridArgs();
	args.insert(QStringLiteral("edits"), arrayProperty());
	args.insert(QStringLiteral("scale_mode"),
		enumProperty({QStringLiteral("snap"), QStringLiteral("strict"), QStringLiteral("off")}));
	cmd.argsSchema = objectSchema(args, {QStringLiteral("clip"), QStringLiteral("edits")});
	cmd.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("step_ticks"), integerProperty(1, MaxStepTicks)}, {QStringLiteral("edited"), integerProperty()},
		{QStringLiteral("added"), integerProperty()}, {QStringLiteral("removed"), integerProperty()},
		{QStringLiteral("notes"), arrayProperty()}});
	cmd.mutating = true;
	cmd.handler = setSteps;
	registry.registerCommand(cmd);
}

} // namespace

void registerNoteStepCommands(ControlRegistry& registry)
{
	registerStepGetState(registry);
	registerStepSet(registry);
}

} // namespace lmms
