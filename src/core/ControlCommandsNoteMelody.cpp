/*
 * ControlCommandsNoteMelody.cpp - note.generate_melody: a seeded melodic contour in the active scale
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
/*! 0.5.0 Q050-221.1 (L07): note.generate_melody - a melodic line of ordinary notes, every key in
 *  the active root and scale (scale.set / scale.root_set, custom scales included), shaped by a
 *  contour and drawn from a seed. 'target' new writes 'count' notes 'step' ticks apart from
 *  'start'; 'target' existing re-pitches the notes in 'scope' and keeps their rhythm (so a
 *  note.generate rhythm can be given a line). One MidiClip journal checkpoint.
 *
 *  Repeatable everywhere: the draw is std::mt19937 (its output sequence is fixed by the
 *  standard) mapped to a range by a 64-bit multiply here - never std::uniform_int_distribution,
 *  whose mapping differs between libstdc++, libc++ and MSVC. No generative model: a contour
 *  function plus seeded jitter over the scale's keys in 'low'..'high'.
 *  Runs on the control (GUI) thread only, like every note command. */

#include <algorithm>
#include <cstdint>
#include <cmath>
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

//! "walk": a random walk from near the middle of 0..@a top, each step 1..@a reach degrees up or down (never standing
//! still), turned back at the range's edges.
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

/*! @a count indices into a pool of @a poolSize scale keys (sorted low to high).
 *  "walk": walkIndices, steps up to @a spread degrees. Any other shape: the contour's place in the
 *  range, moved by a seeded jitter of up to @a spread degrees either way. */
std::vector<int> melodyIndices(const QString& shape, int count, int poolSize, int spread, std::mt19937& rng)
{
	const int top = poolSize - 1;
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
bool melodySeed(const QJsonObject& args, std::uint32_t* seed, ControlResult* error)
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

//! The scale's keys in 'low'..'high', low to high; false with @a error when there are fewer than two.
bool melodyPool(const QJsonObject& args, std::vector<int>* pool, ControlResult* error)
{
	const ScaleContext& context = scaleContext();
	const std::vector<int> classes = scalePitchClasses(context.root, scaleDegreesOf(context.scale));
	if (classes.empty())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("no scale is set: a melody is generated in the active scale (scale.set, or the note editor's scale menu)"));
		return false;
	}
	const int low = args.value(QStringLiteral("low")).toInt(48);
	const int high = args.value(QStringLiteral("high")).toInt(72);
	for (int key = std::max(0, low); key <= std::min(NumKeys - 1, high); ++key)
	{
		if (std::find(classes.begin(), classes.end(), key % 12) != classes.end()) { pool->push_back(key); }
	}
	if (pool->size() < 2)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("keys %1..%2 hold %3 key(s) of the scale: a melody needs at least two").arg(low).arg(high).arg(pool->size()));
		return false;
	}
	return true;
}

//! 'target' existing: the notes in 'scope', in time order (a chord low to high), re-pitched along the line.
ControlResult repitch(const QJsonObject& args, const ClipRef& ref, MidiClip* clip, const std::vector<int>& pool, std::mt19937& rng,
	QJsonObject result)
{
	QVector<Note*> notes = scoped(clip, args.value(QStringLiteral("scope")).toString());
	if (notes.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no notes in that scope to re-pitch")); }
	std::stable_sort(notes.begin(), notes.end(), [](const Note* a, const Note* b) {
		return a->pos() != b->pos() ? a->pos() < b->pos() : a->key() < b->key();
	});
	const int count = static_cast<int>(notes.size());
	const std::vector<int> line = melodyIndices(args.value(QStringLiteral("contour")).toString(QStringLiteral("walk")),
		count, static_cast<int>(pool.size()), args.value(QStringLiteral("spread")).toInt(2), rng);
	const auto keyAt = [&pool, &line](int i) { return pool[static_cast<std::size_t>(line[static_cast<std::size_t>(i)])]; };
	bool changes = false;
	for (int i = 0; i < count; ++i) { changes = changes || notes[i]->key() != keyAt(i); }
	QJsonArray keys;
	for (int index : line) { keys.append(pool[static_cast<std::size_t>(index)]); }
	result.insert(QStringLiteral("keys"), keys);
	result.insert(QStringLiteral("pitched"), changes ? count : 0);
	if (!changes) { return ControlResult::success(finish(ref, clip, QStringLiteral("note.generate_melody"), result, false)); }
	clip->addJournalCheckPoint();
	for (int i = 0; i < count; ++i) { notes[i]->setKey(keyAt(i)); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.generate_melody"), result));
}

ControlResult generateMelody(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	std::vector<int> pool;
	if (!melodyPool(args, &pool, &error)) { return error; }
	std::uint32_t seed = 0;
	if (!melodySeed(args, &seed, &error)) { return error; }
	std::mt19937 rng(seed);
	QJsonObject result{{QStringLiteral("seed"), static_cast<double>(seed)},
		{QStringLiteral("contour"), args.value(QStringLiteral("contour")).toString(QStringLiteral("walk"))}};
	if (args.value(QStringLiteral("target")).toString() == QLatin1String("existing")) { return repitch(args, ref, clip, pool, rng, result); }

	const int count = args.value(QStringLiteral("count")).toInt(16);
	const tick_t step = args.value(QStringLiteral("step")).toInt(24);
	const tick_t length = args.value(QStringLiteral("length")).toInt(step);
	const qint64 start = args.value(QStringLiteral("start")).toInt(0);
	if (start + static_cast<qint64>(count - 1) * step + length > static_cast<qint64>(MaxSongLength))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 notes %2 ticks apart from tick %3 would end past the song limit of %4")
				.arg(count).arg(step).arg(start).arg(static_cast<qint64>(MaxSongLength)));
	}
	const std::vector<int> line = melodyIndices(args.value(QStringLiteral("contour")).toString(QStringLiteral("walk")),
		count, static_cast<int>(pool.size()), args.value(QStringLiteral("spread")).toInt(2), rng);
	Note note(TimePos(length), TimePos(0), DefaultKey, static_cast<volume_t>(args.value(QStringLiteral("velocity")).toInt(DefaultVolume)));
	clip->addJournalCheckPoint();
	QJsonArray keys;
	for (int i = 0; i < count; ++i)
	{
		note.setPos(TimePos(static_cast<tick_t>(start + static_cast<qint64>(i) * step)));
		note.setKey(pool[static_cast<std::size_t>(line[static_cast<std::size_t>(i)])]);
		clip->addNote(note, false);
		keys.append(note.key());
	}
	result.insert(QStringLiteral("keys"), keys);
	result.insert(QStringLiteral("added"), count);
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.generate_melody"), result));
}

} // namespace

void registerNoteMelodyCommands(ControlRegistry& registry)
{
	const QJsonObject args{{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("contour"), enumProperty({QStringLiteral("walk"), QStringLiteral("rise"), QStringLiteral("fall"),
			QStringLiteral("arch"), QStringLiteral("valley")})},
		{QStringLiteral("target"), enumProperty({QStringLiteral("new"), QStringLiteral("existing")})},
		{QStringLiteral("scope"), enumProperty({QStringLiteral("clip"), QStringLiteral("selection")})},
		{QStringLiteral("seed"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("count"), integerProperty(1, 256)}, {QStringLiteral("step"), integerProperty(1, 3072)},
		{QStringLiteral("length"), integerProperty(1, 3072)}, {QStringLiteral("start"), integerProperty(0, static_cast<int>(MaxSongLength))},
		{QStringLiteral("low"), integerProperty(0, 127)}, {QStringLiteral("high"), integerProperty(0, 127)},
		{QStringLiteral("spread"), integerProperty(0, 12)}, {QStringLiteral("velocity"), integerProperty(0, 200)}};
	control::defineCommandWithSchema(registry, QStringLiteral("note.generate_melody"),
		QStringLiteral("Generate a melodic line in the active scale (scale.set; custom scales too): every key is one of the scale's "
			"keys in 'low'..'high' (48..72 when left out). 'contour' walk (a random walk, steps of 1..'spread' degrees), rise, "
			"fall, arch or valley (the shape, moved up to 'spread' degrees either way; 2 when left out, 0 = the bare shape). "
			"'target' new writes 'count' notes (16) 'step' ticks (24) apart from 'start', 'length' ticks long, at 'velocity'; "
			"existing re-pitches the notes in 'scope' and keeps their rhythm. The same 'seed' (the project's seed when left out) "
			"and the same clip give the same notes. Refused when no scale is set. One undo step."),
		objectSchema(args, QJsonArray{QStringLiteral("clip")}), true, generateMelody);
}

} // namespace lmms
