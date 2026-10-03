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
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlScaleShared.h"
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

//! A copy of @a source holding, of all its clips, only @a clip's twin with only @a key's notes.
Track* keyTrack(Track* source, const MidiClip* clip, int key)
{
	Track* copy = source->clone();
	const tick_t start = clip->startPosition().getTicks();
	for (Clip* other : std::vector<Clip*>(copy->getClips().begin(), copy->getClips().end()))
	{
		auto* midi = dynamic_cast<MidiClip*>(other);
		if (midi == nullptr || midi->startPosition().getTicks() != start) { delete other; continue; }  // a clip leaves its track as it goes
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
	auto made = std::make_shared<std::vector<Track*>>();
	// Guarded: a later undo can rebuild the source track as a new object, and then redo has nothing to copy.
	const QPointer<Track> source = clip->getTrack();
	const QPointer<MidiClip> from = clip;
	const auto build = [made, source, from, keys]() {
		if (source.isNull() || from.isNull()) { return; }
		for (int key : keys) { made->push_back(keyTrack(source, from, key)); }
	};
	addUndoStep([made]() { for (auto it = made->rbegin(); it != made->rend(); ++it) { removeTrack(*it); } made->clear(); }, build);
	build();
	QJsonArray ids;
	for (const Track* track : *made) { ids.append(trackIdOf(track)); }
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
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
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
	ControlCommand cmd;
	cmd.id = QLatin1String(id);
	const int dot = cmd.id.indexOf(QLatin1Char('.'));
	cmd.group = cmd.id.left(dot);
	cmd.verb = cmd.id.mid(dot + 1);
	cmd.description = description;
	cmd.argsSchema = objectSchema(args, QJsonArray::fromStringList(required));
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
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
