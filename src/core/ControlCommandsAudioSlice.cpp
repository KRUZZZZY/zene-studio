/*
 * ControlCommandsAudioSlice.cpp - clip.transients, clip.split_at, clip.slice_to_instrument
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

/*! 0.5.0 L05: slicing audio. clip.transients answers where a sample clip's audio has its hits
 *  (ED-150); clip.split_at cuts a clip at its transients, every beat or bar, the markers or given
 *  positions (ED-163, one undo step - the track's checkpoint); clip.slice_to_instrument loads the
 *  clip's audio into a new SlicerT track cut at its transients or beats, with a MIDI clip that plays
 *  the slices back in place (ED-162, one undo step that removes the track). */

#include <QDomDocument>
#include <QJsonArray>

#include "ArrangementEdits.h"
#include "ClipTransients.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlTrackCreation.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "ProjectMarkers.h"
#include "SampleClip.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxSlices = 128;
//! Slices an instrument can play: slice i + 1 sits at the new track's base key + i + 2, which must stay a
//! MIDI key (review L05-L09-L08-1 #10).
constexpr int MaxInstrumentSlices = NumKeys - DefaultBaseKey - 2;

int ticksPerBeat() { return DefaultTicksPerBar / std::max(1, Engine::getSong()->getTimeSigModel().getDenominator()); }

//! Song ticks every @a step from the song's start, strictly inside (@a start, @a end).
std::vector<tick_t> gridInside(tick_t start, tick_t end, int step)
{
	std::vector<tick_t> out;
	for (tick_t t = (start / step + 1) * step; t < end; t += step) { out.push_back(t); }
	return out;
}

//! `at: "positions"`: the song ticks @a args names, each one a whole number.
//! toInt() answers 0 for anything it cannot convert, and cutPoints' out-of-range
//! filter drops that 0 as quietly as it drops a real cut past the end - so a call
//! naming one bad entry used to succeed having cut at the good entries only. An
//! entry that is not a whole number of ticks is a caller error, and is refused
//! here, before anything is cut.
bool askedTicks(const QJsonObject& args, std::vector<tick_t>* out, ControlResult* error)
{
	const QJsonArray asked = args.value(QStringLiteral("positions")).toArray();
	for (int i = 0; i < asked.size(); ++i)
	{
		const QJsonValue& value = asked.at(i);
		if (!value.isDouble() || static_cast<double>(value.toInt()) != value.toDouble())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'positions' entry %1 is not a whole number of song ticks").arg(i));
			return false;
		}
		out->push_back(value.toInt());
	}
	return true;
}

//! The song ticks @a args name for a cut of @a clip, or false (with @a error).
bool cutPoints(Clip* clip, const QJsonObject& args, std::vector<tick_t>* out, ControlResult* error)
{
	const QString at = args.value(QStringLiteral("at")).toString();
	const tick_t start = clip->startPosition().getTicks();
	const tick_t end = clip->endPosition().getTicks();
	if (at == QStringLiteral("beats")) { *out = gridInside(start, end, ticksPerBeat()); }
	else if (at == QStringLiteral("bars")) { *out = gridInside(start, end, TimePos::ticksPerBar()); }
	else if (at == QStringLiteral("markers"))
	{
		for (const Marker& marker : ProjectMarkers::instance().markers()) { out->push_back(marker.tick); }
	}
	else if (at == QStringLiteral("positions"))
	{
		// A whole number outside the clip is still ignored below, as this command documents.
		if (!askedTicks(args, out, error)) { return false; }
	}
	else
	{
		auto* sample = dynamic_cast<SampleClip*>(clip);
		if (sample == nullptr)
		{
			*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("transients belong to sample clips"));
			return false;
		}
		const tick_t origin = start + clip->startTimeOffset().getTicks();
		for (const int tick : clipTransientTicks(*sample)) { out->push_back(origin + tick); }
	}
	std::sort(out->begin(), out->end());
	out->erase(std::unique(out->begin(), out->end()), out->end());
	out->erase(std::remove_if(out->begin(), out->end(), [start, end](tick_t t) { return t <= start || t >= end; }), out->end());
	return true;
}

ControlResult transients(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	auto* sample = dynamic_cast<SampleClip*>(ref.clip);
	if (sample == nullptr) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("transients belong to sample clips")); }
	const std::vector<f_cnt_t> frames = clipTransientFrames(*sample);
	const std::vector<int> ticks = clipTransientTicks(*sample);
	QJsonArray rows;
	for (std::size_t i = 0; i < frames.size() && i < ticks.size(); ++i)
	{
		rows.append(QJsonObject{{QStringLiteral("frame"), static_cast<qint64>(frames[i])}, {QStringLiteral("tick"), ticks[i]}});
	}
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("transients"), rows},
		{QStringLiteral("count"), rows.size()}});
}

ControlResult splitAt(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	std::vector<tick_t> points;
	if (!cutPoints(ref.clip, args, &points, &error)) { return error; }
	if (points.empty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("nothing to cut at inside the clip")); }
	if (points.size() >= static_cast<std::size_t>(MaxSlices))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("%1 cuts would make more than %2 pieces; "
			"cut a shorter clip or at a coarser grid").arg(points.size()).arg(MaxSlices));
	}
	addUndoStep(QVector<JournallingObject*>{ref.track});
	// From the last cut to the first: the clip itself stays the leftmost piece.
	int pieces = 1;
	for (auto it = points.rbegin(); it != points.rend(); ++it)
	{
		if (arrangement::splitClipAt(ref.clip, *it) != nullptr) { ++pieces; }
	}
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("pieces"), pieces}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("clip"), clipId(ref.id)}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("Track checkpoint: undo puts the uncut clip back")));
	return ControlResult::success(result);
}

//! SlicerT's state for @a sample cut at @a frames (source frames, ascending): points normalised to
//! the whole sample, 0 first and 1 last, synced off so the slices play at their own speed.
QDomElement slicerState(QDomDocument& doc, SampleClip& clip, const std::vector<f_cnt_t>& frames)
{
	QDomElement state = doc.createElement(QStringLiteral("slicert"));
	const Sample& sample = clip.sample();
	state.setAttribute(QStringLiteral("version"), QStringLiteral("1"));
	state.setAttribute(QStringLiteral("src"), sample.sampleFile());
	if (sample.sampleFile().isEmpty()) { state.setAttribute(QStringLiteral("sampledata"), sample.toBase64()); }
	const double size = static_cast<double>(std::max<std::size_t>(1, sample.sampleSize()));
	std::vector<double> points{0.0};
	for (const f_cnt_t frame : frames) { points.push_back(frame / size); }
	points.push_back(1.0);
	state.setAttribute(QStringLiteral("totalSlices"), static_cast<int>(points.size()));
	for (std::size_t i = 0; i < points.size(); ++i) { state.setAttribute(QStringLiteral("slice_%1").arg(i), points[i]); }
	state.setAttribute(QStringLiteral("syncEnable"), 0);
	return state;
}

//! The source frames a slice starts at: the clip's transients, or every beat of its window - the
//! first at the clip's in-point, so the opening beat is a slice that plays too (gridInside starts after it).
std::vector<f_cnt_t> sliceFrames(SampleClip& clip, const QString& at)
{
	if (at != QStringLiteral("beats")) { return clipTransientFrames(clip); }
	// A clip with no audio has nothing to slice, as on the transients path (an empty result is NotFound).
	const SampleWindow window = clip.sampleWindow();
	if (clip.sample().sampleSize() == 0 || window.sourceOut <= window.sourceIn) { return {}; }
	std::vector<f_cnt_t> out{clip.sourceFrameAt(clip.startPosition())};
	const tick_t start = clip.startPosition().getTicks();
	for (const tick_t tick : gridInside(start, clip.endPosition().getTicks(), ticksPerBeat()))
	{
		out.push_back(clip.sourceFrameAt(TimePos(tick)));
	}
	return out;
}

//! The new track: SlicerT holding @a sample's audio cut at @a frames, and a MIDI clip that plays each slice where it sits.
InstrumentTrack* buildSliceTrack(SampleClip& clip, const std::vector<f_cnt_t>& frames, ControlResult* error)
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	Instrument* slicer = track != nullptr ? track->loadInstrument(QStringLiteral("slicert")) : nullptr;
	if (slicer == nullptr || slicer->nodeName() != QStringLiteral("slicert"))
	{
		if (track != nullptr) { removeTrack(track); }
		*error = ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("this build has no SlicerT instrument"));
		return nullptr;
	}
	QDomDocument doc;
	slicer->restoreState(slicerState(doc, clip, frames));
	track->setName(QStringLiteral("%1 slices").arg(clip.name().isEmpty() ? QStringLiteral("Audio") : clip.name()));
	auto* notes = dynamic_cast<MidiClip*>(track->createClip(clip.startPosition()));
	// The MIDI clip starts where the audio clip does, so notes sit at song ticks less its start (timelinePosAt is
	// already a song position - it carries the clip's time offset - so the offset is not taken off again).
	const tick_t origin = clip.startPosition().getTicks();
	const tick_t end = clip.endPosition().getTicks() - origin;
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		const tick_t at = clip.timelinePosAt(frames[i]).getTicks() - origin;
		const tick_t next = i + 1 < frames.size() ? clip.timelinePosAt(frames[i + 1]).getTicks() - origin : end;
		if (at < 0 || next <= at) { continue; }
		// Slice i + 1 of SlicerT's list (slice 0 is the head before the first cut) sits at key base + i + 2.
		notes->addNote(Note(TimePos(next - at), TimePos(at), track->baseNote() + static_cast<int>(i) + 2), false);
	}
	notes->rearrangeAllNotes();
	notes->updateLength();
	return track;
}

ControlResult sliceToInstrument(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	auto* sample = dynamic_cast<SampleClip*>(ref.clip);
	if (sample == nullptr) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("slicing belongs to sample clips")); }
	std::vector<f_cnt_t> frames = sliceFrames(*sample, args.value(QStringLiteral("at")).toString());
	if (frames.empty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("nothing to slice at in the clip")); }
	const bool capped = frames.size() > static_cast<std::size_t>(MaxInstrumentSlices);
	if (capped) { frames.resize(MaxInstrumentSlices); }
	InstrumentTrack* track = buildSliceTrack(*sample, frames, &error);
	if (track == nullptr) { return error; }
	// One undo step removes the track; redo rebuilds it from what it was saved as (ControlTrackCreation.h).
	recordCreatedTrack(track);
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("track"), trackIdOf(track)},
		{QStringLiteral("slices"), static_cast<int>(frames.size()) + 1}, {QStringLiteral("capped"), capped}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("clip"), clipId(ref.id)}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: the recorded undo step removes the created track")));
	return ControlResult::success(result);
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("clip.") + QLatin1String(verb), description, args, mutating, handler);
}

} // namespace

void registerAudioSliceCommands(ControlRegistry& registry)
{
	const QJsonObject clipOnly = objectSchema({{QStringLiteral("clip"), stringProperty()}}, {QStringLiteral("clip")});
	add(registry, "transients", QStringLiteral("Where a sample clip's audio has its hits: [{frame (source), tick (from the "
		"clip's start, where it plays now)}] - the onset rule detect.* uses."), clipOnly, false, transients);
	add(registry, "split_at", QStringLiteral("Cut a clip into pieces: 'at' transients (sample clips), beats, bars, markers "
		"or positions ('positions', song ticks); cuts outside the clip are ignored. One undo step (track checkpoint)."),
		objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("at"), enumProperty({QStringLiteral("transients"),
			QStringLiteral("beats"), QStringLiteral("bars"), QStringLiteral("markers"), QStringLiteral("positions")})},
			{QStringLiteral("positions"), arrayProperty()}}, {QStringLiteral("clip"), QStringLiteral("at")}), true, splitAt);
	add(registry, "slice_to_instrument", QStringLiteral("Slice a sample clip's audio into a new SlicerT track - cut 'at' its "
		"transients (default) or every beat - with a MIDI clip at the same place that plays each slice where it sat, so "
		"the loop sounds as before and can be re-sequenced (at most 69 slices, so every slice has a MIDI key - 'capped' "
		"says the rest were left out). One undo step removes the track."),
		objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("at"), enumProperty({QStringLiteral("transients"),
			QStringLiteral("beats")})}}, {QStringLiteral("clip")}), true, sliceToInstrument);
}

} // namespace lmms
