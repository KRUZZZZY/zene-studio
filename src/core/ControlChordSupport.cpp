/*
 * ControlChordSupport.cpp - the chord.* group's wire helpers and the two
 *                           mechanisms its edits are reversed by
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
 *
 */

#include "ControlChordSupport.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ChordVocabulary.h"
#include "ControlEdit.h"          // ClipRef, transactionPayload, clipId, trackIdOf
#include "ControlGrooveSupport.h" // readTicks - the shared bounded-tick reader
#include "ControlRegistry.h"      // ControlResult, ControlErrorKind
#include "ControlReversibility.h" // control::addUndoStep
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"
#include "TimePos.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace control
{

ChordTrack* projectChordTrack()
{
	Song* song = Engine::getSong();
	return song == nullptr ? nullptr : &song->chordTrack();
}


QString chordTrackXml()
{
	ChordTrack* track = projectChordTrack();
	return track == nullptr ? QString() : track->toXml();
}


void recordChordTrackRestore(const QString& before)
{
	// Captured HERE, not inside the step: the redo half must re-apply the state
	// the edit produced, and by the time an undo runs this is long gone.
	QString after;
	if (ChordTrack* track = projectChordTrack()) { after = track->toXml(); }

	addUndoStep(
		[before]() {
			if (ChordTrack* track = projectChordTrack()) { track->fromXml(before); }
		},
		[after]() {
			if (ChordTrack* track = projectChordTrack()) { track->fromXml(after); }
		});
}


QString chordTrackMechanism()
{
	return QStringLiteral("action checkpoint: the track is project state the Song's journal "
		"checkpoint does not carry (it is not in the track container and is not a "
		"JournallingObject), so the recorded step writes the <chord-track> element captured "
		"before the edit back through ChordTrack::loadSettings - the project loader's own "
		"path. One command is one step");
}


QString chordClipMechanism()
{
	return QStringLiteral("action checkpoint on the CLIP's own ProjectJournal checkpoint: a "
		"MidiClip is a JournallingObject whose serialized state IS its note list, so the "
		"checkpoint taken before the write restores every note this command added or "
		"removed, exactly as note.add and note.remove are reversed");
}


QJsonObject chordTrackInverse(const QString& before, const QString& inverseOp,
	const QJsonObject& inverseArgs)
{
	// The captured element travels as a STRING under "track" (the groove pool's
	// "pool"): it is one element whose bytes are the before-state, and a reader
	// can re-issue it by hand through a project that holds it.
	QJsonObject captured;
	captured.insert(QStringLiteral("track"), before);
	return transactionPayload(captured, inverseOp, inverseArgs, true, chordTrackMechanism());
}


QJsonObject chordClipInverse(const QJsonObject& before, const QJsonObject& target)
{
	return transactionPayload(before, QStringLiteral("control.undo"), target, true,
		chordClipMechanism());
}


QJsonObject chordEventJson(const ChordEvent& event)
{
	QJsonObject out;
	out.insert(QStringLiteral("pos"), static_cast<qint64>(event.pos));
	out.insert(QStringLiteral("length"), static_cast<qint64>(event.length));
	out.insert(QStringLiteral("root"), event.root);
	out.insert(QStringLiteral("octave"), event.octave);
	out.insert(QStringLiteral("key"),
		ChordVocabulary::keyOf(event.root, event.octave));
	out.insert(QStringLiteral("chord"), event.chord);
	out.insert(QStringLiteral("scale"), event.scale);
	return out;
}


QJsonObject chordEventArgs(const ChordEvent& event)
{
	QJsonObject out;
	out.insert(QStringLiteral("pos"), static_cast<qint64>(event.pos));
	out.insert(QStringLiteral("chord"), event.chord);
	out.insert(QStringLiteral("root"), event.root);
	out.insert(QStringLiteral("octave"), event.octave);
	out.insert(QStringLiteral("length"), static_cast<qint64>(event.length));
	out.insert(QStringLiteral("scale"), event.scale);
	return out;
}


QJsonObject chordTrackState(const ChordTrack& track)
{
	QJsonObject out;
	out.insert(QStringLiteral("chords"), track.size());
	out.insert(QStringLiteral("max_chords"), ChordTrack::MaxEvents);
	QJsonArray events;
	for (const ChordEvent& event : track.events()) { events.append(chordEventJson(event)); }
	out.insert(QStringLiteral("events"), events);
	return out;
}


QJsonObject chordTrackStateSchema(QJsonObject extra)
{
	QJsonObject properties{
		{QStringLiteral("chords"), integerProperty(0, ChordTrack::MaxEvents)},
		{QStringLiteral("max_chords"), integerProperty(0, ChordTrack::MaxEvents)},
		{QStringLiteral("events"), arrayProperty()},
	};
	for (auto it = extra.begin(); it != extra.end(); ++it) { properties.insert(it.key(), it.value()); }
	return objectSchema(properties);
}


QJsonObject chordEventSchema()
{
	return objectSchema({
		{QStringLiteral("pos"), tickProperty()},
		{QStringLiteral("length"), tickProperty()},
		{QStringLiteral("root"), integerProperty(0, 11)},
		{QStringLiteral("octave"),
			integerProperty(ChordVocabulary::MinOctave, ChordVocabulary::MaxOctave)},
		{QStringLiteral("key"), integerProperty(0, NumKeys - 1)},
		{QStringLiteral("chord"), stringProperty()},
		{QStringLiteral("scale"), stringProperty()},
	});
}


QJsonObject chordEventArgsSchema()
{
	return objectSchema({
		{QStringLiteral("pos"), tickProperty()},
		{QStringLiteral("chord"), stringProperty()},
		{QStringLiteral("root"), integerProperty(0, 11)},
		{QStringLiteral("octave"),
			integerProperty(ChordVocabulary::MinOctave, ChordVocabulary::MaxOctave)},
		{QStringLiteral("length"), tickProperty()},
		{QStringLiteral("scale"), stringProperty()},
	}, {QStringLiteral("pos"), QStringLiteral("chord")});
}


QJsonObject chordKeySchema()
{
	return objectSchema({
		{QStringLiteral("scale"), stringProperty()},
		{QStringLiteral("root"), integerProperty(-1, 11)},
		{QStringLiteral("key"), integerProperty(-1, NumKeys - 1)},
		{QStringLiteral("pitch_classes"), integerProperty(0, 12)},
		{QStringLiteral("complete"), booleanProperty()},
	});
}


QJsonObject chordProgressionSchema()
{
	return objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("degrees"), arrayProperty()},
		{QStringLiteral("steps"), integerProperty(0, ChordProgression::MaxDegreeCount)},
	});
}


QJsonObject chordClipBefore(const ClipRef& ref, const MidiClip& clip)
{
	QJsonObject before;
	before.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	before.insert(QStringLiteral("track"), trackIdOf(ref.track));
	before.insert(QStringLiteral("note_count"), static_cast<int>(clip.notes().size()));
	return before;
}


//! Reads an OPTIONAL bounded tick argument: an absent key leaves \a out exactly
//! as it was, and a present one goes through the same reader the required ticks
//! use (one definition of "a bounded whole number of ticks").
bool readOptionalTicks(const QJsonObject& args, const QString& key, tick_t minimum,
	tick_t maximum, tick_t* out, ControlResult* error)
{
	if (!args.contains(key)) { return true; }
	return readTicks(args, key, minimum, maximum, out, error);
}


bool readChordEvent(const QJsonObject& args, ChordEvent* out, ControlResult* error)
{
	ControlResult scratch;
	if (error == nullptr) { error = &scratch; }

	ChordEvent event;
	tick_t pos = 0;
	if (!readTicks(args, QStringLiteral("pos"), 0, MaxSchemaInteger, &pos, error))
	{
		return false;
	}
	event.pos = pos;
	event.chord = args.value(QStringLiteral("chord")).toString();
	if (event.chord.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'chord' is empty; it names a chord of this engine's vocabulary "
				"(chord.progression_list lists them)"));
		return false;
	}
	tick_t length = 0;
	tick_t root = 0;
	tick_t octave = 4;
	if (!readOptionalTicks(args, QStringLiteral("length"), 0, MaxSchemaInteger, &length, error)
		|| !readOptionalTicks(args, QStringLiteral("root"), 0, 11, &root, error)
		|| !readOptionalTicks(args, QStringLiteral("octave"), ChordVocabulary::MinOctave,
			ChordVocabulary::MaxOctave, &octave, error))
	{
		return false;
	}
	event.length = length;
	event.root = static_cast<int>(root);
	event.octave = static_cast<int>(octave);
	event.scale = args.value(QStringLiteral("scale")).toString();

	QString reason;
	if (!ChordTrack::isWritable(event, &reason))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, reason);
		return false;
	}
	*out = event;
	return true;
}


bool readDetectOptions(const QJsonObject& args, ChordDetect::DetectOptions* out,
	ControlResult* error)
{
	ControlResult scratch;
	if (error == nullptr) { error = &scratch; }
	ChordDetect::DetectOptions options;
	tick_t window = 0;
	tick_t minClasses = 2;
	if (!readOptionalTicks(args, QStringLiteral("window_ticks"), 0, DefaultTicksPerBar, &window,
			error)
		|| !readOptionalTicks(args, QStringLiteral("min_pitch_classes"), 1, 13, &minClasses,
			error))
	{
		return false;
	}
	options.windowTicks = window;
	options.minPitchClasses = static_cast<int>(minClasses);
	*out = options;
	return true;
}


/*! The KEY half of a generation request: the scale NAME (proved against the
 *  vocabulary here, so a caller is told, typed, that the scale does not exist),
 *  the root, the octave, where the walk starts and how long each chord lasts.
 *  One helper per group of refusals (Gate 4's complexity ratchet). */
bool readProgressionKey(const QJsonObject& args, ChordProgression::Request* request,
	ControlResult* error)
{
	request->scale = args.value(QStringLiteral("scale")).toString();
	if (request->scale.isEmpty()) { request->scale = QStringLiteral("Major"); }
	if (ChordVocabulary::scaleByName(request->scale) == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("'%1' is not a scale of this engine's vocabulary (chord."
				"progression_list lists its %2)")
				.arg(request->scale).arg(ChordVocabulary::scaleNames().size()));
		return false;
	}
	tick_t root = request->root;
	tick_t octave = request->octave;
	tick_t start = request->start;
	tick_t step = request->stepTicks;
	if (!readOptionalTicks(args, QStringLiteral("root"), 0, 11, &root, error)
		|| !readOptionalTicks(args, QStringLiteral("octave"), ChordVocabulary::MinOctave,
			ChordVocabulary::MaxOctave, &octave, error)
		|| !readOptionalTicks(args, QStringLiteral("tick"), 0, MaxSchemaInteger, &start, error)
		|| !readOptionalTicks(args, QStringLiteral("step_ticks"), 1, MaxSchemaInteger, &step, error))
	{
		return false;
	}
	request->root = static_cast<int>(root);
	request->octave = static_cast<int>(octave);
	request->start = start;
	request->stepTicks = step;
	return true;
}


/*! The SHAPE half: how many chords, the pattern they are laid out under, their
 *  velocity, the seed and the variation - same seam as the key half above. */
bool readProgressionShape(const QJsonObject& args, ChordProgression::Request* request,
	ControlResult* error)
{
	tick_t steps = request->steps;
	tick_t velocity = request->velocity;
	tick_t seed = 0;
	if (!readOptionalTicks(args, QStringLiteral("steps"), 1, ChordProgression::MaxSteps, &steps,
			error)
		|| !readOptionalTicks(args, QStringLiteral("velocity"), MinVolume, MaxVolume, &velocity,
			error)
		|| !readOptionalTicks(args, QStringLiteral("seed"), 0, MaxSchemaInteger, &seed, error))
	{
		return false;
	}
	request->steps = static_cast<int>(steps);
	request->velocity = static_cast<int>(velocity);
	request->seed = static_cast<uint32_t>(seed);

	if (args.contains(QStringLiteral("variation")))
	{
		const double value = args.value(QStringLiteral("variation")).toDouble(-1.0);
		if (value < 0.0 || value > 1.0)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'variation' is %1; it is how far the seeded draws may move a "
					"chord, so it is a fraction in 0..1 (0 = the progression exactly)")
					.arg(value));
			return false;
		}
		request->variation = static_cast<float>(value);
	}
	if (!args.contains(QStringLiteral("pattern"))) { return true; }
	const QString pattern = args.value(QStringLiteral("pattern")).toString();
	if (ChordProgression::patternFromName(pattern, &request->pattern)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'pattern' is '%1'; it is one of %2").arg(pattern)
			.arg(ChordProgression::patternNames().join(QStringLiteral(", "))));
	return false;
}


bool readProgressionRequest(const QJsonObject& args, ChordProgression::Request* out,
	ControlResult* error)
{
	ControlResult scratch;
	if (error == nullptr) { error = &scratch; }

	ChordProgression::Request request;
	request.progression = args.value(QStringLiteral("progression")).toString();
	const ChordProgression::Entry* entry = ChordProgression::find(request.progression);
	if (entry == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no progression called '%1' (this engine knows %2: %3)")
				.arg(request.progression).arg(ChordProgression::progressionNames().size())
				.arg(ChordProgression::progressionNames().join(QStringLiteral(", "))));
		return false;
	}
	// Absent, `steps` is the progression's own length: one pass of the walk,
	// which is what "generate this progression" means without a repeat count.
	request.steps = static_cast<int>(entry->degrees.size());
	if (!readProgressionKey(args, &request, error)) { return false; }
	if (!readProgressionShape(args, &request, error)) { return false; }
	*out = request;
	return true;
}


int writeNoteSpecs(MidiClip* clip, const std::vector<ChordProgression::NoteSpec>& specs)
{
	if (clip == nullptr) { return 0; }
	int written = 0;
	for (const ChordProgression::NoteSpec& spec : specs)
	{
		Note fresh(TimePos(spec.length), TimePos(spec.pos), spec.key,
			static_cast<volume_t>(spec.velocity));
		// quant_pos = false: the ticks a generator computed are the ticks it
		// means, the rule note.add follows.
		Note* added = clip->addNote(fresh, false);
		if (added == nullptr) { continue; }
		added->setSelected(false);
		++written;
	}
	clip->rearrangeAllNotes();
	clip->dataChanged();
	return written;
}


int clearClipNotes(MidiClip* clip)
{
	if (clip == nullptr) { return 0; }
	const int count = static_cast<int>(clip->notes().size());
	if (count > 0)
	{
		clip->clearNotes();
		clip->dataChanged();
	}
	return count;
}


std::vector<int> eventKeys(const ChordEvent& event)
{
	std::vector<int> keys;
	const ChordVocabulary::Chord* entry = ChordVocabulary::chordByName(event.chord);
	if (entry == nullptr) { return keys; }
	const int base = ChordVocabulary::keyOf(event.root, event.octave);
	for (const int offset : ChordVocabulary::toneOffsets(*entry))
	{
		int key = base + offset;
		if (key < 0) { key = 0; }
		if (key > NumKeys - 1) { key = NumKeys - 1; }
		keys.push_back(key);
	}
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	return keys;
}

} // namespace control

} // namespace lmms
