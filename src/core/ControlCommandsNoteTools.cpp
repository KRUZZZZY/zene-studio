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

#include "ClipAudioLoop.h"
#include "ControlCommandsNoteShared.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlScaleShared.h"
#include "ControlVocabulary.h"
#include "SampleClip.h"

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
	const bool changes = std::any_of(notes.cbegin(), notes.cend(), [twice](const Note* note)
	{
		const int target = std::clamp(twice - note->key(), 0, NumKeys - 1);
		return target != note->key();
	});
	if (changes) { clip->addJournalCheckPoint(); }
	for (Note* note : notes) { note->setKey(std::clamp(twice - note->key(), 0, NumKeys - 1)); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.invert"), {{QStringLiteral("inverted"), notes.size()}}));
}

/*! Which of @a voices the run plays at each step, as INDICES into it.
 *
 *  Luna cluster 5691: this used to be the sorted list of KEYS with one stored note per
 *  key, so two chord members of the same pitch shared a single voice and the second one's
 *  velocity, pan and probability were dropped when the chord was removed. One entry per
 *  note - sorted by key for "up", reversed for "down", back down for "updown" - keeps two
 *  same-pitch notes as two steps that each carry their own state.
 */
std::vector<std::size_t> runOrder(const std::vector<Note>& voices, const QString& pattern)
{
	std::vector<std::size_t> order(voices.size());
	for (std::size_t i = 0; i < voices.size(); ++i) { order[i] = i; }
	std::stable_sort(order.begin(), order.end(),
		[&voices](std::size_t a, std::size_t b) { return voices[a].key() < voices[b].key(); });
	if (pattern == QLatin1String("down")) { std::reverse(order.begin(), order.end()); }
	if (pattern == QLatin1String("updown") && order.size() > 2)
	{
		const std::vector<std::size_t> down = order;
		order.insert(order.end(), down.rbegin() + 1, down.rend() - 1);
	}
	return order;
}

constexpr std::size_t MaxArpeggioNotes = 10000;

bool exceedsArpeggioOutputLimit(const std::map<tick_t, std::vector<Note*>>& chords, tick_t rate)
{
	std::size_t outputCount = 0;
	for (const auto& chordEntry : chords)
	{
		tick_t length = 0;
		for (const Note* note : chordEntry.second)
		{
			length = std::max<tick_t>(length, note->length().getTicks());
		}
		const std::size_t chordCount = static_cast<std::size_t>(length / rate)
			+ (length % rate == 0 ? 0U : 1U);
		if (chordCount > MaxArpeggioNotes - outputCount) { return true; }
		outputCount += chordCount;
	}
	return false;
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
	if (exceedsArpeggioOutputLimit(chords, rate))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("arpeggio output is limited to %1 notes").arg(MaxArpeggioNotes));
	}
	clip->addJournalCheckPoint();
	int made = 0;
	for (auto& [start, chord] : chords)
	{
		tick_t length = 0;
		std::vector<Note> voices;  // each chord member plays as its own note did: velocity, pan, probability, mute...
		voices.reserve(static_cast<std::size_t>(chord.size()));
		for (const Note* note : chord)
		{
			length = std::max<tick_t>(length, note->length().getTicks());
			voices.push_back(*note);
		}
		for (Note* note : chord) { clip->removeNote(note); }
		const std::vector<std::size_t> order = runOrder(voices, args.value(QStringLiteral("pattern")).toString(QStringLiteral("up")));
		for (tick_t at = 0, i = 0; at < length; at += rate, ++i)
		{
			Note step = voices[order[static_cast<std::size_t>(i) % order.size()]];
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
	// Luna cluster 5694: the ratio was std::clamp-ed into 0.05..1.0 and the call still
	// answered ok, so `velocity: 2` reported success while writing a value the caller
	// never asked for. The args schema is a bare number (it is in the
	// ControlCommandsSnapshot canary and may not change), so the range is refused HERE -
	// the same handler-side refusal as 4e288ecb5.
	const double ratio = args.contains(QStringLiteral("velocity"))
		? args.value(QStringLiteral("velocity")).toDouble(0.6) : 0.6;
	if (ratio < 0.05 || ratio > 1.0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'velocity' is %1: the grace notes' velocity ratio must be 0.05..1.0")
				.arg(ratio));
	}
	if (std::none_of(notes.begin(), notes.end(), [offset](const Note* note) { return note->pos().getTicks() >= offset; }))
	{
		// No note has room for a grace note: nothing changes, so no undo step.
		return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("added"), 0}});
	}
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
	const bool changes = std::any_of(notes.cbegin(), notes.cend(), [&classes, steps](const Note* note)
	{
		return stepInScale(note->key(), steps, classes) != note->key();
	});
	if (changes) { clip->addJournalCheckPoint(); }
	int moved = 0;  // a note the scale would take past key 0 or 127 stays, and is not counted
	for (Note* note : notes)
	{
		const int key = stepInScale(note->key(), steps, classes);
		moved += key != note->key() ? 1 : 0;
		note->setKey(key);
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.transpose_in_key"), {{QStringLiteral("moved"), moved}}));
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
	// Luna cluster 2883: the copies' positions were formed with no range check and
	// MidiClip::addNote validates none, so a note at the right edge of the song produced
	// notes ending past MaxSongLength - while the reply still counted every one of them.
	// The farthest copy is the LAST one (i == count), so one check per note covers them
	// all; the sum is done in qint64, as checkNewEndPosition() does.
	for (const Note* note : notes)
	{
		const qint64 farthest = static_cast<qint64>(note->pos().getTicks())
			+ static_cast<qint64>(count) * static_cast<qint64>(interval);
		if (farthest + static_cast<qint64>(note->length().getTicks()) <= static_cast<qint64>(MaxSongLength)) { continue; }
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a repeat of the note at %1 would end at %2, past the song limit of %3")
				.arg(note->pos().getTicks()).arg(farthest + note->length().getTicks())
				.arg(static_cast<qint64>(MaxSongLength)));
	}
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

//! 0.5.0 ED-028 (audio): an audio clip's content turned 'amount' ticks later with wrap-around, through its loop
//! (include/ClipAudioLoop.h) - one Clip checkpoint.
ControlResult rotateAudio(const ClipRef& ref, SampleClip* clip, const QJsonObject& args)
{
	const QString refusal = audioloop::refusal(*clip);
	if (!refusal.isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, refusal); }
	ClipEdits after = clip->clipEdits();
	bool turned = false;
	QString message;
	if (!audioloop::rotated(*clip, args.value(QStringLiteral("amount")).toInt(), &after, &turned, &message))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, message);
	}
	if (!turned) { return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("rotated"), 0}}); }
	clip->addJournalCheckPoint();
	clip->setClipEdits(after);
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("rotated"), 1},
		{QStringLiteral("offset_frames"), static_cast<double>(after.audioLoopOffset)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("clip"), clipId(ref.id)}},
		QStringLiteral("control.undo"), QJsonObject(), true, QStringLiteral("Clip journal checkpoint: undo restores the clip's loop and offset")));
	return ControlResult::success(result);
}

//! ED-028: the notes in scope moved 'amount' ticks later (negative: earlier), wrapped inside the clip's length; an
//! audio clip's content turned the same way (rotateAudio).
ControlResult rotate(const QJsonObject& args)
{
	ClipRef ref;
	QVector<Note*> notes;
	ControlResult error;
	if (resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
	{
		if (auto* audio = dynamic_cast<SampleClip*>(ref.clip)) { return rotateAudio(ref, audio, args); }
	}
	MidiClip* clip = scopedNotes(args, &ref, &notes, &error);
	if (clip == nullptr) { return error; }
	// The span the clip plays: a clip trimmed on the left plays only up to length - startTimeOffset, so a wrap past
	// it would park notes where they never sound (review W3-batchB #3).
	// Luna cluster 4856: the wrap was taken from ZERO, so a left-trimmed clip rotated its
	// notes into the trimmed region - a note at tick 70 with a 20-tick offset and a 20-tick
	// rotation wrapped to tick 10, which the clip never plays. The span is measured from
	// startTimeOffset() and the offset added back, so every note stays inside the audible region.
	const tick_t offset = clip->startTimeOffset().getTicks();
	const tick_t length = std::max<tick_t>(1, clip->length().getTicks() - offset);
	const tick_t amount = args.value(QStringLiteral("amount")).toInt();
	if (amount % length == 0) { return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("rotated"), 0}}); }
	clip->addJournalCheckPoint();
	for (Note* note : notes)
	{
		const tick_t relative = note->pos().getTicks() - offset;
		const tick_t rotated = offset + ((relative + amount) % length + length) % length;
		const tick_t duration = note->length().getTicks();
		const tick_t beforeWrap = offset + length - rotated;
		note->setPos(TimePos(rotated));
		if (duration <= beforeWrap) { continue; }
		Note wrapped = *note;
		note->setLength(TimePos(beforeWrap));
		wrapped.forgetTiming();
		wrapped.setPos(TimePos(offset));
		wrapped.setLength(TimePos(duration - beforeWrap));
		clip->addNote(wrapped, false);
	}
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
	// Luna cluster 5693: `pulses` was independently clamped to `steps`, so a request that
	// passes the schema with more pulses than steps succeeded having quietly asked for
	// fewer. The schema takes pulses 0..128 (the ControlCommandsSnapshot canary), so the
	// impossible combination is refused HERE, the way cf5504f80 does for note.flam's velocity.
	if (args.value(QStringLiteral("pulses")).toInt(5) > steps)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'pulses' is %1, more than the %2 steps asked for: a step cannot "
				"carry more than one pulse").arg(args.value(QStringLiteral("pulses")).toInt(5)).arg(steps));
	}
	const int pulses = std::clamp(args.value(QStringLiteral("pulses")).toInt(5), 0, steps);
	const tick_t step = std::max(1, args.value(QStringLiteral("step")).toInt(12));
	const bool random = args.value(QStringLiteral("kind")).toString() == QLatin1String("random");
	std::mt19937 rng(static_cast<unsigned>(args.value(QStringLiteral("seed")).toInt(1)));
	if (pulses == 0) { return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("added"), 0}}); }  // no step to add
	// Luna cluster 1154: the checkpoint was taken before the random draw, and a draw that hits
	// nothing - which it can, at 1 pulse over 128 steps - still answered ok, so a roll that
	// wrote no note pushed an undo step. The hit set is decided first; the checkpoint follows it.
	std::vector<bool> on = euclid(steps, pulses, args.value(QStringLiteral("rotate")).toInt(0));
	if (random)
	{
		on.assign(static_cast<std::size_t>(steps), false);
		for (int i = 0; i < steps; ++i)
		{
			on[static_cast<std::size_t>(i)] = std::uniform_int_distribution<int>(0, steps - 1)(rng) < pulses;
		}
	}
	if (std::none_of(on.begin(), on.end(), [](bool hit) { return hit; }))
	{
		return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("added"), 0}});
	}
	Note note(TimePos(step), TimePos(0), args.value(QStringLiteral("key")).toInt(DefaultKey),
		static_cast<volume_t>(args.value(QStringLiteral("velocity")).toInt(DefaultVolume)));
	clip->addJournalCheckPoint();
	int made = 0;
	for (int i = 0; i < steps; ++i)
	{
		if (!on[static_cast<std::size_t>(i)]) { continue; }
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
		"(0.05..1 - a grace note always sounds -, 0.6 when left out) of it - a flam. One undo step."),
		with({{QStringLiteral("offset"), integerProperty(1, 96)}, {QStringLiteral("velocity"), numberProperty()}}), flam);
	add(registry, "transpose_in_key", QStringLiteral("Move the notes in 'scope' 'steps' scale degrees (negative: down) within the scale "
		"scale.set chose, so they stay in key. One undo step."), with({{QStringLiteral("steps"), integerProperty(-24, 24)}}), transposeInKey);
	add(registry, "rotate", QStringLiteral("Move the notes in 'scope' 'amount' ticks later (negative: earlier), wrapping round inside "
		"the clip's length, so the pattern turns without growing. On an audio clip its content turns the same way ('scope' is not "
		"read): inside its loop, or inside the clip, which then loops exactly the audio it plays (refused, typed, for a clip longer "
		"than its audio with no loop, or one whose loop starts after its first tick). One undo step."),
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
