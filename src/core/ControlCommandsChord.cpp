/*
 * ControlCommandsChord.cpp - the chord.* command group (SPEC A11-A16), READ
 *                            half: chord.get_state, chord.detect and
 *                            chord.progression_list.
 *
 * THE ENGINE HALF. The chord vocabulary is the one this product already has:
 * InstrumentFunctionNoteStacking::ChordTable's 95 entries, the same table the
 * piano roll's chord and scale selectors read, reached through
 * include/ChordVocabulary.h, which is a read-only VIEW over it and adds no
 * table of this fork's own. `<chord-track>` is the persistent entity
 * (include/ChordTrack.h); include/ChordDetect.h says what a clip's notes spell;
 * include/ChordProgression.h holds the catalogue and the seeded generator. All
 * of that arithmetic is testable with no Engine at all, which is what
 * tests/src/core/ChordTrackTest.cpp, ChordDetectTest.cpp and
 * ChordProgressionTest.cpp do with it.
 *
 * WHAT THIS FILE ADDS IS THE SURFACE. Without it a chord track could only be
 * written by editing the project file by hand, and a detection could only be
 * read by a human looking at a piano roll - the gap AGENT-TOOLING.md section 1
 * makes a defect: a feature that cannot be driven through the socket is not in
 * the release.
 *
 * The shared helpers are in ControlChordSupport.cpp (wire form, schemas,
 * argument readers, the two undo mechanisms) and the mutating verbs in
 * ControlCommandsChordEdit.cpp - the split the automation, warp, rack, comp and
 * groove groups all use.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ChordDetect.h"
#include "ChordVocabulary.h"
#include "ControlChordSupport.h"
#include "ControlEdit.h" // ClipRef, resolveMidiClip, clipId, trackIdOf
#include "ControlRegistry.h"
#include "MidiClip.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace control
{

namespace
{

const QString kGroup = QStringLiteral("chord");


QJsonObject matchJson(const ChordDetect::ChordMatch& match)
{
	QJsonObject out;
	out.insert(QStringLiteral("pos"), static_cast<qint64>(match.pos));
	out.insert(QStringLiteral("length"), static_cast<qint64>(match.length));
	out.insert(QStringLiteral("notes"), match.notes);
	out.insert(QStringLiteral("bass"), match.bass);
	out.insert(QStringLiteral("root"), match.root);
	out.insert(QStringLiteral("key"), match.rootKey);
	out.insert(QStringLiteral("chord"), match.chord);
	out.insert(QStringLiteral("scale"), match.scale);
	out.insert(QStringLiteral("missing"), match.missing);
	out.insert(QStringLiteral("extra"), match.extra);
	out.insert(QStringLiteral("exact"), match.exact);
	return out;
}


QJsonObject keyJson(const ChordDetect::KeyEstimate& key)
{
	QJsonObject out;
	out.insert(QStringLiteral("scale"), key.scale);
	out.insert(QStringLiteral("root"), key.root);
	out.insert(QStringLiteral("key"), key.rootKey);
	out.insert(QStringLiteral("pitch_classes"), key.pitchClasses);
	out.insert(QStringLiteral("complete"), key.complete);
	return out;
}


QJsonObject progressionJson(const ChordProgression::Entry& entry)
{
	QJsonArray degrees;
	for (const int degree : entry.degrees) { degrees.append(degree); }
	QJsonObject out;
	out.insert(QStringLiteral("name"), entry.name);
	out.insert(QStringLiteral("degrees"), degrees);
	out.insert(QStringLiteral("steps"), static_cast<int>(entry.degrees.size()));
	return out;
}


/*! The catalogue SPELT OUT in one key: every named progression evaluated against
 *  \a scale (root \a root, octave \a octave), so an interface can show what each
 *  one WOULD write before anything is written. Each chord is named by the SAME
 *  vocabulary the generator writes with, so a suggestion and the take it stands
 *  for cannot disagree. An entry whose walk the scale cannot be built on comes
 *  back with an empty `chords` and the engine's own `reason`, never silently
 *  dropped from the list. */
QJsonArray progressionSuggestions(const QString& scale, int root, int octave)
{
	QJsonArray out;
	for (const ChordProgression::Entry& entry : ChordProgression::catalogue())
	{
		ChordProgression::Request request;
		request.progression = entry.name;
		request.scale = scale;
		request.root = root;
		request.octave = octave;
		request.steps = static_cast<int>(entry.degrees.size());

		QJsonObject object = progressionJson(entry);
		std::vector<ChordProgression::ChordStep> chords;
		QString error;
		if (!ChordProgression::chordsFor(request, &chords, &error))
		{
			object.insert(QStringLiteral("chords"), QJsonArray());
			object.insert(QStringLiteral("reason"), error);
			out.append(object);
			continue;
		}
		QJsonArray chordList;
		for (const ChordProgression::ChordStep& step : chords)
		{
			QJsonObject chord;
			chord.insert(QStringLiteral("degree"), step.degree);
			chord.insert(QStringLiteral("root"), step.root);
			chord.insert(QStringLiteral("key"), step.rootKey);
			chord.insert(QStringLiteral("chord"), step.name);
			chordList.append(chord);
		}
		object.insert(QStringLiteral("chords"), chordList);
		out.append(object);
	}
	return out;
}


//! Reads a pitch-class root argument in 0..11, defaulting to \a fallback when
//! absent. The schema already bounds it, so out-of-range never reaches here.
int suggestionRoot(const QJsonObject& args, int fallback)
{
	return args.contains(QStringLiteral("root"))
		? args.value(QStringLiteral("root")).toInt() : fallback;
}


int suggestionOctave(const QJsonObject& args, int fallback)
{
	return args.contains(QStringLiteral("octave"))
		? args.value(QStringLiteral("octave")).toInt() : fallback;
}


ControlResult chordGetState()
{
	ChordTrack* track = projectChordTrack();
	if (track == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no chord track to read"));
	}
	return ControlResult::success(chordTrackState(*track));
}


ControlResult chordDetect(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }

	ChordDetect::DetectOptions options;
	if (!readDetectOptions(args, &options, &error)) { return error; }

	const std::vector<ChordDetect::ChordMatch> matches =
		ChordDetect::detectChords(clip->notes(), options);
	const ChordDetect::KeyEstimate key = ChordDetect::detectKey(clip->notes());

	QJsonArray chords;
	for (const ChordDetect::ChordMatch& match : matches) { chords.append(matchJson(match)); }

	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(ref.id));
	result.insert(QStringLiteral("track"), trackIdOf(ref.track));
	result.insert(QStringLiteral("count"), static_cast<int>(matches.size()));
	result.insert(QStringLiteral("window_ticks"), static_cast<qint64>(options.windowTicks));
	result.insert(QStringLiteral("min_pitch_classes"), options.minPitchClasses);
	result.insert(QStringLiteral("chords"), chords);
	result.insert(QStringLiteral("key"), keyJson(key));
	return ControlResult::success(result);
}


ControlResult chordProgressionList(const QJsonObject& args)
{
	QJsonArray progressions;
	for (const ChordProgression::Entry& entry : ChordProgression::catalogue())
	{
		progressions.append(progressionJson(entry));
	}
	QJsonArray scales;
	for (const QString& name : ChordVocabulary::scaleNames()) { scales.append(name); }
	QJsonArray chords;
	for (const QString& name : ChordVocabulary::chordNames()) { chords.append(name); }
	QJsonArray patterns;
	for (const QString& name : ChordProgression::patternNames()) { patterns.append(name); }

	QJsonObject result;
	result.insert(QStringLiteral("progressions"), progressions);
	result.insert(QStringLiteral("count"), static_cast<int>(ChordProgression::catalogue().size()));
	result.insert(QStringLiteral("scales"), scales);
	result.insert(QStringLiteral("chords"), chords);
	result.insert(QStringLiteral("patterns"), patterns);
	result.insert(QStringLiteral("max_steps"), ChordProgression::MaxSteps);
	result.insert(QStringLiteral("max_degree_count"), ChordProgression::MaxDegreeCount);
	result.insert(QStringLiteral("max_chords"), ChordTrack::MaxEvents);

	// With a scale named, the catalogue is also SPELLED OUT in that key: each
	// progression's own chords, so a caller can suggest and preview before it
	// writes. Read-only - the same walk chordsFor() computes for the generator.
	if (args.contains(QStringLiteral("scale")))
	{
		const QString scale = args.value(QStringLiteral("scale")).toString();
		const int root = suggestionRoot(args, 0);
		const int octave = suggestionOctave(args, 4);
		result.insert(QStringLiteral("suggest_scale"), scale);
		result.insert(QStringLiteral("suggest_root"), root);
		result.insert(QStringLiteral("suggest_octave"), octave);
		result.insert(QStringLiteral("suggestions"),
			progressionSuggestions(scale, root, octave));
	}

	const QString name = args.value(QStringLiteral("name")).toString();
	if (!name.isEmpty())
	{
		const ChordProgression::Entry* entry = ChordProgression::find(name);
		if (entry == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no progression called '%1' (this engine knows %2)")
					.arg(name).arg(ChordProgression::progressionNames().size()));
		}
		result.insert(QStringLiteral("progression"), progressionJson(*entry));
	}
	return ControlResult::success(result);
}

} // namespace


} // namespace control


void registerChordCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("chord.get_state");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("The project's CHORD TRACK: every chord with its "
			"position, length, root pitch class, the octave the root sounds in, the root's "
			"absolute MIDI key, the chord's name and the key (scale) it was written in, plus "
			"the track's own bounds. Read-only. The track is project state saved inside "
			"<song> as one <chord-track> element (written only when it holds a chord, so a "
			"project that never used one is byte-identical to before), and it is what "
			"chord.track_write turns into notes. Chord and scale names come from the "
			"piano roll's own vocabulary - see chord.progression_list.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = chordTrackStateSchema();
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject&) { return chordGetState(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("chord.detect");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("detect");
		cmd.description = QStringLiteral("What chords a MIDI clip's notes spell. The notes are "
			"grouped into slices - by default notes that start at the SAME tick; "
			"'window_ticks' widens that for a strummed or humanised take, measured from each "
			"slice's first note so a run of sixteenths cannot chain into one huge slice - and "
			"each slice is named from this engine's chord vocabulary. Every slice reports the "
			"nearest entry with 'missing' (chord tones the slice does not sound) and 'extra' "
			"(tones the chord does not contain), so 'exact' false is visible rather than "
			"rounded to a name, and the whole clip's key is reported too. Read-only: nothing "
			"is written to the clip or the track (write a detection with "
			"chord.detect_to_track).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("window_ticks"), integerProperty(0, DefaultTicksPerBar)},
			{QStringLiteral("min_pitch_classes"), integerProperty(1, 13)},
		}, {QStringLiteral("clip")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("track"), stringProperty()},
			{QStringLiteral("count"), integerProperty(0, MaxSchemaInteger)},
			{QStringLiteral("window_ticks"), tickProperty()},
			{QStringLiteral("min_pitch_classes"), integerProperty(1, 13)},
			{QStringLiteral("chords"), arrayProperty()},
			{QStringLiteral("key"), chordKeySchema()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return chordDetect(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("chord.progression_list");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("progression_list");
		cmd.description = QStringLiteral("What this engine can be asked for: the named "
			"PROGRESSIONS the generator walks (each a name and the scale degrees it steps "
			"through), the SCALES a key may name, the CHORDS a chord-track event may name, "
			"the patterns a generation can lay a chord out with, and the group's bounds. "
			"Every one of those vocabularies is the piano roll's own "
			"(InstrumentFunctionNoteStacking::ChordTable, the table behind its chord and "
			"scale selectors) - this group adds no names of its own. Read-only; with 'name' "
			"one progression is reported on its own. With 'scale' (and optional 'root' "
			"0..11 and 'octave') every progression is also SPELLED OUT in that key as "
			"`suggestions[]`: each entry's own chords, named by the same vocabulary the "
			"generator writes with. That is what the progression panel shows before it "
			"writes; nothing is written here.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("name"), stringProperty()},
			{QStringLiteral("scale"), enumProperty(ChordVocabulary::scaleNames())},
			{QStringLiteral("root"), integerProperty(0, 11)},
			{QStringLiteral("octave"),
				integerProperty(ChordVocabulary::MinOctave, ChordVocabulary::MaxOctave)},
		});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("progressions"), arrayProperty()},
			{QStringLiteral("count"), integerProperty(0, MaxSchemaInteger)},
			{QStringLiteral("scales"), arrayProperty()},
			{QStringLiteral("chords"), arrayProperty()},
			{QStringLiteral("patterns"), arrayProperty()},
			{QStringLiteral("max_steps"), integerProperty(0, ChordProgression::MaxSteps)},
			{QStringLiteral("max_degree_count"),
				integerProperty(0, ChordProgression::MaxDegreeCount)},
			{QStringLiteral("max_chords"), integerProperty(0, ChordTrack::MaxEvents)},
			{QStringLiteral("progression"), chordProgressionSchema()},
			{QStringLiteral("suggest_scale"), stringProperty()},
			{QStringLiteral("suggest_root"), integerProperty(0, 11)},
			{QStringLiteral("suggest_octave"),
				integerProperty(ChordVocabulary::MinOctave, ChordVocabulary::MaxOctave)},
			{QStringLiteral("suggestions"), arrayProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return chordProgressionList(args); };
		registry.registerCommand(cmd);
	}

	// The mutating half: the track edits (this file), then the two generators.
	// Its registration point lives in ControlCommandsChordEdit.cpp and calls
	// ControlCommandsChordWrite.cpp's, so the registry has ONE chord.* entry.
	registerChordEditCommands(registry);
	registerChordWriteCommands(registry);
}

} // namespace lmms
