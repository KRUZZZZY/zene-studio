/*
 * ControlCommandsArrangeTools.cpp - the clip list, tab to transient, remove silence
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
/*! 0.5.0 L04 (W3), working the arrangement:
 *  - clip.list (ED-036): every clip, or a track's, or those whose name holds a text - to find and pick
 *    one by name;
 *  - arrangement.next_transient (ED-025): the play position moves to the next (or previous) transient
 *    of the audio under it - tab to transient;
 *  - clip.remove_silence (ED-040): the stretches of a sample clip quieter than a threshold for longer
 *    than a length are cut out, as one undo step;
 *  - track.folder_move (ED-035): every clip inside a folder (its sub-folders too) moved together - the
 *    folder handled as one block of the song, one undo step; or, with a span, one of the folder's summary
 *    clips (FolderSummary.h) - the clips starting inside it. */

#include <algorithm>
#include <cmath>
#include <vector>

#include <QJsonArray>

#include "ArrangementEdits.h"
#include "ClipTransients.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleClip.h"
#include "ControlCommandsTrackFolderShared.h"
#include "Song.h"
#include "Track.h"
#include "TrackFolder.h"

namespace lmms
{

using namespace control;
using trackfoldercontrol::insideFolder;

namespace
{

QString kindOf(const Clip* clip)
{
	return dynamic_cast<const SampleClip*>(clip) != nullptr ? QStringLiteral("audio") : QStringLiteral("other");
}

ControlResult listClips(const QJsonObject& args)
{
	const QString onTrack = args.value(QStringLiteral("track")).toString();
	const QString text = args.value(QStringLiteral("name")).toString();
	const int limit = args.value(QStringLiteral("limit")).toInt(500);
	// A 'track' that names no live track is a typo, not an empty track: refuse it the way
	// automation.get_state (ControlCommandsAutomation.cpp:129) refuses a filter that matched
	// nothing, so a misspelt id cannot read as a successful answer about the wrong track.
	QJsonArray clips;
	bool matchedTrack = onTrack.isEmpty();
	for (const Track* track : Engine::getSong()->tracks())
	{
		if (!onTrack.isEmpty() && trackIdOf(track) != onTrack) { continue; }
		matchedTrack = true;
		for (const Clip* clip : track->getClips())
		{
			if (!text.isEmpty() && !clip->name().contains(text, Qt::CaseInsensitive)) { continue; }
			if (clips.size() >= limit) { break; }
			clips.append(QJsonObject{{QStringLiteral("clip"), clipIdOf(clip)}, {QStringLiteral("name"), clip->name()},
				{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("track_name"), track->name()},
				{QStringLiteral("start"), static_cast<int>(clip->startPosition().getTicks())},
				{QStringLiteral("length"), static_cast<int>(clip->length().getTicks())},
				{QStringLiteral("kind"), track->type() == Track::Type::Instrument ? QStringLiteral("midi") : kindOf(clip)}});
		}
	}
	if (!matchedTrack)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no track %1 (the song has %2)").arg(onTrack).arg(Engine::getSong()->tracks().size()));
	}
	return ControlResult::success({{QStringLiteral("clips"), clips}, {QStringLiteral("count"), clips.size()}});
}

//! The song ticks of the transients of every audio clip that sounds at @a tick.
std::vector<tick_t> transientsAround(tick_t tick)
{
	std::vector<tick_t> out;
	for (Track* track : Engine::getSong()->tracks())
	{
		for (Clip* clip : track->getClips())
		{
			auto* audio = dynamic_cast<SampleClip*>(clip);
			const tick_t clipStart = clip->startPosition().getTicks();
			const tick_t clipEnd = clip->endPosition().getTicks();
			if (audio == nullptr || tick < clipStart || tick >= clipEnd) { continue; }
			const tick_t origin = clip->startPosition().getTicks() + clip->startTimeOffset().getTicks();
			for (const int at : clipTransientTicks(*audio))
			{
				const tick_t mapped = origin + at;
				if (mapped >= clipStart && mapped < clipEnd) { out.push_back(mapped); }
			}
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

ControlResult nextTransient(const QJsonObject& args)
{
	const tick_t now = Engine::getSong()->getPlayPos().getTicks();
	const std::vector<tick_t> transients = transientsAround(now);
	const bool back = args.value(QStringLiteral("direction")).toString() == QLatin1String("previous");
	tick_t target = -1;
	if (back)
	{
		const auto it = std::lower_bound(transients.begin(), transients.end(), now);
		if (it != transients.begin()) { target = *std::prev(it); }
	}
	else
	{
		const auto it = std::upper_bound(transients.begin(), transients.end(), now);
		if (it != transients.end()) { target = *it; }
	}
	if (target < 0) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no transient that way in the audio under the play position")); }
	return ControlRegistry::instance()->invoke(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), static_cast<double>(target)}});
}

//! The quiet stretches of @a clip, as source-frame ranges: below @a level for at least @a minFrames.
std::vector<std::pair<f_cnt_t, f_cnt_t>> quietRuns(SampleClip& clip, float level, f_cnt_t minFrames)
{
	const SampleWindow window = clip.sampleWindow();
	const SampleFrame* data = clip.sample().data();
	std::vector<std::pair<f_cnt_t, f_cnt_t>> runs;
	if (data == nullptr || window.sourceOut <= window.sourceIn) { return runs; }
	// f_cnt_t is unsigned: a run is marked by its own flag, never a -1 start.
	bool inRun = false;
	f_cnt_t start = 0;
	for (f_cnt_t f = window.sourceIn; f <= window.sourceOut; ++f)
	{
		const bool quiet = f < window.sourceOut && std::max(std::fabs(data[f][0]), std::fabs(data[f][1])) < level;
		if (quiet && !inRun) { start = f; inRun = true; }
		if (!quiet && inRun)
		{
			if (f - start >= minFrames) { runs.emplace_back(start, f); }
			inRun = false;
		}
	}
	return runs;
}

//! Cut [@a from, @a to) (song ticks) out of @a clip on @a track; the clip itself stays the leftmost piece.
void cutOut(Track* track, Clip* clip, tick_t from, tick_t to)
{
	Clip* middle = from > clip->startPosition().getTicks() ? arrangement::splitClipAt(clip, from) : clip;
	if (middle == nullptr) { return; }
	arrangement::splitClipAt(middle, to);  // the rest after the quiet stretch stays
	arrangement::deleteClip(track, middle);
}

ControlResult removeSilence(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	if (clip == nullptr) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clip' is an audio clip")); }
	const float level = std::pow(10.f, static_cast<float>(args.value(QStringLiteral("threshold_db")).toDouble(-50.0)) / 20.f);
	const tick_t minTicks = std::max(1, args.value(QStringLiteral("min_length")).toInt(48));
	// min_length counts TICKS and quietRuns() counts frames of the clip's OWN sample, so the scalar
	// has to be the clip's: at the engine's rate a 96 kHz clip in a 48 kHz engine would cut half the
	// frames asked for. clipFramesPerTick() is where the clip's tempo mode enters the mapping.
	const auto minFrames = static_cast<f_cnt_t>(std::ceil(static_cast<double>(minTicks) * clip->clipFramesPerTick()));
	std::vector<std::pair<tick_t, tick_t>> cuts;
	for (const auto& [from, to] : quietRuns(*clip, level, minFrames))
	{
		cuts.emplace_back(clip->timelinePosAt(from).getTicks(), clip->timelinePosAt(to).getTicks());
	}
	if (cuts.empty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no stretch is that quiet for that long")); }
	addUndoStep(QVector<JournallingObject*>{ref.track});
	// From the last stretch back: a cut at the clip's very start deletes the clip itself, so it goes last.
	for (auto it = cuts.rbegin(); it != cuts.rend(); ++it) { cutOut(ref.track, clip, it->first, it->second); }
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("removed"), static_cast<int>(cuts.size())}});
}

//! Whether a clip starting at @a start is one the move takes: inside [@a from, @a to), or any with no span.
bool inSpan(tick_t start, tick_t from, tick_t to)
{
	return from < 0 || (start >= from && start < to);
}

//! The tracks inside @a folder holding clips the move takes, and the earliest start among those clips in @a earliest.
QVector<JournallingObject*> folderTracks(const TrackFolder* folder, tick_t from, tick_t to, tick_t* earliest)
{
	QVector<JournallingObject*> tracks;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (!insideFolder(track, folder)) { continue; }
		bool holds = false;
		for (const Clip* clip : track->getClips())
		{
			if (!inSpan(clip->startPosition().getTicks(), from, to)) { continue; }
			holds = true;
			*earliest = std::min<tick_t>(*earliest, clip->startPosition().getTicks());
		}
		if (holds) { tracks.append(track); }
	}
	return tracks;
}

//! ED-035's summary clip: 'start' and 'end' together limit the move to the clips starting in [start, end); -1 for none.
bool spanOf(const QJsonObject& args, tick_t* from, tick_t* to)
{
	const bool hasStart = args.contains(QStringLiteral("start"));
	if (hasStart != args.contains(QStringLiteral("end"))) { return false; }
	*from = hasStart ? args.value(QStringLiteral("start")).toInt() : -1;
	*to = hasStart ? args.value(QStringLiteral("end")).toInt() : -1;
	return !hasStart || *to > *from;
}

//! Move the clips of @a tracks starting in [@a from, @a to) (every one with no span) by @a delta; how many moved.
int moveClips(const QVector<JournallingObject*>& tracks, tick_t from, tick_t to, tick_t delta)
{
	int moved = 0;
	for (JournallingObject* object : tracks)
	{
		// The clips the move takes are found first: a moved clip must not be found again past its new start.
		std::vector<Clip*> taken;
		for (Clip* clip : static_cast<Track*>(object)->getClips())
		{
			if (inSpan(clip->startPosition().getTicks(), from, to)) { taken.push_back(clip); }
		}
		for (Clip* clip : taken) { clip->movePosition(TimePos(clip->startPosition().getTicks() + delta)); ++moved; }
	}
	return moved;
}

ControlResult folderMove(const QJsonObject& args)
{
	ControlResult error;
	const auto* folder = dynamic_cast<const TrackFolder*>(resolveTrack(args.value(QStringLiteral("folder")).toString(), &error));
	if (folder == nullptr) { return error.errorMessage.isEmpty() ? ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'folder' is a folder track")) : error; }
	tick_t from = -1;
	tick_t to = -1;
	if (!spanOf(args, &from, &to)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'start' and 'end' come together, 'end' after 'start'")); }
	const tick_t delta = args.value(QStringLiteral("delta")).toInt();
	tick_t earliest = MaxSongLength;
	const QVector<JournallingObject*> tracks = folderTracks(folder, from, to, &earliest);
	if (tracks.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the folder holds no clips there")); }
	if (delta == 0 || earliest + delta < 0) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'delta' moves the clips, and not before the song's start")); }
	addUndoStep(tracks);
	const int moved = moveClips(tracks, from, to, delta);
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("moved"), moved}, {QStringLiteral("tracks"), static_cast<int>(tracks.size())}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerArrangeToolCommands(ControlRegistry& registry)
{
	add(registry, "clip.list", QStringLiteral("The arrangement's clips: id, name, track, start, length and kind - every one, a "
		"'track''s, or those whose name holds 'name' (any case), at most 'limit' (500)."),
		{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("name"), stringProperty()}, {QStringLiteral("limit"), integerProperty(1, 10000)}},
		{}, false, listClips);
	add(registry, "arrangement.next_transient", QStringLiteral("Move the play position to the next (or, with 'direction' previous, "
		"the previous) transient of the audio clips under it - tab to transient. Through transport.seek."),
		{{QStringLiteral("direction"), enumProperty({QStringLiteral("next"), QStringLiteral("previous")})}}, {}, false, nextTransient);
	add(registry, "clip.remove_silence", QStringLiteral("Cut out of an audio clip every stretch quieter than 'threshold_db' (-50 when "
		"left out) for at least 'min_length' ticks (48, a beat): the clip is split around each and the quiet pieces deleted. "
		"One undo step."),
		{{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("threshold_db"), numberProperty()},
			{QStringLiteral("min_length"), integerProperty(1, 192 * 64)}}, {QStringLiteral("clip")}, true, removeSilence);
	add(registry, "track.folder_move", QStringLiteral("Move every clip inside folder track 'folder' (its sub-folders too) by 'delta' "
		"ticks - the folder's part of the song as one block. With 'start' and 'end' only the clips starting in [start, end) "
		"move: one of the folder's summary clips, the spans its row draws. One undo step."),
		{{QStringLiteral("folder"), stringProperty()}, {QStringLiteral("delta"), integerProperty(-192 * 9999, 192 * 9999)},
			{QStringLiteral("start"), integerProperty(0, 192 * 9999)}, {QStringLiteral("end"), integerProperty(1, 192 * 9999)}},
		{QStringLiteral("folder"), QStringLiteral("delta")}, true, folderMove);
}

} // namespace lmms
