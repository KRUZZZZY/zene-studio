/*
 * ControlCommandsNoteHarmony.cpp - notes follow the chord track, clip positions quantised, scales of your own
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
 *  - note.follow_chords (ED-117): the notes in 'scope' move to the nearest tone of the chord the chord
 *    track plays where each sounds ('mode' scale: of that chord's scale) - one undo step;
 *  - clip.quantize (ED-138): clip start positions snapped to a grid - one undo step;
 *  - scale.define / scale.custom (ED-115): scales of your own (semitone offsets), which scale.set,
 *    the scale snap and note.transpose_in_key then take by name. */

#include <QJsonArray>

#include "ChordTrack.h"
#include "ChordVocabulary.h"
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

//! The key nearest @a key whose pitch class is in @a classes (down on a tie), or @a key.
int nearestIn(int key, const std::vector<int>& classes)
{
	for (int distance = 0; distance < 12; ++distance)
	{
		for (const int candidate : {key - distance, key + distance})
		{
			if (candidate < 0 || candidate >= NumKeys) { continue; }
			if (std::find(classes.begin(), classes.end(), ChordVocabulary::pitchClassOf(candidate)) != classes.end()) { return candidate; }
		}
	}
	return key;
}

//! The pitch classes the chord track sounds at @a pos (its chord, or its scale), empty where it is silent.
std::vector<int> classesAt(tick_t pos, bool scale)
{
	const ChordEvent* event = Engine::getSong()->chordTrack().soundingAt(pos);  // the chord heard there, not one starting there
	if (event == nullptr) { return {}; }
	const ChordVocabulary::Chord* chord = scale && !event->scale.isEmpty() ? ChordVocabulary::scaleByName(event->scale) : ChordVocabulary::chordByName(event->chord);
	return chord != nullptr ? ChordVocabulary::pitchClassesOf(*chord, event->root) : std::vector<int>{};
}

ControlResult followChords(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	if (Engine::getSong()->chordTrack().empty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the chord track is empty")); }
	const QVector<Note*> notes = scoped(clip, args.value(QStringLiteral("scope")).toString());
	const bool scale = args.value(QStringLiteral("mode")).toString() == QLatin1String("scale");
	// Where the clip sounds: a clip trimmed from the left starts its notes earlier than its position.
	const tick_t start = clip->startPosition().getTicks() + clip->startTimeOffset().getTicks();
	std::vector<std::pair<Note*, int>> moves;
	for (Note* note : notes)
	{
		const std::vector<int> classes = classesAt(start + note->pos().getTicks(), scale);
		const int key = classes.empty() ? note->key() : nearestIn(note->key(), classes);
		if (key != note->key()) { moves.emplace_back(note, key); }
	}
	if (moves.empty()) { return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("moved"), 0}}); }
	clip->addJournalCheckPoint();
	for (const auto& [note, key] : moves) { note->setKey(key); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.follow_chords"), {{QStringLiteral("moved"), static_cast<int>(moves.size())}}));
}

ControlResult quantizeClips(const QJsonObject& args)
{
	const tick_t grid = std::max(1, args.value(QStringLiteral("grid")).toInt(TimePos::ticksPerBar()));
	QVector<ClipRef> refs;
	for (const QJsonValue& id : args.value(QStringLiteral("clips")).toArray())
	{
		ClipRef ref;
		ControlResult error;
		if (!resolveClip(id.toString(), &ref, &error)) { return error; }
		refs.append(ref);
	}
	if (refs.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("name the 'clips'")); }
	const auto snapped = [grid](const ClipRef& ref) { return (ref.clip->startPosition().getTicks() + grid / 2) / grid * grid; };
	QVector<JournallingObject*> tracks;
	for (const ClipRef& ref : refs)
	{
		if (snapped(ref) != ref.clip->startPosition().getTicks() && !tracks.contains(ref.track)) { tracks.append(ref.track); }
	}
	if (tracks.isEmpty()) { return ControlResult::success({{QStringLiteral("moved"), 0}, {QStringLiteral("grid"), static_cast<int>(grid)}}); }
	addUndoStep(tracks);
	int moved = 0;
	for (const ClipRef& ref : refs)
	{
		if (snapped(ref) != ref.clip->startPosition().getTicks()) { ref.clip->movePosition(TimePos(snapped(ref))); ++moved; }
	}
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("moved"), moved}, {QStringLiteral("grid"), static_cast<int>(grid)}});
}

ControlResult defineScale(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a scale needs a 'name'")); }
	QJsonObject all = customScales();
	const QJsonObject before = all;
	if (!args.contains(QStringLiteral("degrees"))) { all.remove(name); }
	else
	{
		QJsonArray degrees;
		for (const QJsonValue& degree : args.value(QStringLiteral("degrees")).toArray())
		{
			const int offset = degree.toInt(-1);
			if (offset < 0 || offset > 11) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("each degree is 0..11 semitones")); }
			if (!degrees.contains(offset)) { degrees.append(offset); }
		}
		if (!degrees.contains(0)) { degrees.prepend(0); }  // a scale holds its root
		all.insert(name, degrees);
	}
	if (all == before) { return ControlResult::success({{QStringLiteral("scales"), all}}); }
	setCustomScales(all);
	addUndoStep([before]() { setCustomScales(before); }, [all]() { setCustomScales(all); });
	return ControlResult::success({{QStringLiteral("scales"), all}});
}

ControlResult listCustom(const QJsonObject&) { return ControlResult::success({{QStringLiteral("scales"), customScales()}}); }

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerNoteHarmonyCommands(ControlRegistry& registry)
{
	add(registry, "note.follow_chords", QStringLiteral("Move the notes in 'scope' to the nearest tone of the chord the chord track plays "
		"where each one sounds - or, with 'mode' scale, of that chord's scale - so a part follows the chords. Notes where the "
		"chord track is silent stay. One undo step."),
		{{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("scope"), enumProperty({QStringLiteral("clip"), QStringLiteral("selection")})},
			{QStringLiteral("mode"), enumProperty({QStringLiteral("chord"), QStringLiteral("scale")})}}, {QStringLiteral("clip")}, true, followChords);
	add(registry, "clip.quantize", QStringLiteral("Snap the start of each of 'clips' to the nearest 'grid' line (ticks; a bar when left "
		"out). One undo step."), {{QStringLiteral("clips"), arrayProperty()}, {QStringLiteral("grid"), integerProperty(1, 3072 * 16)}},
		{QStringLiteral("clips")}, true, quantizeClips);
	add(registry, "scale.define", QStringLiteral("Define a scale of your own: 'name' and 'degrees' (semitone offsets 0..11 from the root; "
		"the root is added when missing). scale.set, the scale snap and note.transpose_in_key take it by name. Without 'degrees' "
		"it removes the scale. Kept in the config file; one undo step."),
		{{QStringLiteral("name"), stringProperty()}, {QStringLiteral("degrees"), arrayProperty()}}, {QStringLiteral("name")}, true, defineScale);
	add(registry, "scale.custom", QStringLiteral("The scales defined with scale.define, each with its degrees."), {}, {}, false, listCustom);
}

} // namespace lmms
