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
 *    folder handled as one block of the song, one undo step. */

#include <algorithm>
#include <cmath>
#include <vector>

#include <QJsonArray>

#include "ArrangementEdits.h"
#include "ClipTransients.h"
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
	QJsonArray clips;
	for (const Track* track : Engine::getSong()->tracks())
	{
		if (!onTrack.isEmpty() && trackIdOf(track) != onTrack) { continue; }
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
			if (audio == nullptr || tick < clip->startPosition().getTicks() || tick >= clip->endPosition().getTicks()) { continue; }
			const tick_t origin = clip->startPosition().getTicks() + clip->startTimeOffset().getTicks();
			for (const int at : clipTransientTicks(*audio)) { out.push_back(origin + at); }
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
	f_cnt_t start = -1;
	for (f_cnt_t f = window.sourceIn; f <= window.sourceOut; ++f)
	{
		const bool quiet = f < window.sourceOut && std::max(std::fabs(data[f][0]), std::fabs(data[f][1])) < level;
		if (quiet && start < 0) { start = f; }
		if (!quiet && start >= 0)
		{
			if (f - start >= minFrames) { runs.emplace_back(start, f); }
			start = -1;
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
	const auto minFrames = static_cast<f_cnt_t>(minTicks * Engine::framesPerTick());
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

//! The tracks inside @a folder that hold clips, and the earliest clip start among them in @a earliest.
QVector<JournallingObject*> folderTracks(const TrackFolder* folder, tick_t* earliest)
{
	QVector<JournallingObject*> tracks;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (!insideFolder(track, folder) || track->getClips().empty()) { continue; }
		tracks.append(track);
		for (const Clip* clip : track->getClips()) { *earliest = std::min<tick_t>(*earliest, clip->startPosition().getTicks()); }
	}
	return tracks;
}

ControlResult folderMove(const QJsonObject& args)
{
	ControlResult error;
	const auto* folder = dynamic_cast<const TrackFolder*>(resolveTrack(args.value(QStringLiteral("folder")).toString(), &error));
	if (folder == nullptr) { return error.errorMessage.isEmpty() ? ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'folder' is a folder track")) : error; }
	const tick_t delta = args.value(QStringLiteral("delta")).toInt();
	tick_t earliest = MaxSongLength;
	const QVector<JournallingObject*> tracks = folderTracks(folder, &earliest);
	if (tracks.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the folder holds no clips")); }
	if (delta == 0 || earliest + delta < 0) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'delta' moves the clips, and not before the song's start")); }
	addUndoStep(tracks);
	int moved = 0;
	for (JournallingObject* object : tracks)
	{
		for (Clip* clip : static_cast<Track*>(object)->getClips()) { clip->movePosition(TimePos(clip->startPosition().getTicks() + delta)); ++moved; }
	}
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("moved"), moved}, {QStringLiteral("tracks"), static_cast<int>(tracks.size())}});
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
		"ticks - the folder's part of the song as one block. One undo step."),
		{{QStringLiteral("folder"), stringProperty()}, {QStringLiteral("delta"), integerProperty(-192 * 9999, 192 * 9999)}},
		{QStringLiteral("folder"), QStringLiteral("delta")}, true, folderMove);
}

} // namespace lmms
