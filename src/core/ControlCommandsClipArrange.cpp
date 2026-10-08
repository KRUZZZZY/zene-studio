/*
 * ControlCommandsClipArrange.cpp - clip.repeat and clip.time_scale
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

/*! 0.5.0 L04: ED-030 repeat a clip back to back (clip.repeat - Duplicate is a count of one) and
 *  ED-032 scale a clip's content in time (clip.time_scale): a MIDI clip's notes move and stretch
 *  with it, a sample clip plays at the matching rate (it leads with a source tempo, pitch kept
 *  when its stretch mode keeps it). */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <QJsonArray>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ClipLinks.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxRepeats = 64;
constexpr double MinFactor = 0.125;
constexpr double MaxFactor = 8.0;

ControlResult repeatClip(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const int count = args.value(QStringLiteral("count")).toInt(1);
	ref.track->addJournalCheckPoint();
	QJsonArray copies;
	tick_t next = ref.clip->endPosition().getTicks();
	const tick_t length = ref.clip->length().getTicks();
	// ED-043: with ripple on, what follows the clip moves later to make room for the copies.
	if (arrangement::modes().ripple != arrangement::Ripple::Off)
	{
		arrangement::checkpointRipple(ref.track);
		arrangement::rippleShift(ref.track, next, count * length);
	}
	for (int i = 0; i < count; ++i)
	{
		Clip* copy = ref.clip->clone();
		copy->movePosition(TimePos(next));
		copies.append(clipIdOf(copy));
		next += length;
	}
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("copies"), copies}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("clip"), clipId(ref.id)}},
		QStringLiteral("control.undo"), {}, true,  // clip.delete takes one clip, and would not undo a ripple
		QStringLiteral("Track journal checkpoint: undo removes every copy")));
	return ControlResult::success(result);
}

void scaleNotes(MidiClip* clip, double factor)
{
	for (Note* note : clip->notes())
	{
		note->setPos(TimePos(static_cast<tick_t>(std::lround(note->pos().getTicks() * factor))));
		note->setLength(TimePos(std::max<tick_t>(1, static_cast<tick_t>(std::lround(note->length().getTicks() * factor)))));
	}
	clip->rearrangeAllNotes();
}

//! Whether @a tick, computed in wide arithmetic, is a tick a controller point can store.
bool fitsTick(long long tick)
{
	return tick >= 0 && tick <= static_cast<long long>(std::numeric_limits<tick_t>::max());
}

//! Controller points: each must scale onto a storable tick.
bool scaledControllersFit(const MidiClip* clip, double factor)
{
	for (const ControllerLane& lane : clip->controllers().lanes())
	{
		for (const ControllerPoint& point : lane.points)
		{
			if (!fitsTick(std::llround(static_cast<double>(point.tick) * factor))) { return false; }
		}
	}
	return true;
}

//! Preflight for a time scale, in wide arithmetic before the checkpoint, so a refused scale mutates nothing: every
//! tick it writes must be storable - each note's start and end (a note is at least 1 long, as scaleNotes writes it),
//! each controller point, and the clip's own length (the same rounding timeScale uses).
bool scaleFits(const Clip* clip, double factor)
{
	const auto scaled = [factor](tick_t tick) { return std::llround(static_cast<double>(tick) * factor); };
	if (!fitsTick(std::max<long long>(1, scaled(clip->length().getTicks())))) { return false; }
	const auto* midi = dynamic_cast<const MidiClip*>(clip);
	if (midi == nullptr) { return true; }
	for (const Note* note : midi->notes())
	{
		const long long pos = scaled(note->pos().getTicks());
		const long long length = std::max<long long>(1, scaled(note->length().getTicks()));
		if (!fitsTick(pos) || !fitsTick(pos + length)) { return false; }
	}
	return scaledControllersFit(midi, factor);
}

//! Controller points carry clip-time ticks, exactly like note positions, so a time scale moves
//! them by the same factor and the same rounding. Points that scale onto the same tick collapse
//! (setPoints keeps one per tick) - the same bounds policy the notes get from their >= 1 length.
//! The caller has checked scaleFits, so every scaled tick is storable.
void scaleControllers(MidiClip* clip, double factor)
{
	for (const ControllerLane& lane : clip->controllers().lanes())
	{
		std::vector<ControllerPoint> points;
		points.reserve(lane.points.size());
		for (const ControllerPoint& point : lane.points)
		{
			points.push_back({static_cast<tick_t>(std::llround(static_cast<double>(point.tick) * factor)), point.value});
		}
		clip->setControllerPoints(lane.kind, lane.controller, std::move(points));
	}
}

ControlResult timeScale(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const double factor = args.value(QStringLiteral("factor")).toDouble();
	if (factor < MinFactor || factor > MaxFactor)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'factor' must be %1..%2").arg(MinFactor).arg(MaxFactor));
	}
	if (!scaleFits(ref.clip, factor))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("a tick would scale past the largest tick; nothing was changed"));
	}
	const tick_t before = ref.clip->length().getTicks();
	ref.clip->addJournalCheckPoint();
	if (auto* midi = dynamic_cast<MidiClip*>(ref.clip))
	{
		scaleNotes(midi, factor);
		scaleControllers(midi, factor);
		// Scaling writes the clip's own Note objects, so it writes the group's
		// shared content without passing a note entry point - the same seam
		// cluster 5689 measured on note.randomize. One mirror keeps every
		// member of a link group on the scaled notes (docs/LINKED-CLIPS.md §4).
		if (midi->linkId() > 0) { ClipLinks::mirrorContent(midi); }
	}
	else if (auto* sample = dynamic_cast<SampleClip*>(ref.clip))
	{
		// A source tempo of (project tempo x factor) makes the clip play 1/factor as fast: it leads.
		const double sourceTempo = (sample->warpTempoMode() == WarpTempoMode::SourceTempo ? sample->sourceTempo()
			: static_cast<double>(Engine::getSong()->getTempo())) * factor;
		sample->setWarpTempoMode(WarpTempoMode::SourceTempo);
		sample->setSourceTempo(static_cast<float>(sourceTempo));
	}
	ref.clip->changeLength(TimePos(std::max<tick_t>(1, static_cast<tick_t>(std::lround(before * factor)))));
	ref.clip->setAutoResize(false);
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("factor"), factor},
		{QStringLiteral("length"), ref.clip->length().getTicks()}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("length"), before}},
		// Not time_scale by 1/factor: that leaves the warp mode it set and rounds the notes twice.
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("Clip journal checkpoint: undo restores the clip's notes (or warp) and length")));
	return ControlResult::success(result);
}

//! The MIDI clips 'clips' names, all on one track, earliest first; false (with @a error) otherwise.
bool joinable(const QJsonArray& ids, QVector<MidiClip*>* clips, Track** track, ControlResult* error)
{
	for (const QJsonValue& id : ids)
	{
		ClipRef ref;
		MidiClip* clip = resolveMidiClip(id.toString(), &ref, error);
		if (clip == nullptr) { return false; }
		if (*track != nullptr && ref.track != *track)
		{
			*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clips to join must be on one track"));
			return false;
		}
		*track = ref.track;
		if (!clips->contains(clip)) { clips->append(clip); }
	}
	if (clips->size() < 2)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clips' names at least two MIDI clips"));
		return false;
	}
	std::sort(clips->begin(), clips->end(), [](const MidiClip* a, const MidiClip* b) { return a->startPosition() < b->startPosition(); });
	return true;
}

//! The start of @a clip in song ticks, time offset included, in 64-bit arithmetic: a join's moves are deltas of these.
long long wideSongStart(const MidiClip* clip)
{
	return static_cast<long long>(clip->startPosition().getTicks()) + clip->startTimeOffset().getTicks();
}

//! Preflight for a join: every note position and controller point of the later clips must land on a storable
//! song tick once moved by its clip's delta from the first clip's origin, in wide arithmetic.
bool joinedContentFits(const QVector<MidiClip*>& clips, long long origin)
{
	for (int i = 1; i < clips.size(); ++i)
	{
		const MidiClip* other = clips.at(i);
		const long long delta = wideSongStart(other) - origin;
		for (const Note* note : other->notes())
		{
			if (!fitsTick(static_cast<long long>(note->pos().getTicks()) + delta)) { return false; }
		}
		for (const ControllerLane& lane : other->controllers().lanes())
		{
			for (const ControllerPoint& point : lane.points)
			{
				if (!fitsTick(static_cast<long long>(point.tick) + delta)) { return false; }
			}
		}
	}
	return true;
}

//! The lanes of @a clip moved into @a first by @a delta ticks - the same song-time delta the
//! notes get. Points that land on the same tick collapse (one point per tick), and a lane that
//! already exists in @a first is merged into it rather than replacing it.
void mergeControllers(MidiClip* first, const MidiClip* other, tick_t delta)
{
	for (const ControllerLane& lane : other->controllers().lanes())
	{
		std::vector<ControllerPoint> points;
		const ControllerLane* existing = first->controllers().lane(lane.kind, lane.controller);
		if (existing != nullptr) { points = existing->points; }
		for (const ControllerPoint& point : lane.points)
		{
			points.push_back({point.tick + delta, point.value});
		}
		first->setControllerPoints(lane.kind, lane.controller, std::move(points));
	}
}

//! ED-031 (0.5.0 L05): glue MIDI clips into the first - their notes move in at their song positions,
//! the others go, and the first spans to the last one's end.
ControlResult joinClips(const QJsonObject& args)
{
	ControlResult error;
	QVector<MidiClip*> clips;
	Track* track = nullptr;
	if (!joinable(args.value(QStringLiteral("clips")).toArray(), &clips, &track, &error)) { return error; }
	MidiClip* first = clips.front();
	const long long origin = wideSongStart(first);
	if (!joinedContentFits(clips, origin))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a note or a controller point of a later clip "
			"would move past the largest tick; nothing was changed"));
	}
	addUndoStep(QVector<JournallingObject*>{track});
	tick_t end = first->endPosition().getTicks();
	for (int i = 1; i < clips.size(); ++i)
	{
		MidiClip* other = clips.at(i);
		// The move is the song delta, computed once in 64-bit arithmetic and added to each note's own position.
		// (The old order, from + pos - origin, overflowed tick_t in its middle term when the result still fit.)
		const long long delta = wideSongStart(other) - origin;
		for (const Note* note : other->notes())
		{
			Note moved = *note;
			moved.setPos(TimePos(static_cast<tick_t>(note->pos().getTicks() + delta)));  // the preflight checked it fits
			first->addNote(moved, false);
		}
		mergeControllers(first, other, static_cast<tick_t>(delta));
		end = std::max(end, other->endPosition().getTicks());
		arrangement::deleteClip(track, other);
	}
	first->rearrangeAllNotes();
	first->setAutoResize(false);
	first->changeLength(TimePos(end - first->startPosition().getTicks()));
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clip"), clipIdOf(first)}, {QStringLiteral("joined"), clips.size()}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("clip"), clipIdOf(first)}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("Track checkpoint: undo puts the separate clips back")));
	return ControlResult::success(result);
}

} // namespace

void registerClipArrangeCommands(ControlRegistry& registry)
{
	ControlCommand join;
	join.id = QStringLiteral("clip.join");
	join.group = QStringLiteral("clip");
	join.verb = QStringLiteral("join");
	join.description = QStringLiteral("Glue MIDI clips on one track into one: the earliest keeps its id and spans to the "
		"last one's end, the others' notes move into it at their song positions and the others are removed (sample clips "
		"are joined by clip.consolidate). One undo step (Track checkpoint).");
	join.argsSchema = objectSchema({{QStringLiteral("clips"), arrayProperty()}}, {QStringLiteral("clips")});
	join.resultSchema = objectProperty();
	join.mutating = true;
	join.handler = joinClips;
	registry.registerCommand(join);

	ControlCommand repeat;
	repeat.id = QStringLiteral("clip.repeat");
	repeat.group = QStringLiteral("clip");
	repeat.verb = QStringLiteral("repeat");
	repeat.description = QStringLiteral("Put 'count' copies of a clip (1..64, default 1 - Duplicate) right after it, back "
		"to back on its track; answers the copies' ids. With ripple on (arrangement.modes) what follows moves later to make "
		"room. Reversible through the ProjectJournal (Track checkpoint).");
	repeat.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("count"), integerProperty(1, MaxRepeats)}},
		{QStringLiteral("clip")});
	repeat.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("copies"), arrayProperty()}});
	repeat.mutating = true;
	repeat.handler = repeatClip;
	registry.registerCommand(repeat);

	ControlCommand scale;
	scale.id = QStringLiteral("clip.time_scale");
	scale.group = QStringLiteral("clip");
	scale.verb = QStringLiteral("time_scale");
	scale.description = QStringLiteral("Scale a clip's content in time by 'factor' (0.125..8; 2 = twice as long): a MIDI "
		"clip's note positions and lengths scale with it; a sample clip leads with a source tempo of the project tempo "
		"times the factor, so it plays at 1/factor speed (pitch kept when its stretch mode keeps it). The clip's length "
		"scales too. Reversible through the ProjectJournal (Clip checkpoint).");
	scale.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("factor"), numberProperty()}},
		{QStringLiteral("clip"), QStringLiteral("factor")});
	scale.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("factor"), numberProperty()},
		{QStringLiteral("length"), integerProperty()}});
	scale.mutating = true;
	scale.handler = timeScale;
	registry.registerCommand(scale);
}

} // namespace lmms
