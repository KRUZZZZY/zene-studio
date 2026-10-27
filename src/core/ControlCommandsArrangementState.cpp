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

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "AudioEngine.h"
#include "AudioInputPath.h"
#include "ControlEdit.h"
#include "ControlRecordingSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"
#include "TrackRecorder.h"

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
		"addressable by index. `playhead_ticks` reports the arranger's Song-timeline position, also "
		"used by the video window.");
	cmd.argsSchema = control::objectSchema({});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("clips"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("track_count"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("clip_count"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("selected_clip"), control::stringProperty()},
		{QStringLiteral("playhead_ticks"), control::integerProperty(0, 0x7fffffff)},
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
				if (ref.trackIndex == i) { clipIds.append(control::clipId(ref.id)); }
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
		result.insert(QStringLiteral("playhead_ticks"), static_cast<qint64>(
			Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks()));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


/*!
 * track.set_arm - the record-arm verb (0.3.0, feature row 14).
 *
 * IT USED TO REFUSE. The registered command said, in as many words, that "no
 * record-arm exists on a %1 track in this build: arm state lives on the prototype
 * MultiTrackRecorder (AudioEngine::recorder(), input-channel keyed), not on the
 * song model" - honest about the tree it described, and a defect against the
 * feature list, which is the contradiction FEATURE-LIST-0.3.0.md row 14 records:
 * a real N-route recorder in the tree with no command that could start it.
 *
 * WHAT IT DOES NOW, and why it does NOT add a field to the track's serialization
 * format (the thing the old refusal was right to avoid): a song track is mapped
 * to a RECORD ROUTE by its position in the song, and the route's state lives
 * where it always did - on the recorder. So arming is a real capture that writes
 * a real WAV and journals the take, and a track file gains nothing: `trk-7` gains
 * a live capture stream, not an `armed` attribute. The consequence is stated in
 * the refusal below rather than hidden: a track whose index is past the
 * recorder's route count cannot be armed, and the route count is one per
 * configured input channel (record.input_get_state reports both numbers).
 *
 * The take is journalled by the recorder's own arm(), so a crash mid-recording
 * is offered to the next start by record.recovery_get_state - the same journal
 * the record.journal_* verbs write, not a second one.
 */
//! The record route a song track maps to: its position in the song, or -1 when
//! it is not in the song container at all (a nested container's track is not
//! addressable by id either, exactly as arrangement.get_state documents).
int trackRouteOf(const Track* track)
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return -1; }  // no song: no track is in its container
	const TrackContainer::TrackList& list = song->tracks();
	for (int i = 0; i < static_cast<int>(list.size()); ++i)
	{
		if (list[i] == track) { return i; }
	}
	return -1;
}

//! The track.set_arm reply for a route: the route's own state plus the address
//! the caller used, so a client can match a reply to its request.
QJsonObject armReply(const TrackRecorder& target, const QString& trackId, int route)
{
	QJsonObject result;
	result.insert(QStringLiteral("track"), trackId);
	result.insert(QStringLiteral("route"), route);
	result.insert(QStringLiteral("armed"), target.isArmed());
	result.insert(QStringLiteral("input_channel"), target.inputChannel());
	result.insert(QStringLiteral("input_channel_capacity"), target.inputChannelCapacity());
	result.insert(QStringLiteral("file"), QString::fromStdString(target.filePath()));
	result.insert(QStringLiteral("journal"), QString::fromStdString(target.journalPath()));
	result.insert(QStringLiteral("frames_pushed"), static_cast<qint64>(target.framesPushed()));
	result.insert(QStringLiteral("frames_recorded"), static_cast<qint64>(target.framesRecorded()));
	return result;
}

//! `armed: false`: stop the capture and retire its journal. The take stays, and
//! re-arming starts a NEW one, so this records no inverse and says so.
ControlResult disarmTrackArm(TrackRecorder& target, const QString& trackId, int route)
{
	const bool wasArmed = target.isArmed();
	target.disarm();

	QJsonObject result = armReply(target, trackId, route);
	result.insert(QStringLiteral("sample_rate"), 0);
	result.insert(QStringLiteral("armed_before"), wasArmed);
	return ControlResult::success(result);
}

//! `armed: true`: validate the take path and the input channel, then start a
//! real capture. No inverse is recorded: the take file the capture writes stays
//! on disk once the capture stops, so the contract row is irreversible and
//! control.undo refuses this command instead of half-reversing it.
ControlResult armTrackArm(MultiTrackRecorder& recorder, Track* track, const QString& trackId,
	int route, const QJsonObject& args, int sampleRate)
{
	TrackRecorder& target = recorder.track(route);
	if (target.isArmed())
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the record route this track maps to (route %1) is already armed and "
				"writing %2; disarm it first").arg(route)
				.arg(QString::fromStdString(target.filePath())));
	}

	const QString file = args.value(QStringLiteral("file")).toString();
	if (!file.isEmpty() && !QFileInfo(file).isAbsolute())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'file' must be absolute: a relative path names a different file in "
				"the next process, and so does the take journal beside it"));
	}
	const QString takePath = file.isEmpty()
		? QDir(control::defaultRecoveryDir()).absoluteFilePath(
			QStringLiteral("zene-take-trk%1.wav").arg(track->id()))
		: file;

	const int channels = target.inputChannelCapacity();
	const int channel = args.contains(QStringLiteral("input_channel"))
		? args.value(QStringLiteral("input_channel")).toInt()
		: std::min(route, channels - 1);
	if (!target.inputChannelSelectable(channel))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("input_channel %1 is outside [0, %2): this instance's input path "
				"delivers %2 channel(s) (record.input_get_state reports the device's own answer)")
				.arg(channel).arg(channels));
	}

	if (!recorder.armTrack(route, takePath.toStdString(), sampleRate, channel))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("route %1 could not be armed: the take file %2 could not be opened "
				"for writing").arg(route).arg(takePath));
	}

	QJsonObject result = armReply(target, trackId, route);
	result.insert(QStringLiteral("sample_rate"), sampleRate);
	return ControlResult::success(result);
}

//! track.set_arm's handler: arm or disarm the record route one song track maps to.
ControlResult setArm(const QJsonObject& args)
{
	ControlResult error;
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	if (!args.contains(QStringLiteral("armed")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'armed' is required: true starts the capture, false stops it"));
	}

	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this instance has no audio engine, so there is no recorder to arm "
				"(the route state lives on it: include/AudioEngine.h)"));
	}
	MultiTrackRecorder& recorder = engine->recorder();

	const int route = trackRouteOf(track);
	if (route < 0)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the track is not in the song container, so it has no route in the "
				"recorder"));
	}
	if (route >= recorder.trackCount())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this track is at position %1 in the song and the engine prepared "
				"%2 record route(s) (MultiTrackRecorder::MaxRoutes, fixed at start): a track "
				"past the last route has nowhere to record to")
				.arg(route).arg(recorder.trackCount()));
	}

	const QString trackId = args.value(QStringLiteral("track")).toString();
	if (!args.value(QStringLiteral("armed")).toBool())
	{
		return disarmTrackArm(recorder.track(route), trackId, route);
	}
	return armTrackArm(recorder, track, trackId, route, args,
		static_cast<int>(engine->baseSampleRate()));
}

void registerTrackSetArm(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_arm");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_arm");
	cmd.description = QStringLiteral("Arm or disarm a song track for recording. Arming starts a "
		"capture on the record route the track's position in the song maps to: the interleaved "
		"input channel named by 'input_channel' (default: the route's own index) is written to a "
		"24-bit WAV and the take is journalled beside it, so a crash mid-take is recoverable "
		"(record.recovery_get_state). 'file' must be absolute and defaults to zene-take-trk<N>.wav "
		"beside this instance's recovery file. The frames a route records come from the engine's "
		"input path, so a route armed while nothing is staged records silence: "
		"record.input_get_state reports whether a capture device is open and how many frames are "
		"staged ('capture_capable', 'capture_open', 'capture_reason', 'input_frames_staged'). "
		"Arming writes a take file that stays on disk when the capture stops, so it has no "
		"complete inverse and control.undo refuses this command; stop the capture with this "
		"command again, armed: false, or with record.disarm_track. Disarming records no inverse "
		"either, because re-arming starts a NEW take.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("armed"), control::booleanProperty()},
		{QStringLiteral("file"), control::stringProperty()},
		{QStringLiteral("input_channel"),
			control::integerProperty(0, AudioInputPath::MaxChannels - 1)},
	}, {QStringLiteral("track"), QStringLiteral("armed")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("route"), control::integerProperty()},
		{QStringLiteral("armed"), control::booleanProperty()},
		{QStringLiteral("input_channel"), control::integerProperty()},
		{QStringLiteral("input_channel_capacity"), control::integerProperty()},
		{QStringLiteral("file"), control::stringProperty()},
		{QStringLiteral("journal"), control::stringProperty()},
		{QStringLiteral("frames_pushed"), control::integerProperty()},
		{QStringLiteral("frames_recorded"), control::integerProperty()},
		{QStringLiteral("sample_rate"), control::integerProperty()},
		{QStringLiteral("armed_before"), control::booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = setArm;
	registry.registerCommand(cmd);
}

//! W1: what a batch arm did - the tracks it changed (in order), the ones that refused and why, each route's reply.
struct ArmBatch
{
	QJsonArray changed;
	QJsonArray refused;
	QJsonArray routes;
	QString lastReason;
};

//! The 'tracks' argument: an array whose every entry is a track ID string.
QJsonObject trackIdsProperty()
{
	QJsonObject property = control::arrayProperty();
	property.insert(QStringLiteral("items"), control::stringProperty());
	return property;
}

//! True when every entry of @a ids is a string - checked before any track is armed, so a malformed entry
//! cannot leave earlier valid entries armed while the batch reports a refusal.
bool allTrackIdsAreStrings(const QJsonArray& ids)
{
	return std::all_of(ids.begin(), ids.end(), [](const QJsonValue& id) { return id.isString(); });
}

//! Each track @a ids names, through track.set_arm's own handler. A track already in that state is skipped, not
//! refused; every other refusal is collected and the rest still go ahead. A repeated id names ONE track, so it is
//! acted on and reported once.
ArmBatch armEach(const QJsonArray& ids, bool armed)
{
	ArmBatch out;
	QSet<QString> seen;
	for (const QJsonValue& id : ids)
	{
		const QString trackId = id.toString();
		if (seen.contains(trackId)) { continue; }
		seen.insert(trackId);
		ControlResult one = setArm({{QStringLiteral("track"), trackId}, {QStringLiteral("armed"), armed}});
		if (one.ok)
		{
			// Disarming an already-disarmed route SUCCEEDS - there is nothing there to
			// refuse - so the batch has to ask the route's own before-state, or it
			// reports tracks whose state it never changed.
			if (!armed && !one.result.value(QStringLiteral("armed_before")).toBool()) { continue; }
			one.result.remove(QStringLiteral("__transaction"));  // the per-track reply carries no transaction payload: neither arm verb records an inverse
			out.changed.append(trackId);
			out.routes.append(one.result);
			continue;
		}
		if (one.errorKind == ControlErrorKind::Busy) { continue; }
		out.refused.append(QJsonObject{{QStringLiteral("track"), trackId}, {QStringLiteral("reason"), one.errorMessage}});
		out.lastReason = one.errorMessage;
	}
	return out;
}

/*! track.set_arm_many (W1): the mixer's Arm All as ONE call. Arming creates take files that remain on disk after the
 *  captures stop, so the batch has no complete inverse and control.undo refuses it. Stop each route with
 *  record.disarm_track; disarming records no inverse because re-arming starts a NEW take. */
ControlResult setArmMany(const QJsonObject& args)
{
	const QJsonArray ids = args.value(QStringLiteral("tracks")).toArray();
	if (ids.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'tracks' names no track")); }
	if (!allTrackIdsAreStrings(ids))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'tracks' entries must be strings"));
	}
	const bool armed = args.value(QStringLiteral("armed")).toBool();
	const ArmBatch batch = armEach(ids, armed);
	if (batch.changed.isEmpty())
	{
		return batch.refused.isEmpty()
			? ControlResult::failure(ControlErrorKind::Busy, armed
				? QStringLiteral("every named track's route is already armed")
				: QStringLiteral("every named track's route is already disarmed"))
			: ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no track was armed: %1").arg(batch.lastReason));
	}
	QJsonObject result{{QStringLiteral("armed"), armed}, {QStringLiteral("tracks"), batch.changed},
		{QStringLiteral("refused"), batch.refused}, {QStringLiteral("routes"), batch.routes}};
	return ControlResult::success(result);
}

void registerTrackSetArmMany(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_arm_many");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_arm_many");
	cmd.description = QStringLiteral("Arm or disarm several song tracks for recording in one call - the mixer's Arm All. "
		"Each track in 'tracks' (trk-<n> ids) is armed or disarmed as track.set_arm does it, with its default take file and "
		"input channel; a track already armed is skipped, and a track that refuses (past the last record route, say) is "
		"listed in 'refused' with its reason while the others still go ahead. 'tracks' answers the ones changed, 'routes' "
		"each one's route reply. Arming writes take files that remain on disk when captures stop, so control.undo refuses "
		"this command; stop each route with record.disarm_track using its route id from 'routes'. Disarming records no "
		"inverse, as track.set_arm's does not.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("tracks"), trackIdsProperty()},
		{QStringLiteral("armed"), control::booleanProperty()},
	}, {QStringLiteral("tracks"), QStringLiteral("armed")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("armed"), control::booleanProperty()},
		{QStringLiteral("tracks"), control::arrayProperty()},
		{QStringLiteral("refused"), control::arrayProperty()},
		{QStringLiteral("routes"), control::arrayProperty()},
	});
	cmd.mutating = true;
	cmd.handler = setArmMany;
	registry.registerCommand(cmd);
}

} // namespace

void registerArrangementStateCommands(ControlRegistry& registry)
{
	registerTrackSetArm(registry);
	registerTrackSetArmMany(registry);
	registerArrangementGetState(registry);
}

namespace control
{

bool trackRecordArmed(const Track* track)
{
	AudioEngine* engine = Engine::audioEngine();
	const int route = trackRouteOf(track);
	if (engine == nullptr || route < 0 || route >= engine->recorder().trackCount()) { return false; }
	return engine->recorder().track(route).isArmed();
}

} // namespace control

} // namespace lmms
