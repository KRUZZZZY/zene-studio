/*
 * ControlCommandsNoteTools.cpp - invert, arpeggiate, flam, transpose in key, repeat, generate
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
/*! 0.5.0 L07 (W3): note tools - each one MidiClip journal checkpoint, on the notes in 'scope'
 *  ('selection' or the whole clip):
 *  - note.invert (ED-094): pitches mirrored around 'pivot' (the middle of their range by default);
 *  - note.arpeggiate (ED-099): each chord (notes starting together) played as a run, 'rate' ticks a
 *    step, 'pattern' up / down / updown, over the chord's length;
 *  - note.flam (ED-100): a grace note 'offset' ticks before each note, at 'velocity' of it;
 *  - note.transpose_in_key (ED-103): 'steps' scale degrees up or down in the scale set (scale.set);
 *  - note.repeat (ED-144): each note repeated 'count' times every 'interval' ticks;
 *  - note.generate (ED-101): a Euclidean rhythm ('pulses' spread over 'steps') or random notes on one
 *    key, 'step' ticks a step, into the clip. */

#include <algorithm>
#include <map>
#include <random>

#include "ControlCommandsNoteShared.h"
#include "ControlCommandDef.h"
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

//! The clip and the notes a call names, or a failure in @a error.
MidiClip* scopedNotes(const QJsonObject& args, ClipRef* ref, QVector<Note*>* notes, ControlResult* error)
{
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), ref, error);
	if (clip == nullptr) { return nullptr; }
	*notes = scoped(clip, args.value(QStringLiteral("scope")).toString());
	if (notes->isEmpty()) { *error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no notes in that scope")); return nullptr; }
	return clip;
}

ControlResult invert(const QJsonObject& args)
{
	ClipRef ref;
	QVector<Note*> notes;
	ControlResult error;
	MidiClip* clip = scopedNotes(args, &ref, &notes, &error);
	if (clip == nullptr) { return error; }
	int low = 127;
	int high = 0;
	for (const Note* note : notes) { low = std::min(low, note->key()); high = std::max(high, note->key()); }
	const int twice = args.contains(QStringLiteral("pivot")) ? 2 * args.value(QStringLiteral("pivot")).toInt() : low + high;
	clip->addJournalCheckPoint();
	for (Note* note : notes) { note->setKey(std::clamp(twice - note->key(), 0, NumKeys - 1)); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.invert"), {{QStringLiteral("inverted"), notes.size()}}));
}

//! The chord's keys in the order @a pattern plays them.
std::vector<int> runOrder(std::vector<int> keys, const QString& pattern)
{
	std::sort(keys.begin(), keys.end());
	if (pattern == QLatin1String("down")) { std::reverse(keys.begin(), keys.end()); }
	if (pattern == QLatin1String("updown") && keys.size() > 2) { keys.insert(keys.end(), keys.rbegin() + 1, keys.rend() - 1); }
	return keys;
}

ControlResult arpeggiate(const QJsonObject& args)
{
	ClipRef ref;
	QVector<Note*> notes;
	ControlResult error;
	MidiClip* clip = scopedNotes(args, &ref, &notes, &error);
	if (clip == nullptr) { return error; }
	const tick_t rate = std::max(1, args.value(QStringLiteral("rate")).toInt(12));
	std::map<tick_t, std::vector<Note*>> chords;
	for (Note* note : notes) { chords[note->pos().getTicks()].push_back(note); }
	clip->addJournalCheckPoint();
	int made = 0;
	for (auto& [start, chord] : chords)
	{
		tick_t length = 0;
		std::vector<int> keys;
		for (const Note* note : chord) { length = std::max<tick_t>(length, note->length().getTicks()); keys.push_back(note->key()); }
		Note step = *chord.front();
		for (Note* note : chord) { clip->removeNote(note); }
		const std::vector<int> order = runOrder(keys, args.value(QStringLiteral("pattern")).toString(QStringLiteral("up")));
		for (tick_t at = 0, i = 0; at < length; at += rate, ++i)
		{
			step.setKey(order[static_cast<std::size_t>(i) % order.size()]);
			step.setPos(TimePos(start + at));
			step.setLength(TimePos(std::min(rate, length - at)));
			clip->addNote(step, false);
			++made;
		}
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.arpeggiate"), {{QStringLiteral("notes"), made}}));
}

ControlResult flam(const QJsonObject& args)
{
	ClipRef ref;
	QVector<Note*> notes;
	ControlResult error;
	MidiClip* clip = scopedNotes(args, &ref, &notes, &error);
	if (clip == nullptr) { return error; }
	const tick_t offset = std::max(1, args.value(QStringLiteral("offset")).toInt(3));
	const double ratio = std::clamp(args.value(QStringLiteral("velocity")).toDouble(0.6), 0.05, 1.0);
	clip->addJournalCheckPoint();
	int made = 0;
	for (const Note* note : notes)
	{
		if (note->pos().getTicks() < offset) { continue; }  // no room before it
		Note grace = *note;
		grace.setPos(TimePos(note->pos().getTicks() - offset));
		grace.setLength(TimePos(offset));
		grace.setVolume(static_cast<volume_t>(std::lround(note->getVolume() * ratio)));
		clip->addNote(grace, false);
		++made;
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.flam"), {{QStringLiteral("added"), made}}));
}

//! @a key moved @a steps degrees within @a classes (the scale's pitch classes, sorted).
int stepInScale(int key, int steps, const std::vector<int>& classes)
{
	int moved = key;
	const int direction = steps > 0 ? 1 : -1;
	for (int done = 0; done != steps;)
	{
		moved += direction;
		if (moved < 0 || moved >= NumKeys) { return key; }
		if (std::find(classes.begin(), classes.end(), moved % 12) != classes.end()) { done += direction; }
	}
	return moved;
}

ControlResult transposeInKey(const QJsonObject& args)
{
	const ScaleContext& context = scaleContext();
	const std::vector<int> classes = scalePitchClasses(context.root, scaleDegreesOf(context.scale));
	if (classes.empty()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no scale is set (scale.set)")); }
	ClipRef ref;
	QVector<Note*> notes;
	ControlResult error;
	MidiClip* clip = scopedNotes(args, &ref, &notes, &error);
	if (clip == nullptr) { return error; }
	const int steps = args.value(QStringLiteral("steps")).toInt(1);
	clip->addJournalCheckPoint();
	for (Note* note : notes) { note->setKey(stepInScale(note->key(), steps, classes)); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.transpose_in_key"), {{QStringLiteral("moved"), notes.size()}}));
}

ControlResult repeatNotes(const QJsonObject& args)
{
	ClipRef ref;
	QVector<Note*> notes;
	ControlResult error;
	MidiClip* clip = scopedNotes(args, &ref, &notes, &error);
	if (clip == nullptr) { return error; }
	const int count = std::clamp(args.value(QStringLiteral("count")).toInt(3), 1, 64);
	const tick_t interval = std::max(1, args.value(QStringLiteral("interval")).toInt(48));
	clip->addJournalCheckPoint();
	for (const Note* note : notes)
	{
		Note copy = *note;
		for (int i = 1; i <= count; ++i)
		{
			copy.setPos(TimePos(note->pos().getTicks() + i * interval));
			clip->addNote(copy, false);
		}
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.repeat"), {{QStringLiteral("added"), notes.size() * count}}));
}

//! ED-028: the notes in scope moved 'amount' ticks later (negative: earlier), wrapped inside the clip's length.
ControlResult rotate(const QJsonObject& args)
{
	ClipRef ref;
	QVector<Note*> notes;
	ControlResult error;
	MidiClip* clip = scopedNotes(args, &ref, &notes, &error);
	if (clip == nullptr) { return error; }
	// The span the clip plays: a clip trimmed on the left plays only up to length - startTimeOffset, so a wrap past
	// it would park notes where they never sound (review W3-batchB #3).
	const tick_t length = std::max<tick_t>(1, clip->length().getTicks() - clip->startTimeOffset().getTicks());
	const tick_t amount = args.value(QStringLiteral("amount")).toInt();
	if (amount % length == 0) { return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("rotated"), 0}}); }
	clip->addJournalCheckPoint();
	for (Note* note : notes) { note->setPos(TimePos(((note->pos().getTicks() + amount) % length + length) % length)); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.rotate"), {{QStringLiteral("rotated"), notes.size()}}));
}

//! Which of @a steps steps carry a pulse: Bjorklund's grouping (E(3,8) is x..x..x.), rotated by @a rotate.
std::vector<bool> euclid(int steps, int pulses, int rotate)
{
	using Group = std::vector<bool>;
	std::vector<Group> front(static_cast<std::size_t>(pulses), Group{true});
	std::vector<Group> back(static_cast<std::size_t>(steps - pulses), Group{false});
	while (back.size() > 1 && !front.empty())
	{
		const std::size_t paired = std::min(front.size(), back.size());
		std::vector<Group> joined;
		for (std::size_t i = 0; i < paired; ++i) { joined.push_back(front[i]); joined.back().insert(joined.back().end(), back[i].begin(), back[i].end()); }
		std::vector<Group> rest = front.size() > paired ? std::vector<Group>(front.begin() + paired, front.end())
			: std::vector<Group>(back.begin() + paired, back.end());
		front = std::move(joined);
		back = std::move(rest);
	}
	Group flat;
	for (const auto* part : {&front, &back}) { for (const Group& group : *part) { flat.insert(flat.end(), group.begin(), group.end()); } }
	std::vector<bool> out(static_cast<std::size_t>(steps), false);
	for (int i = 0; i < steps; ++i) { out[static_cast<std::size_t>(((i + rotate) % steps + steps) % steps)] = flat[static_cast<std::size_t>(i)]; }
	return out;
}

ControlResult generate(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const int steps = std::clamp(args.value(QStringLiteral("steps")).toInt(16), 1, 128);
	const int pulses = std::clamp(args.value(QStringLiteral("pulses")).toInt(5), 0, steps);
	const tick_t step = std::max(1, args.value(QStringLiteral("step")).toInt(12));
	const bool random = args.value(QStringLiteral("kind")).toString() == QLatin1String("random");
	std::mt19937 rng(static_cast<unsigned>(args.value(QStringLiteral("seed")).toInt(1)));
	const std::vector<bool> hits = euclid(steps, pulses, args.value(QStringLiteral("rotate")).toInt(0));
	Note note(TimePos(step), TimePos(0), args.value(QStringLiteral("key")).toInt(DefaultKey),
		static_cast<volume_t>(args.value(QStringLiteral("velocity")).toInt(DefaultVolume)));
	clip->addJournalCheckPoint();
	int made = 0;
	for (int i = 0; i < steps; ++i)
	{
		const bool on = random ? std::uniform_int_distribution<int>(0, steps - 1)(rng) < pulses : hits[static_cast<std::size_t>(i)];
		if (!on) { continue; }
		note.setPos(TimePos(i * step));
		clip->addNote(note, false);
		++made;
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.generate"), {{QStringLiteral("added"), made}}));
}

void add(ControlRegistry& registry, const char* verb, const QString& description, QJsonObject args, ControlResult (*handler)(const QJsonObject&))
{
	args.insert(QStringLiteral("clip"), stringProperty());
	control::defineCommandWithSchema(registry, QStringLiteral("note.") + QLatin1String(verb), description,
		objectSchema(args, QJsonArray{QStringLiteral("clip")}), true, handler);
}

} // namespace

void registerNoteToolCommands(ControlRegistry& registry)
{
	const QJsonObject scope{{QStringLiteral("scope"), enumProperty({QStringLiteral("clip"), QStringLiteral("selection")})}};
	auto with = [scope](std::initializer_list<std::pair<QString, QJsonValue>> more) {
		QJsonObject out = scope;
		for (const auto& [key, value] : more) { out.insert(key, value); }
		return out;
	};
	add(registry, "invert", QStringLiteral("Mirror the pitches of the notes in 'scope' around 'pivot' (a key; the middle of their range "
		"when left out). One undo step."), with({{QStringLiteral("pivot"), integerProperty(0, 127)}}), invert);
	add(registry, "arpeggiate", QStringLiteral("Play each chord in 'scope' (notes starting together) as a run: 'rate' ticks a step (12 = "
		"a 16th), 'pattern' up, down or updown, over the chord's length. One undo step."),
		with({{QStringLiteral("rate"), integerProperty(1, 3072)}, {QStringLiteral("pattern"),
			enumProperty({QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("updown")})}}), arpeggiate);
	add(registry, "flam", QStringLiteral("Put a grace note 'offset' ticks (3 when left out) before each note in 'scope', at 'velocity' "
		"(0..1, 0.6 when left out) of it - a flam. One undo step."),
		with({{QStringLiteral("offset"), integerProperty(1, 96)}, {QStringLiteral("velocity"), numberProperty()}}), flam);
	add(registry, "transpose_in_key", QStringLiteral("Move the notes in 'scope' 'steps' scale degrees (negative: down) within the scale "
		"scale.set chose, so they stay in key. One undo step."), with({{QStringLiteral("steps"), integerProperty(-24, 24)}}), transposeInKey);
	add(registry, "rotate", QStringLiteral("Move the notes in 'scope' 'amount' ticks later (negative: earlier), wrapping round inside "
		"the clip's length, so the pattern turns without growing. One undo step."),
		with({{QStringLiteral("amount"), integerProperty(-192 * 256, 192 * 256)}}), rotate);
	add(registry, "repeat", QStringLiteral("Repeat each note in 'scope' 'count' times (1..64), every 'interval' ticks - a note repeater. "
		"One undo step."), with({{QStringLiteral("count"), integerProperty(1, 64)}, {QStringLiteral("interval"), integerProperty(1, 3072)}}),
		repeatNotes);
	add(registry, "generate", QStringLiteral("Write a rhythm into the clip from its start: 'kind' euclid ('pulses' spread evenly over "
		"'steps', turned by 'rotate') or random ('pulses' in 'steps' on average, from 'seed'), 'step' ticks a step, on 'key' at "
		"'velocity'. One undo step."),
		{{QStringLiteral("kind"), enumProperty({QStringLiteral("euclid"), QStringLiteral("random")})}, {QStringLiteral("steps"), integerProperty(1, 128)},
			{QStringLiteral("pulses"), integerProperty(0, 128)}, {QStringLiteral("rotate"), integerProperty(-128, 128)},
			{QStringLiteral("step"), integerProperty(1, 3072)}, {QStringLiteral("key"), integerProperty(0, 127)},
			{QStringLiteral("velocity"), integerProperty(0, 200)}, {QStringLiteral("seed"), integerProperty(0, 2147483647)}}, generate);
}

} // namespace lmms
