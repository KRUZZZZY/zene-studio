/*
 * ControlCommandsNoteStepFill.cpp - note.step_fill: a seeded fill that re-pitches the existing
 *                                   step columns of a MIDI clip, shape or random, in the active
 *                                   scale (Q050-294.3)
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
/*! 0.5.0 Q050-294.3: note.step_fill - the notes of each step column (every note whose position
 *  falls in one 'step_ticks' slot) re-pitched in the active root and scale, or in the 'root' and
 *  'scale' the call names (scale.set, custom scales too). 'kind' shape follows a contour (walk, rise, fall, arch, valley) across the
 *  columns, in 'low'..'high', moved by up to 'spread' degrees. 'kind' random picks one key per
 *  column from the same range and keeps each column with 'density' percent; the rest are removed.
 *  A chord keeps its shape in scale DEGREES: each note moves by the degrees it stood above its
 *  column's lowest note, so a triad stays a triad in the new key. The reply lists 'filled' (columns
 *  written), 'keys' (each written column's lowest key after the fill) and 'notes' (ids still written).
 *
 *  Repeatable everywhere: the draw is std::mt19937 (its output sequence is fixed by the standard)
 *  mapped to a range by a 64-bit multiply here - never std::uniform_int_distribution, whose
 *  mapping differs between libstdc++, libc++ and MSVC. One MidiClip journal checkpoint, and none
 *  when the fill changes nothing (checkpointNoteEdit).
 *  Runs on the control (GUI) thread only, like every note command. */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <vector>

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlCommandsNoteShared.h"
#include "ControlNoteShared.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlScaleShared.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control;
using namespace control::notes;

namespace
{

//! A sixteenth note in the engine's 192-ticks-per-bar time (the same default note.step_set uses).
constexpr int DefaultFillStepTicks = 12;
constexpr int MaxFillStepTicks = 3072;

//! A draw in 0..count-1 from @a rng, the same on every standard library.
int draw(std::mt19937& rng, int count)
{
	return static_cast<int>((static_cast<std::uint64_t>(rng()) * static_cast<std::uint64_t>(count)) >> 32);
}

//! Where contour @a shape sits at @a at (0..1 through the line), as 0 (lowest) .. 1 (highest).
double contourAt(const QString& shape, double at)
{
	if (shape == QLatin1String("rise")) { return at; }
	if (shape == QLatin1String("fall")) { return 1.0 - at; }
	if (shape == QLatin1String("arch")) { return 1.0 - std::abs(2.0 * at - 1.0); }
	return std::abs(2.0 * at - 1.0);  // valley
}

//! "walk": a random walk from near the middle of 0..@a top, each step 1..@a reach degrees up or down (never
//! standing still), turned back at the range's edges.
std::vector<int> walkIndices(int count, int top, int reach, std::mt19937& rng)
{
	std::vector<int> out;
	out.reserve(static_cast<std::size_t>(count));
	int at = std::clamp(top / 2 + draw(rng, 2 * reach + 1) - reach, 0, top);
	out.push_back(at);
	for (int i = 1; i < count; ++i)
	{
		const int size = 1 + draw(rng, reach);
		int next = at + (draw(rng, 2) == 0 ? -size : size);
		if (next < 0 || next > top) { next = at + (next < 0 ? size : -size); }  // turn back at the edge
		at = std::clamp(next, 0, top);
		out.push_back(at);
	}
	return out;
}

/*! @a count indices into a pool of scale keys, top = pool size - 1 (the pool sorted low to high).
 *  "walk": walkIndices, steps up to @a spread degrees. Any other shape: the contour's place in the
 *  range, moved by a seeded jitter of up to @a spread degrees either way. */
std::vector<int> lineIndices(const QString& shape, int count, int top, int spread, std::mt19937& rng)
{
	if (shape == QLatin1String("walk")) { return walkIndices(count, top, std::max(1, spread), rng); }
	std::vector<int> out;
	out.reserve(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		const double place = count > 1 ? static_cast<double>(i) / static_cast<double>(count - 1) : 0.5;
		const int centre = static_cast<int>(contourAt(shape, place) * top + 0.5);
		const int jitter = spread > 0 ? draw(rng, 2 * spread + 1) - spread : 0;
		out.push_back(std::clamp(centre + jitter, 0, top));
	}
	return out;
}

//! The seed a call names, else the project's own (note.random_seed_set); false with @a error when out of range.
bool fillSeed(const QJsonObject& args, std::uint32_t* seed, ControlResult* error)
{
	*seed = Engine::getSong()->midiSeed();
	if (!args.contains(QStringLiteral("seed"))) { return true; }
	const double value = args.value(QStringLiteral("seed")).toDouble(-1.0);
	if (value < 0.0 || value > static_cast<double>(kMaxSchemaInteger) || value != static_cast<double>(static_cast<qint64>(value)))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'seed' is %1: a seed is a whole number in 0..%2").arg(value).arg(kMaxSchemaInteger));
		return false;
	}
	*seed = static_cast<std::uint32_t>(value);
	return true;
}

//! Every key of the scale over the whole keyboard, low to high (a key's index in it is its scale degree).
std::vector<int> scaleKeysOf(const std::vector<int>& classes)
{
	std::vector<int> out;
	for (int key = 0; key < NumKeys; ++key)
	{
		if (std::find(classes.begin(), classes.end(), key % 12) != classes.end()) { out.push_back(key); }
	}
	return out;
}

//! The scale degree @a key stands on: how many scale keys lie below it (an off-scale key counts to the next one up).
int degreeOf(const std::vector<int>& keys, int key)
{
	return static_cast<int>(std::lower_bound(keys.begin(), keys.end(), key) - keys.begin());
}

//! One planned note edit: the note, and the key it takes (or the note is removed).
struct Repitch
{
	Note* note;
	int key;
};

//! The arguments of note.step_fill once read and checked: the kind, the contour, the step size, the seed, the
//! scale keys (a key's index in them is its scale degree) and the 'low'..'high' pool a fill writes into.
struct FillArgs
{
	QString kind;
	QString contour;
	int stepTicks = DefaultFillStepTicks;
	std::uint32_t seed = 0;
	std::vector<int> keys;
	std::vector<int> pool;
	int spread = 2;
	int density = 100;
};

//! The plan of one fill: the notes to re-pitch, the notes to remove, and the written columns in step order.
struct FillPlan
{
	std::vector<Repitch> repitches;
	std::vector<Note*> removals;
	std::vector<std::vector<Repitch>> columns;  // one entry per kept column, in step order
	int stepCount = 0;                          // every step column the clip has, kept or not
};

//! The keys of @a keys from @a low to @a high, inclusive, in order.
std::vector<int> poolOf(const std::vector<int>& keys, int low, int high)
{
	std::vector<int> pool;
	for (int key : keys)
	{
		if (key >= low && key <= high) { pool.push_back(key); }
	}
	return pool;
}

//! Reads and checks note.step_fill's arguments in order (kind, step_ticks, scale, pool, seed); false with @a error
//! at the first one that is wrong.
bool readFillArgs(const QJsonObject& args, FillArgs* out, ControlResult* error)
{
	out->kind = args.value(QStringLiteral("kind")).toString(QStringLiteral("shape"));
	if (out->kind != QLatin1String("shape") && out->kind != QLatin1String("random"))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'kind' is shape or random"));
		return false;
	}
	out->stepTicks = args.value(QStringLiteral("step_ticks")).toInt(DefaultFillStepTicks);
	if (out->stepTicks < 1 || out->stepTicks > MaxFillStepTicks)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'step_ticks' must be in [1, %1]").arg(MaxFillStepTicks));
		return false;
	}

	ResolvedScale resolved;
	if (!resolveScale(args, &resolved, error)) { return false; }
	if (resolved.classes.empty())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("no scale is set: a fill is written in the active scale (scale.set, or the step grid's Scale box)"));
		return false;
	}
	out->keys = scaleKeysOf(resolved.classes);
	const int low = std::max(0, args.value(QStringLiteral("low")).toInt(48));
	const int high = std::min(NumKeys - 1, args.value(QStringLiteral("high")).toInt(72));
	out->pool = poolOf(out->keys, low, high);
	if (out->pool.size() < 2)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("keys %1..%2 hold %3 key(s) of the scale: a fill needs at least two").arg(low).arg(high).arg(out->pool.size()));
		return false;
	}
	if (!fillSeed(args, &out->seed, error)) { return false; }

	out->contour = args.value(QStringLiteral("contour")).toString(QStringLiteral("walk"));
	out->spread = args.value(QStringLiteral("spread")).toInt(2);
	out->density = std::clamp(args.value(QStringLiteral("density")).toInt(100), 1, 100);
	return true;
}

//! The step columns of @a clip: every note whose position falls in the same 'step_ticks' slot, in time order.
std::map<int, std::vector<Note*>> stepColumnsOf(MidiClip* clip, int stepTicks)
{
	std::map<int, std::vector<Note*>> columns;
	for (Note* note : clip->notes()) { columns[note->pos().getTicks() / stepTicks].push_back(note); }
	return columns;
}

//! The plan of a fill of @a columns (each one's notes sorted by key) from @a in, drawing from @a rng.
FillPlan planFill(const FillArgs& in, std::map<int, std::vector<Note*>>& columns, std::mt19937& rng)
{
	FillPlan plan;
	plan.stepCount = static_cast<int>(columns.size());
	const int top = static_cast<int>(in.pool.size()) - 1;
	std::vector<int> line;
	if (in.kind == QLatin1String("shape"))
	{
		line = lineIndices(in.contour, plan.stepCount, top, in.spread, rng);
	}

	std::size_t ordinal = 0;
	for (auto& column : columns)
	{
		std::vector<Note*>& notes = column.second;
		std::stable_sort(notes.begin(), notes.end(), [](const Note* a, const Note* b) { return a->key() < b->key(); });
		int at = 0;
		bool keep = true;
		if (in.kind == QLatin1String("shape")) { at = line[ordinal]; }
		else
		{
			keep = draw(rng, 100) < in.density;
			at = draw(rng, top + 1);
		}
		++ordinal;
		if (!keep)
		{
			plan.removals.insert(plan.removals.end(), notes.begin(), notes.end());
			continue;
		}
		const int base = degreeOf(in.keys, notes.front()->key());  // the column's lowest note anchors the chord
		for (Note* note : notes)
		{
			const int index = std::clamp(at + degreeOf(in.keys, note->key()) - base, 0, top);
			plan.repitches.push_back({note, in.pool[static_cast<std::size_t>(index)]});
		}
		plan.columns.emplace_back(plan.repitches.end() - static_cast<std::ptrdiff_t>(notes.size()), plan.repitches.end());
	}
	return plan;
}

//! How many of @a plan's notes change key.
int countPitched(const FillPlan& plan)
{
	int pitched = 0;
	for (const Repitch& edit : plan.repitches) { pitched += edit.key != edit.note->key() ? 1 : 0; }
	return pitched;
}

//! Writes @a plan into @a clip: the new keys, then the removals.
void applyFill(MidiClip* clip, const FillPlan& plan)
{
	for (const Repitch& edit : plan.repitches) { edit.note->setKey(edit.key); }
	for (Note* note : plan.removals) { clip->removeNote(note); }
}

//! The note.step_fill reply for @a plan. The written columns in step order: each one's lowest key after the fill,
//! and its note ids in pitch order.
QJsonObject fillReply(const FillArgs& in, int pitched, const FillPlan& plan)
{
	QJsonArray lowestKeys;
	QJsonArray noteIds;
	for (std::vector<Repitch> column : plan.columns)
	{
		std::stable_sort(column.begin(), column.end(), [](const Repitch& a, const Repitch& b) { return a.key < b.key; });
		lowestKeys.append(column.front().key);
		for (const Repitch& edit : column) { noteIds.append(control::noteIdOf(edit.note)); }
	}
	return QJsonObject{{QStringLiteral("kind"), in.kind}, {QStringLiteral("seed"), static_cast<double>(in.seed)},
		{QStringLiteral("step_ticks"), in.stepTicks}, {QStringLiteral("columns"), plan.stepCount},
		{QStringLiteral("pitched"), pitched}, {QStringLiteral("removed"), static_cast<int>(plan.removals.size())},
		{QStringLiteral("filled"), static_cast<int>(plan.columns.size())},
		{QStringLiteral("keys"), lowestKeys}, {QStringLiteral("notes"), noteIds}};
}

ControlResult stepFill(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }

	FillArgs in;
	if (!readFillArgs(args, &in, &error)) { return error; }
	std::map<int, std::vector<Note*>> columns = stepColumnsOf(clip, in.stepTicks);
	if (columns.empty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the clip has no notes: a fill re-pitches existing steps"));
	}

	std::mt19937 rng(in.seed);
	const FillPlan plan = planFill(in, columns, rng);
	const int pitched = countPitched(plan);
	const bool changes = pitched > 0 || !plan.removals.empty();
	// One checkpoint for the whole fill, and none for a fill that changes nothing (see checkpointNoteEdit).
	checkpointNoteEdit(clip, changes);
	if (changes) { applyFill(clip, plan); }
	QJsonObject result = fillReply(in, pitched, plan);
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.step_fill"), result, changes));
}

} // namespace

void registerNoteStepFillCommands(ControlRegistry& registry)
{
	const QJsonObject args{{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("kind"), enumProperty({QStringLiteral("shape"), QStringLiteral("random")})},
		{QStringLiteral("contour"), enumProperty({QStringLiteral("walk"), QStringLiteral("rise"), QStringLiteral("fall"),
			QStringLiteral("arch"), QStringLiteral("valley")})},
		{QStringLiteral("step_ticks"), integerProperty(1, MaxFillStepTicks)},
		{QStringLiteral("seed"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("low"), integerProperty(0, 127)}, {QStringLiteral("high"), integerProperty(0, 127)},
		{QStringLiteral("spread"), integerProperty(0, 12)}, {QStringLiteral("density"), integerProperty(1, 100)},
		{QStringLiteral("root"), rootArgumentSchema()}, {QStringLiteral("scale"), stringProperty()}};
	control::defineCommandWithSchema(registry, QStringLiteral("note.step_fill"),
		QStringLiteral("Fill a clip's existing step columns (every note in one 'step_ticks' slot, 12 = a sixteenth) with new keys "
			"of the active scale, or of the 'root' and 'scale' the call names (scale.set; custom scales too), in 'low'..'high' "
			"(48..72 when left out). 'kind' shape moves the "
			"columns along a 'contour' (walk, rise, fall, arch or valley, moved up to 'spread' degrees either way). 'kind' random "
			"picks a key per column and keeps each column with 'density' percent (100 when left out); the others are removed. A "
			"chord keeps its shape in scale degrees. The same 'seed' (the project's seed when left out) and the same clip give the "
			"same notes. Refused when no scale is set. The reply lists 'filled' (columns written), 'keys' (each one's lowest key "
			"after the fill) and 'notes' (the ids still written). One undo step; none when the fill changes nothing."),
		objectSchema(args, QJsonArray{QStringLiteral("clip")}), true, stepFill);
}

} // namespace lmms
