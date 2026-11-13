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
 *  falls in one 'step_ticks' slot) re-pitched in the active root and scale (scale.set, custom
 *  scales too). 'kind' shape follows a contour (walk, rise, fall, arch, valley) across the
 *  columns, in 'low'..'high', moved by up to 'spread' degrees. 'kind' random picks one key per
 *  column from the same range and keeps each column with 'density' percent; the rest are removed.
 *  A chord keeps its shape in scale DEGREES: each note moves by the degrees it stood above its
 *  column's lowest note, so a triad stays a triad in the new key.
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

ControlResult stepFill(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }

	const QString kind = args.value(QStringLiteral("kind")).toString(QStringLiteral("shape"));
	if (kind != QLatin1String("shape") && kind != QLatin1String("random"))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'kind' is shape or random"));
	}
	const int stepTicks = args.value(QStringLiteral("step_ticks")).toInt(DefaultFillStepTicks);
	if (stepTicks < 1 || stepTicks > MaxFillStepTicks)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'step_ticks' must be in [1, %1]").arg(MaxFillStepTicks));
	}

	const ScaleContext& context = scaleContext();
	const std::vector<int> classes = scalePitchClasses(context.root, scaleDegreesOf(context.scale));
	if (classes.empty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("no scale is set: a fill is written in the active scale (scale.set, or the note editor's scale menu)"));
	}
	const std::vector<int> keys = scaleKeysOf(classes);
	const int low = std::max(0, args.value(QStringLiteral("low")).toInt(48));
	const int high = std::min(NumKeys - 1, args.value(QStringLiteral("high")).toInt(72));
	std::vector<int> pool;
	for (int key : keys)
	{
		if (key >= low && key <= high) { pool.push_back(key); }
	}
	if (pool.size() < 2)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("keys %1..%2 hold %3 key(s) of the scale: a fill needs at least two").arg(low).arg(high).arg(pool.size()));
	}
	std::uint32_t seed = 0;
	if (!fillSeed(args, &seed, &error)) { return error; }

	// The step columns: every note whose position falls in the same 'step_ticks' slot, in time order.
	std::map<int, std::vector<Note*>> columns;
	for (Note* note : clip->notes()) { columns[note->pos().getTicks() / stepTicks].push_back(note); }
	if (columns.empty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the clip has no notes: a fill re-pitches existing steps"));
	}

	std::mt19937 rng(seed);
	const int top = static_cast<int>(pool.size()) - 1;
	const int density = std::clamp(args.value(QStringLiteral("density")).toInt(100), 1, 100);
	std::vector<int> line;
	if (kind == QLatin1String("shape"))
	{
		line = lineIndices(args.value(QStringLiteral("contour")).toString(QStringLiteral("walk")), static_cast<int>(columns.size()),
			top, args.value(QStringLiteral("spread")).toInt(2), rng);
	}

	std::vector<Repitch> repitches;
	std::vector<Note*> removals;
	std::size_t ordinal = 0;
	for (auto& column : columns)
	{
		std::vector<Note*>& notes = column.second;
		std::stable_sort(notes.begin(), notes.end(), [](const Note* a, const Note* b) { return a->key() < b->key(); });
		int at = 0;
		bool keep = true;
		if (kind == QLatin1String("shape")) { at = line[ordinal]; }
		else
		{
			keep = draw(rng, 100) < density;
			at = draw(rng, top + 1);
		}
		++ordinal;
		if (!keep)
		{
			removals.insert(removals.end(), notes.begin(), notes.end());
			continue;
		}
		const int base = degreeOf(keys, notes.front()->key());  // the column's lowest note anchors the chord
		for (Note* note : notes)
		{
			const int index = std::clamp(at + degreeOf(keys, note->key()) - base, 0, top);
			repitches.push_back({note, pool[static_cast<std::size_t>(index)]});
		}
	}

	int pitched = 0;
	for (const Repitch& edit : repitches) { pitched += edit.key != edit.note->key() ? 1 : 0; }
	const bool changes = pitched > 0 || !removals.empty();
	// One checkpoint for the whole fill, and none for a fill that changes nothing (see checkpointNoteEdit).
	checkpointNoteEdit(clip, changes);
	if (changes)
	{
		for (const Repitch& edit : repitches) { edit.note->setKey(edit.key); }
		for (Note* note : removals) { clip->removeNote(note); }
	}
	QJsonObject result{{QStringLiteral("kind"), kind}, {QStringLiteral("seed"), static_cast<double>(seed)},
		{QStringLiteral("step_ticks"), stepTicks}, {QStringLiteral("columns"), static_cast<int>(columns.size())},
		{QStringLiteral("pitched"), pitched}, {QStringLiteral("removed"), static_cast<int>(removals.size())}};
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
		{QStringLiteral("spread"), integerProperty(0, 12)}, {QStringLiteral("density"), integerProperty(1, 100)}};
	control::defineCommandWithSchema(registry, QStringLiteral("note.step_fill"),
		QStringLiteral("Fill a clip's existing step columns (every note in one 'step_ticks' slot, 12 = a sixteenth) with new keys "
			"of the active scale (scale.set; custom scales too) in 'low'..'high' (48..72 when left out). 'kind' shape moves the "
			"columns along a 'contour' (walk, rise, fall, arch or valley, moved up to 'spread' degrees either way). 'kind' random "
			"picks a key per column and keeps each column with 'density' percent (100 when left out); the others are removed. A "
			"chord keeps its shape in scale degrees. The same 'seed' (the project's seed when left out) and the same clip give the "
			"same notes. Refused when no scale is set. One undo step; none when the fill changes nothing."),
		objectSchema(args, QJsonArray{QStringLiteral("clip")}), true, stepFill);
}

} // namespace lmms
