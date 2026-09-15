/*
 * ControlCommandsChordWrite.cpp - the GENERATOR half of the chord.* command group
 *                                 (SPEC A11-A16): chord.track_write and
 *                                 chord.progression_generate.
 *
 * WHAT MAKES THESE TWO A FILE OF THEIR OWN, beyond the file-length ratchet: they
 * are the group's only verbs whose inverse is a LIVE checkpoint. The four verbs in
 * ControlCommandsChordEdit.cpp edit the chord TRACK, which is project state the
 * Song's journal checkpoint does not carry (so each records an action checkpoint);
 * these two write NOTES into a MidiClip, which IS a JournallingObject whose
 * serialized state is its note list - the mechanism note.add, note.remove and
 * groove.apply reverse with. The A16 classification is per ROW
 * (src/core/ControlReversibilityTableChord.cpp); this seam is the split.
 *
 * The arithmetic is engine code and is NOT here: generateNotes and layOutChord live
 * in src/core/ChordProgression.cpp, the vocabulary in src/core/ChordVocabulary.cpp
 * and the track in src/core/ChordTrack.cpp. What this file owns is the surface of
 * the two generators: their schemas, their refusals, and the one clip write both
 * share.
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

#include "ChordProgression.h"
#include "ChordTrack.h"
#include "ChordVocabulary.h"
#include "ControlChordSupport.h"
#include "ControlEdit.h"          // ClipRef, resolveMidiClip, clipId, trackIdOf
#include "ControlGrooveSupport.h" // readTicks - the shared bounded-tick reader
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

struct ClipWrite
{
	ClipRef ref;
	MidiClip* clip = nullptr;
	QJsonObject before;
	int replaced = 0;
	ControlResult error;
};

ControlResult beginClipWrite(const QJsonObject& args, bool replace, ClipWrite* out)
{
	out->clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &out->ref,
		&out->error);
	if (out->clip == nullptr) { return out->error; }
	out->before = chordClipBefore(out->ref, *out->clip);
	// The piano roll checkpoints the clip before it changes its notes, and
	// MidiClip::addNote itself never journals - the call note.add makes.
	out->clip->addJournalCheckPoint();
	if (replace) { out->replaced = clearClipNotes(out->clip); }
	return ControlResult::success();
}


bool readPatternArg(const QJsonObject& args, ChordProgression::Pattern* out, ControlResult* error)
{
	if (!args.contains(QStringLiteral("pattern"))) { return true; }
	const QString pattern = args.value(QStringLiteral("pattern")).toString();
	if (ChordProgression::patternFromName(pattern, out)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'pattern' is '%1'; it is one of %2").arg(pattern)
			.arg(ChordProgression::patternNames().join(QStringLiteral(", "))));
	return false;
}


/*! Every chord of \a track laid out as notes: the tones of the event, its own
 *  length ("0 means hold" resolved against its neighbours), and the pattern
 *  every generator shares. \a chordsWritten receives how many events produced
 *  notes - an event whose name no longer resolves produces none. */
void writeTrackChords(const ChordTrack& track, ChordProgression::Pattern pattern,
	tick_t fallback, int velocity, std::vector<ChordProgression::NoteSpec>* specs,
	int* chordsWritten)
{
	for (int index = 0; index < track.size(); ++index)
	{
		const ChordEvent& event = track.at(index);
		const std::vector<int> keys = eventKeys(event);
		if (keys.empty()) { continue; }
		const tick_t length = track.effectiveLength(index, fallback);
		ChordProgression::layOutChord(keys, event.pos, length, pattern, velocity, specs);
		++(*chordsWritten);
	}
}


ControlResult chordTrackWrite(const QJsonObject& args)
{
	ChordTrack* track = projectChordTrack();
	if (track == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no chord track to read"));
	}
	if (track->empty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the chord track is empty: write some chords into it first "
				"(chord.set or chord.detect_to_track)"));
	}
	ControlResult error;
	ChordProgression::Pattern pattern = ChordProgression::Pattern::Block;
	if (!readPatternArg(args, &pattern, &error)) { return error; }
	tick_t velocity = DefaultVolume;
	if (args.contains(QStringLiteral("velocity"))
		&& !readTicks(args, QStringLiteral("velocity"), MinVolume, MaxVolume, &velocity, &error))
	{
		return error;
	}
	tick_t fallback = ChordTrack::DefaultEventTicks;
	if (args.contains(QStringLiteral("length"))
		&& !readTicks(args, QStringLiteral("length"), 1, MaxSchemaInteger, &fallback, &error))
	{
		return error;
	}
	const bool replace = args.value(QStringLiteral("replace")).toBool(true);

	ClipWrite write;
	ControlResult begun = beginClipWrite(args, replace, &write);
	if (!begun.ok) { return begun; }

	std::vector<ChordProgression::NoteSpec> specs;
	int chordsWritten = 0;
	writeTrackChords(*track, pattern, fallback, static_cast<int>(velocity), &specs,
		&chordsWritten);
	const int written = writeNoteSpecs(write.clip, specs);

	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(write.ref.id));
	result.insert(QStringLiteral("track"), trackIdOf(write.ref.track));
	result.insert(QStringLiteral("pattern"), ChordProgression::patternName(pattern));
	result.insert(QStringLiteral("chords_written"), chordsWritten);
	result.insert(QStringLiteral("notes_written"), written);
	result.insert(QStringLiteral("notes_replaced"), write.replaced);
	result.insert(QStringLiteral("note_count"),
		static_cast<int>(write.clip->notes().size()));
	result.insert(QStringLiteral("__transaction"), chordClipInverse(write.before,
		QJsonObject{{QStringLiteral("clip"), clipId(write.ref.id)},
			{QStringLiteral("command"), QStringLiteral("chord.track_write")}}));
	return ControlResult::success(result);
}


ControlResult chordProgressionGenerate(const QJsonObject& args)
{
	ControlResult error;
	ChordProgression::Request request;
	if (!readProgressionRequest(args, &request, &error)) { return error; }

	std::vector<ChordProgression::ChordStep> chords;
	QString engineError;
	if (!ChordProgression::chordsFor(request, &chords, &engineError))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, engineError);
	}
	std::vector<ChordProgression::NoteSpec> specs;
	if (!ChordProgression::generateNotes(request, &specs, &engineError))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, engineError);
	}
	const bool replace = args.value(QStringLiteral("replace")).toBool(true);

	ClipWrite write;
	ControlResult begun = beginClipWrite(args, replace, &write);
	if (!begun.ok) { return begun; }
	const int written = writeNoteSpecs(write.clip, specs);

	QJsonArray chordList;
	for (const ChordProgression::ChordStep& step : chords)
	{
		QJsonObject object;
		object.insert(QStringLiteral("degree"), step.degree);
		object.insert(QStringLiteral("root"), step.root);
		object.insert(QStringLiteral("key"), step.rootKey);
		object.insert(QStringLiteral("chord"), step.name);
		chordList.append(object);
	}

	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(write.ref.id));
	result.insert(QStringLiteral("track"), trackIdOf(write.ref.track));
	result.insert(QStringLiteral("progression"), request.progression);
	result.insert(QStringLiteral("scale"), request.scale);
	result.insert(QStringLiteral("root"), request.root);
	result.insert(QStringLiteral("octave"), request.octave);
	result.insert(QStringLiteral("tick"), static_cast<qint64>(request.start));
	result.insert(QStringLiteral("step_ticks"), static_cast<qint64>(request.stepTicks));
	result.insert(QStringLiteral("steps"), request.steps);
	result.insert(QStringLiteral("pattern"), ChordProgression::patternName(request.pattern));
	result.insert(QStringLiteral("velocity"), request.velocity);
	result.insert(QStringLiteral("seed"), static_cast<qint64>(request.seed));
	result.insert(QStringLiteral("variation"), static_cast<double>(request.variation));
	result.insert(QStringLiteral("chords"), chordList);
	result.insert(QStringLiteral("notes_written"), written);
	result.insert(QStringLiteral("notes_replaced"), write.replaced);
	result.insert(QStringLiteral("note_count"),
		static_cast<int>(write.clip->notes().size()));
	result.insert(QStringLiteral("__transaction"), chordClipInverse(write.before,
		QJsonObject{{QStringLiteral("clip"), clipId(write.ref.id)},
			{QStringLiteral("command"), QStringLiteral("chord.progression_generate")}}));
	return ControlResult::success(result);
}


//! The result schema both generators share: the clip they wrote into and the
//! numbers they report about it.
QJsonObject generatorResultSchema(QJsonObject extra)
{
	QJsonObject properties{
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("pattern"), stringProperty()},
		{QStringLiteral("notes_written"), integerProperty(0, MaxSchemaInteger)},
		{QStringLiteral("notes_replaced"), integerProperty(0, MaxSchemaInteger)},
		{QStringLiteral("note_count"), integerProperty(0, MaxSchemaInteger)},
	};
	for (auto it = extra.begin(); it != extra.end(); ++it) { properties.insert(it.key(), it.value()); }
	return objectSchema(properties);
}

} // namespace

} // namespace control


void registerChordWriteCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("chord.track_write");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("track_write");
		cmd.description = QStringLiteral("Write the CHORD TRACK's own chords into a MIDI "
			"clip as notes: every event's chord is voiced from its root and laid out under "
			"'pattern' (block, arpeggio_up, arpeggio_down, broken), each event starting where "
			"the track says and sounding for as long as the track says (the distance to the "
			"next chord, or 'length' for the last one). 'replace' (default true) clears the "
			"clip's notes first and reports how many went; false adds to them. Reversible "
			"through the ProjectJournal (MidiClip checkpoint). An empty chord track is "
			"refused, typed: there is nothing to write.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("pattern"), enumProperty(ChordProgression::patternNames())},
			{QStringLiteral("velocity"), integerProperty(MinVolume, MaxVolume)},
			{QStringLiteral("length"), integerProperty(1, MaxSchemaInteger)},
			{QStringLiteral("replace"), booleanProperty()},
		}, {QStringLiteral("clip")});
		cmd.resultSchema = generatorResultSchema({
			{QStringLiteral("chords_written"), integerProperty(0, ChordTrack::MaxEvents)},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return chordTrackWrite(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("chord.progression_generate");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("progression_generate");
		cmd.description = QStringLiteral("GENERATE a named progression into a MIDI clip as "
			"notes. The walk and the key are the pre-existing vocabulary's: each step of the "
			"progression is the chord on that SCALE DEGREE, built by stacking the scale's own "
			"tones in thirds and named by this engine's chord table (a stack the table cannot "
			"name comes back with an empty 'chord' and is still written). 'pattern' is block, "
			"arpeggio_up, arpeggio_down or broken; 'step_ticks' is how long each chord lasts "
			"(default one 4/4 bar); 'steps' defaults to one pass of the walk. SEEDED AND "
			"REPEATABLE: 'variation' (0..1, default 0) opens the draws - each chord's "
			"voicing, its position and its velocity - and every draw is a pure function of "
			"'seed' and that chord's own identity, so the same request reproduces the same "
			"take note for note and a different seed gives a different one. With 'variation' "
			"0 nothing is drawn and the seed decides nothing. Reversible through the "
			"ProjectJournal (MidiClip checkpoint).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("progression"),
				enumProperty(ChordProgression::progressionNames())},
			{QStringLiteral("scale"), enumProperty(ChordVocabulary::scaleNames())},
			{QStringLiteral("root"), integerProperty(0, 11)},
			{QStringLiteral("octave"),
				integerProperty(ChordVocabulary::MinOctave, ChordVocabulary::MaxOctave)},
			{QStringLiteral("tick"), tickProperty()},
			{QStringLiteral("step_ticks"), integerProperty(1, MaxSchemaInteger)},
			{QStringLiteral("steps"), integerProperty(1, ChordProgression::MaxSteps)},
			{QStringLiteral("pattern"), enumProperty(ChordProgression::patternNames())},
			{QStringLiteral("velocity"), integerProperty(MinVolume, MaxVolume)},
			{QStringLiteral("seed"), integerProperty(0, MaxSchemaInteger)},
			{QStringLiteral("variation"), numberProperty()},
			{QStringLiteral("replace"), booleanProperty()},
		}, {QStringLiteral("clip"), QStringLiteral("progression")});
		cmd.resultSchema = generatorResultSchema({
			{QStringLiteral("progression"), stringProperty()},
			{QStringLiteral("scale"), stringProperty()},
			{QStringLiteral("root"), integerProperty(0, 11)},
			{QStringLiteral("octave"),
				integerProperty(ChordVocabulary::MinOctave, ChordVocabulary::MaxOctave)},
			{QStringLiteral("tick"), tickProperty()},
			{QStringLiteral("step_ticks"), integerProperty(1, MaxSchemaInteger)},
			{QStringLiteral("steps"), integerProperty(1, ChordProgression::MaxSteps)},
			{QStringLiteral("velocity"), integerProperty(MinVolume, MaxVolume)},
			{QStringLiteral("seed"), integerProperty(0, MaxSchemaInteger)},
			{QStringLiteral("variation"), numberProperty()},
			{QStringLiteral("chords"), arrayProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return chordProgressionGenerate(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
