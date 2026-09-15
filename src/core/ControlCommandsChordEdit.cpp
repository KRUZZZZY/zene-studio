/*
 * ControlCommandsChordEdit.cpp - the TRACK-editing half of the chord.* command
 *                                group (SPEC A11-A16): chord.set, chord.remove,
 *                                chord.clear and chord.detect_to_track.
 *
 * ONE MECHANISM, for all four: these edit the CHORD TRACK, which is project state
 * the Song's journal checkpoint does not carry (see ControlChordSupport.h), so
 * each records an ACTION checkpoint - the <chord-track> element captured before
 * the edit, written back by the recorded step. The group's two GENERATORS
 * (chord.track_write and chord.progression_generate) write NOTES into a MidiClip
 * instead, so their inverse is the clip's own journal checkpoint: they are the
 * other half of this split, in ControlCommandsChordWrite.cpp.
 *
 * The arithmetic is engine code and is NOT here: detectChords lives in
 * src/core/ChordDetect.cpp, the track itself in src/core/ChordTrack.cpp, and
 * include/ChordVocabulary.h is the read-only view over the chord and scale
 * vocabulary this product already has. What this file owns is the surface: the
 * schemas, the refusals, and the one-command-one-undo-step bookkeeping.
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

//! The octave a key sounds in: keyOf()'s inverse (Note.h's own convention).
int octaveOfKey(int key)
{
	int octave = key / 12 - 1;
	if (octave < ChordVocabulary::MinOctave) { octave = ChordVocabulary::MinOctave; }
	if (octave > ChordVocabulary::MaxOctave) { octave = ChordVocabulary::MaxOctave; }
	return octave;
}


//! The refusal for a clip that carries no notes: there is nothing to detect
//! and nothing to replace, and one message says both.
ControlResult noNotesRefusal(const QString& clip)
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1 has no notes: a chord is detected from notes that sound "
			"together, so there is nothing here to read").arg(clip));
}


ControlResult chordSet(const QJsonObject& args)
{
	ChordTrack* track = projectChordTrack();
	if (track == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no chord track to write"));
	}
	ControlResult error;
	ChordEvent event;
	if (!readChordEvent(args, &event, &error)) { return error; }

	const ChordEvent* existing = track->findAt(event.pos);
	const bool hadPrevious = existing != nullptr;
	const ChordEvent previous = hadPrevious ? *existing : ChordEvent();

	const QString before = track->toXml();
	bool replaced = false;
	if (!track->set(event, &replaced))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the chord track already holds its maximum of %1 chords and has "
				"none at tick %2 to replace; remove one first")
				.arg(ChordTrack::MaxEvents).arg(static_cast<qint64>(event.pos)));
	}
	recordChordTrackRestore(before);

	QJsonObject result = chordTrackState(*track);
	result.insert(QStringLiteral("event"), chordEventJson(event));
	result.insert(QStringLiteral("replaced"), replaced);
	// The recorded inverse is a call a reader can re-issue: the previous chord
	// at that position, or the removal of the one this call created.
	result.insert(QStringLiteral("__transaction"), chordTrackInverse(before,
		hadPrevious ? QStringLiteral("chord.set") : QStringLiteral("chord.remove"),
		hadPrevious ? chordEventArgs(previous)
			: QJsonObject{{QStringLiteral("pos"), static_cast<qint64>(event.pos)}}));
	return ControlResult::success(result);
}


ControlResult chordRemove(const QJsonObject& args)
{
	ChordTrack* track = projectChordTrack();
	if (track == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no chord track to write"));
	}
	ControlResult error;
	tick_t pos = 0;
	if (!readTicks(args, QStringLiteral("pos"), 0, MaxSchemaInteger, &pos, &error)) { return error; }

	const ChordEvent* existing = track->findAt(pos);
	if (existing == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("there is no chord at tick %1 (the track holds %2)")
				.arg(static_cast<qint64>(pos)).arg(track->size()));
	}
	const ChordEvent removed = *existing;
	const QString before = track->toXml();
	track->removeAt(pos);
	recordChordTrackRestore(before);

	QJsonObject result = chordTrackState(*track);
	result.insert(QStringLiteral("event"), chordEventJson(removed));
	result.insert(QStringLiteral("removed"), true);
	result.insert(QStringLiteral("__transaction"),
		chordTrackInverse(before, QStringLiteral("chord.set"), chordEventArgs(removed)));
	return ControlResult::success(result);
}


ControlResult chordClear()
{
	ChordTrack* track = projectChordTrack();
	if (track == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no chord track to write"));
	}
	// A clear that would change nothing is REFUSED rather than recorded as an
	// undo step for a no-op: the track is one element, and its empty state is
	// already what the wire reports.
	if (track->empty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the chord track is already empty"));
	}
	const int removed = track->size();
	const QString before = track->toXml();
	track->clear();
	recordChordTrackRestore(before);

	QJsonObject result = chordTrackState(*track);
	result.insert(QStringLiteral("removed"), removed);
	result.insert(QStringLiteral("__transaction"), chordTrackInverse(before,
		QStringLiteral("control.undo"),
		QJsonObject{{QStringLiteral("command"), QStringLiteral("chord.clear")}}));
	return ControlResult::success(result);
}


/*! Turns ONE detected slice into a chord-track event and stores it.
 *
 *  False when the slice has no name this engine can hold (a real answer from
 *  the vocabulary, not an error) or when the track refused the event - the
 *  caller counts either as `skipped`, so nothing is silently dropped. Shared by
 *  the write loop so the converter is one definition. */
bool writeDetectedMatch(ChordTrack* track, const ChordDetect::ChordMatch& match)
{
	if (match.chord.isEmpty() || match.root < 0) { return false; }
	ChordEvent event;
	event.pos = match.pos;
	event.length = 0;
	event.root = match.root;
	event.octave = octaveOfKey(match.rootKey);
	event.chord = match.chord;
	event.scale = match.scale;
	return track->set(event, nullptr);
}


ControlResult chordDetectToTrack(const QJsonObject& args)
{
	ChordTrack* track = projectChordTrack();
	if (track == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no chord track to write"));
	}
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	if (clip->notes().empty()) { return noNotesRefusal(clipId(ref.id)); }
	ChordDetect::DetectOptions options;
	if (!readDetectOptions(args, &options, &error)) { return error; }
	const bool append = args.value(QStringLiteral("append")).toBool(false);

	const std::vector<ChordDetect::ChordMatch> matches =
		ChordDetect::detectChords(clip->notes(), options);
	const QString before = track->toXml();
	if (!append) { track->clear(); }

	int written = 0;
	int skipped = 0;
	for (const ChordDetect::ChordMatch& match : matches)
	{
		// A slice with no name is a real answer (the vocabulary has no entry for
		// it) and a chord-track event cannot be nameless, so it is counted and
		// not written - reported as `skipped`, never silently dropped.
		if (writeDetectedMatch(track, match)) { ++written; }
		else { ++skipped; }
	}
	if (written == 0)
	{
		// Nothing was written, so nothing is left half-done: put the track back
		// exactly as it was and refuse, typed. A recorded undo step for a write
		// that did not happen would be a false record.
		track->fromXml(before);
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("nothing to write: %1 of %2 slice(s) has a chord this engine's "
				"vocabulary can name (try min_pitch_classes, or window_ticks if the take is "
				"not on the grid)").arg(skipped).arg(static_cast<int>(matches.size())));
	}
	recordChordTrackRestore(before);

	QJsonObject result = chordTrackState(*track);
	result.insert(QStringLiteral("clip"), clipId(ref.id));
	result.insert(QStringLiteral("clip_track"), trackIdOf(ref.track));
	result.insert(QStringLiteral("detected"), static_cast<int>(matches.size()));
	result.insert(QStringLiteral("written"), written);
	result.insert(QStringLiteral("skipped"), skipped);
	result.insert(QStringLiteral("appended"), append);
	result.insert(QStringLiteral("__transaction"), chordTrackInverse(before,
		QStringLiteral("control.undo"),
		QJsonObject{{QStringLiteral("command"), QStringLiteral("chord.detect_to_track")}}));
	return ControlResult::success(result);
}


/*! The clip-writing half both generators share: the arguments of the write, the
 *  before-state, the checkpoint and the replace. Split out so neither generator
 *  repeats the bookkeeping (and so both report the same numbers). */

} // namespace

} // namespace control


void registerChordEditCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("chord.set");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("set");
		cmd.description = QStringLiteral("Write ONE chord into the project's chord track at an "
			"absolute tick: the chord's name (a chord of this engine's vocabulary - "
			"chord.progression_list lists them), its root pitch class, the octave the root "
			"sounds in, and optionally its length. The POSITION is the key: a set at a tick "
			"that already holds a chord REPLACES it (the command reports `replaced`), so a "
			"track cannot hold two chords at once. 'length' 0 (the default) means HOLD - the "
			"chord sounds until the next one, which is what a chord track means. Reversible: "
			"one recorded action checkpoint (the track is project state the Song's journal "
			"checkpoint does not carry), so one control.undo takes the write back.");
		cmd.argsSchema = chordEventArgsSchema();
		cmd.resultSchema = chordTrackStateSchema({
			{QStringLiteral("event"), chordEventSchema()},
			{QStringLiteral("replaced"), booleanProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return chordSet(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("chord.remove");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("remove");
		cmd.description = QStringLiteral("Remove the chord at an absolute tick from the "
			"project's chord track, reporting the event that went. A tick that holds no "
			"chord is a typed not_found, never a silent success. Reversible: one recorded "
			"action checkpoint, and the recorded inverse names chord.set with the removed "
			"event's own arguments.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("pos"), tickProperty()},
		}, {QStringLiteral("pos")});
		cmd.resultSchema = chordTrackStateSchema({
			{QStringLiteral("event"), chordEventSchema()},
			{QStringLiteral("removed"), booleanProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return chordRemove(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("chord.clear");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("clear");
		cmd.description = QStringLiteral("Empty the project's chord track, reporting how many "
			"chords went. An already-empty track is REFUSED, typed: a clear that would change "
			"nothing must not leave an undo step behind. Reversible: one recorded action "
			"checkpoint holding the whole <chord-track> element as it was.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = chordTrackStateSchema({
			{QStringLiteral("removed"), integerProperty(0, ChordTrack::MaxEvents)},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject&) { return chordClear(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("chord.detect_to_track");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("detect_to_track");
		cmd.description = QStringLiteral("Detect the chords a MIDI clip's notes spell (the "
			"same detection chord.detect reports) and write them onto the project's chord "
			"track. By default the track is REPLACED; 'append' adds to what is there. A slice "
			"with no name in this engine's vocabulary cannot be a chord-track event and is "
			"reported as `skipped` - and if nothing at all could be written the track is left "
			"EXACTLY as it was and the call is refused, typed, rather than half-written. "
			"Reversible: one recorded action checkpoint.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("window_ticks"), integerProperty(0, DefaultTicksPerBar)},
			{QStringLiteral("min_pitch_classes"), integerProperty(1, 13)},
			{QStringLiteral("append"), booleanProperty()},
		}, {QStringLiteral("clip")});
		cmd.resultSchema = chordTrackStateSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("clip_track"), stringProperty()},
			{QStringLiteral("detected"), integerProperty(0, MaxSchemaInteger)},
			{QStringLiteral("written"), integerProperty(0, MaxSchemaInteger)},
			{QStringLiteral("skipped"), integerProperty(0, MaxSchemaInteger)},
			{QStringLiteral("appended"), booleanProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return chordDetectToTrack(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
