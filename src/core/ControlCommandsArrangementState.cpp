/*
 * ControlCommandsArrangement.cpp - the track.* (song container) commands and
 *                                  arrangement.get_state (SPEC A11-A16).
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
 */

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

void registerArrangementGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("arrangement.get_state");
	cmd.group = QStringLiteral("arrangement");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("Every track with its clips, addressed by the stable trk-<n> and "
		"clip-<n> ids. The trk-<n> number is assigned at creation and persists in the project file. "
		"Addressing is scoped to the SONG container: a track inside a nested container (the "
		"<trackcontainer> a pattern track carries) is not reachable by id, exactly as it is not "
		"addressable by index.");
	cmd.argsSchema = control::objectSchema({});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("clips"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("track_count"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("clip_count"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.handler = [](const QJsonObject&) {
		const QVector<control::ClipRef> refs = control::enumerateClips();
		QJsonArray clips;
		for (const control::ClipRef& ref : refs) { clips.append(control::clipState(ref)); }

		QJsonArray tracks;
		const TrackContainer::TrackList& list = Engine::getSong()->tracks();
		for (int i = 0; i < static_cast<int>(list.size()); ++i)
		{
			QJsonObject entry = trackEditState(list[i], i);
			QJsonArray clipIds;
			for (const control::ClipRef& ref : refs)
			{
				if (ref.trackIndex == i) { clipIds.append(control::clipId(ref.ordinal)); }
			}
			entry.insert(QStringLiteral("clips"), clipIds);
			entry.insert(QStringLiteral("selected"), control::selectedClipId().isEmpty() ? false
				: clipIds.contains(control::selectedClipId()));
			tracks.append(entry);
		}

		QJsonObject result;
		result.insert(QStringLiteral("tracks"), tracks);
		result.insert(QStringLiteral("clips"), clips);
		result.insert(QStringLiteral("track_count"), tracks.size());
		result.insert(QStringLiteral("clip_count"), clips.size());
		result.insert(QStringLiteral("selected_clip"), control::selectedClipId());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


void registerTrackSetArm(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_arm");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_arm");
	cmd.description = QStringLiteral("Arm or disarm a track for recording. Refused: this tree has no "
		"record-arm on a song track.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("armed"), control::booleanProperty()},
	}, {QStringLiteral("track"), QStringLiteral("armed")});
	cmd.resultSchema = control::objectSchema({});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		// Honest refusal, not a fake success. Record-arm in this tree lives on the
		// prototype MultiTrackRecorder (AudioEngine::recorder(), two capture
		// streams keyed by input channel, task #556) and not on lmms::Track: there
		// is no per-track armed flag to write, and inventing one would add a field
		// to the track serialization format.
		ControlResult error;
		Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
		if (track == nullptr) { return error; }
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("no record-arm exists on a %1 track in this build: arm state lives on the "
				"prototype MultiTrackRecorder (AudioEngine::recorder(), input-channel keyed), not on the "
				"song model").arg(control::trackTypeNameOf(track->type())));
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerArrangementStateCommands(ControlRegistry& registry)
{
	registerTrackSetArm(registry);
	registerArrangementGetState(registry);
}

} // namespace lmms
