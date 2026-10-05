/*
 * ControlCommandsNoteMoves.cpp - notes to another track, split by pitch, key detection, MIDI panic
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
/*! 0.5.0 L07 (W3):
 *  - note.move_to_track (ED-106): the notes in 'scope' leave their clip for a new clip on another
 *    instrument track, at the same place - one undo step;
 *  - note.split_by_pitch (ED-106): a clip's notes split into one new track per key (a drum loop into
 *    its drums), each a copy of the source track holding only that key's notes - one undo step;
 *  - scale.detect (ED-114): the key and scale (major or minor) a clip's notes fit best, with the
 *    runners-up - read only; scale.set takes the answer;
 *  - transport.panic (ED-076): every sounding note of every instrument stopped now. */

#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <vector>

#include <QJsonArray>
#include <QPointer>

#include "ControlCommandsNoteShared.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlScaleShared.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "InstrumentTrack.h"

namespace lmms
{

using namespace control;
using namespace control::notes;

namespace
{

ControlResult moveToTrack(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	auto* to = dynamic_cast<InstrumentTrack*>(resolveTrack(args.value(QStringLiteral("track")).toString(), &error));
	if (to == nullptr) { return error.errorMessage.isEmpty() ? ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'track' is an instrument track")) : error; }
	if (to == clip->getTrack()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the notes are on that track already")); }
	const QVector<Note*> notes = scoped(clip, args.value(QStringLiteral("scope")).toString(QStringLiteral("selection")));
	if (notes.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no notes in that scope")); }
	addUndoStep(QVector<JournallingObject*>{clip, to});
	auto* target = dynamic_cast<MidiClip*>(to->createClip(clip->startPosition()));
	target->setStartTimeOffset(clip->startTimeOffset());  // the notes sound where they did
	for (Note* note : notes)
	{
		target->addNote(*note, false);
		clip->removeNote(note);
	}
	target->rearrangeAllNotes();
	target->updateLength();
	QJsonObject result = finish(ref, clip, QStringLiteral("note.move_to_track"), {{QStringLiteral("moved"), notes.size()}});
	result.insert(QStringLiteral("to_clip"), clipIdOf(target));
	return ControlResult::success(result);
}

//! Where @a clip sits in @a track's clip order, -1 when it is not there; a clone keeps the order.
int clipOrder(const Track* track, const Clip* clip)
{
	const Track::clipVector& clips = track->getClips();
	for (int i = 0; i < static_cast<int>(clips.size()); ++i) { if (clips[i] == clip) { return i; } }
	return -1;
}

//! A copy of @a source holding, of all its clips, only @a clip's twin with only @a key's notes.
Track* keyTrack(Track* source, const MidiClip* clip, int key)
{
	// The twin is found by place in the clip order: a start position alone also names a sibling clip.
	const int twin = clipOrder(source, clip);
	Track* copy = source->clone();
	const std::vector<Clip*> clips(copy->getClips().begin(), copy->getClips().end());
	for (int i = 0; i < static_cast<int>(clips.size()); ++i)
	{
		auto* midi = dynamic_cast<MidiClip*>(clips[i]);
		if (midi == nullptr || i != twin) { delete clips[i]; continue; }  // a clip leaves its track as it goes
		for (Note* note : NoteVector(midi->notes())) { if (note->key() != key) { midi->removeNote(note); } }
	}
	copy->setName(QStringLiteral("%1 %2%3").arg(source->name(), scaleRootNames().value(key % 12)).arg(key / 12));
	return copy;
}

ControlResult splitByPitch(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	std::set<int> keys;
	for (const Note* note : clip->notes()) { keys.insert(note->key()); }
	if (keys.size() < 2) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the clip holds fewer than two keys")); }
	// The new tracks' ids, never their pointers: track.remove and its undo free a track and build an equal one.
	auto made = std::make_shared<std::vector<int>>();
	// The source by name for the same reason: undoing track.remove rebuilds it as a new object, so a
	// Track* or MidiClip* kept across the undo is null when the redo runs.
	const QString sourceName = clipIdOf(clip);
	const auto build = [made, sourceName, keys]() {
		ClipRef ref;
		ControlResult error;
		MidiClip* from = resolveMidiClip(sourceName, &ref, &error);
		if (from == nullptr) { return; }
		for (int key : keys) { made->push_back(keyTrack(ref.track, from, key)->id()); }
	};
	addUndoStep([made]() {
		for (auto it = made->rbegin(); it != made->rend(); ++it) { if (Track* live = trackWithId(*it)) { removeTrack(live); } }
		made->clear();
	}, build);
	build();
	QJsonArray ids;
	for (const int id : *made) { ids.append(trackId(id)); }
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("tracks"), ids}, {QStringLiteral("keys"), static_cast<int>(keys.size())}});
}

//! How well pitch classes @a weight fit the scale @a classes: the share of the note time inside it.
double fit(const std::array<double, 12>& weight, const std::vector<int>& classes)
{
	double inside = 0.0;
	double total = 0.0;
	for (int pc = 0; pc < 12; ++pc)
	{
		total += weight[static_cast<std::size_t>(pc)];
		if (std::find(classes.begin(), classes.end(), pc) != classes.end()) { inside += weight[static_cast<std::size_t>(pc)]; }
	}
	return total > 0.0 ? inside / total : 0.0;
}

ControlResult detectScale(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	std::array<double, 12> weight{};
	for (const Note* note : clip->notes()) { weight[static_cast<std::size_t>(note->key() % 12)] += note->length().getTicks(); }
	struct Candidate { double fit; double tonic; int root; QString scale; };
	std::vector<Candidate> candidates;
	for (const QString& scale : {QStringLiteral("Major"), QStringLiteral("Minor")})
	{
		for (int root = 0; root < 12; ++root)
		{
			// The tonic's own weight breaks the ties a relative major and minor share.
			candidates.push_back({fit(weight, scalePitchClasses(root, scaleDegreesOf(scale))), weight[static_cast<std::size_t>(root)], root, scale});
		}
	}
	if (clip->notes().empty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the clip has no notes")); }
	// Stable: equal fits and tonics keep the order built above - Major before Minor, C upwards.
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
		return a.fit != b.fit ? a.fit > b.fit : a.tonic > b.tonic;
	});
	QJsonArray top;
	for (std::size_t i = 0; i < 3; ++i)
	{
		top.append(QJsonObject{{QStringLiteral("root"), scaleRootNames().value(candidates[i].root)}, {QStringLiteral("scale"), candidates[i].scale},
			{QStringLiteral("fit"), candidates[i].fit}});
	}
	return ControlResult::success({{QStringLiteral("root"), top.first().toObject().value(QStringLiteral("root"))},
		{QStringLiteral("scale"), candidates.front().scale}, {QStringLiteral("fit"), candidates.front().fit}, {QStringLiteral("candidates"), top}});
}

ControlResult panic(const QJsonObject&)
{
	int silenced = 0;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { instrument->silenceAllNotes(); ++silenced; }
	}
	return ControlResult::success({{QStringLiteral("silenced"), silenced}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerNoteMoveCommands(ControlRegistry& registry)
{
	add(registry, "note.move_to_track", QStringLiteral("Move the notes in 'scope' ('selection' when left out, or 'clip') out of their "
		"clip into a new clip on instrument track 'track', at the same place. One undo step."),
		{{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("track"), stringProperty()},
			{QStringLiteral("scope"), enumProperty({QStringLiteral("clip"), QStringLiteral("selection")})}},
		{QStringLiteral("clip"), QStringLiteral("track")}, true, moveToTrack);
	add(registry, "note.split_by_pitch", QStringLiteral("Split a clip by key: one new track per key it holds, each a copy of the clip's "
		"track (its instrument and effects) whose clip keeps only that key's notes - a drum loop into its drums. One undo step."),
		{{QStringLiteral("clip"), stringProperty()}}, {QStringLiteral("clip")}, true, splitByPitch);
	add(registry, "scale.detect", QStringLiteral("The key and scale (major or minor) a clip's notes fit best, weighted by how long each "
		"sounds, with the two runners-up and how much of the music each holds ('fit', 0..1). Read only; scale.set takes the answer."),
		{{QStringLiteral("clip"), stringProperty()}}, {QStringLiteral("clip")}, false, detectScale);
	add(registry, "transport.panic", QStringLiteral("Stop every sounding note of every instrument now - the MIDI panic for a stuck note. "
		"Nothing in the project changes."), {}, {}, false, panic);
}

} // namespace lmms
