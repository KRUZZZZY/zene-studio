/*
 * ControlCommandsRecorder.cpp - plugin.record and plugin.recorder_land: takes recorded by an insert
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
/*! 0.5.0 L10 (W3) MX-156: the Recorder device's two commands.
 *  - plugin.record: start a take of what passes the insert (a file of the caller's, or a new one in
 *    the user's Recordings folder), or stop it; the file is all it writes - the project only learns
 *    the take's path, as the device's saved state;
 *  - plugin.recorder_land: the device's last take on a new audio track, as one undo step. */

#include <QFileInfo>

#include "AudioEngine.h"
#include "ControlDeviceSupport.h"
#include "ControlDeviceTools.h"
#include "ControlRegistryGroups.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Song.h"
#include "TakeRecorder.h"
#include "TimePos.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

//! The recorder of the device 'target' / 'plugin' name, or nullptr with *error set.
TakeRecorder* recorderOf(const QJsonObject& args, ControlResult* error)
{
	ControlTarget target;
	if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, error)) { return nullptr; }
	const QString plugin = args.value(QStringLiteral("plugin")).toString();
	Effect* effect = resolveControlEffect(target, plugin, error);
	if (effect == nullptr) { return nullptr; }
	TakeRecorder* recorder = TakeRecorder::of(effect);
	if (recorder == nullptr) { *error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 is not a Recorder").arg(plugin)); }
	return recorder;
}

QJsonObject takeState(const QJsonObject& args, const TakeRecorder& recorder)
{
	return {{QStringLiteral("target"), args.value(QStringLiteral("target"))}, {QStringLiteral("plugin"), args.value(QStringLiteral("plugin"))},
		{QStringLiteral("recording"), recorder.recording()}, {QStringLiteral("take"), recorder.lastTake()},
		{QStringLiteral("frames"), static_cast<double>(recorder.framesWritten())},
		{QStringLiteral("dropped"), static_cast<double>(recorder.framesDropped())}};
}

ControlResult record(const QJsonObject& args)
{
	ControlResult error;
	TakeRecorder* recorder = recorderOf(args, &error);
	if (recorder == nullptr) { return error; }
	if (!args.value(QStringLiteral("on")).toBool(!recorder->recording()))
	{
		recorder->stop();
		return ControlResult::success(takeState(args, *recorder));
	}
	const QString requestedFile = args.value(QStringLiteral("file")).toString();
	const QString file = requestedFile.isEmpty()
		? TakeRecorder::defaultTakePath() : QFileInfo(requestedFile).absoluteFilePath();
	QString why;
	if (!recorder->start(file, static_cast<int>(Engine::audioEngine()->outputSampleRate()), &why))
	{
		return ControlResult::failure(ControlErrorKind::Refused, why);
	}
	Engine::getSong()->setModified();  // the device's saved state now names the take
	return ControlResult::success(takeState(args, *recorder));
}

ControlResult land(const QJsonObject& args)
{
	ControlResult error;
	TakeRecorder* recorder = recorderOf(args, &error);
	if (recorder == nullptr) { return error; }
	if (recorder->recording()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("stop the take first (plugin.record)")); }
	const QString take = recorder->lastTake();
	if (take.isEmpty() || !QFileInfo::exists(take))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the Recorder has no take on disk"));
	}
	Song* song = Engine::getSong();
	if (song == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("there is no song to land the take on"));
	}
	Track* track = Track::create(Track::Type::Sample, song);
	if (track == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("could not create a track for the take"));
	}
	track->setName(QFileInfo(take).completeBaseName());
	if (auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(args.value(QStringLiteral("position")).toInt(0)))))
	{
		clip->setSampleFile(take);
	}
	recordCreatedTrack(track);
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("take"), take}});
}

} // namespace

void registerRecorderCommands(ControlRegistry& registry)
{
	const QJsonObject device{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()}};
	const QStringList both{QStringLiteral("target"), QStringLiteral("plugin")};
	QJsonObject take = device;
	take.insert(QStringLiteral("on"), booleanProperty());
	take.insert(QStringLiteral("file"), stringProperty());
	addDeviceCommand(registry, "plugin.record", QStringLiteral("Record what passes a Recorder insert ('plugin' fx-<n> on 'target') to a "
		"stereo 24-bit WAV take - 'file', or a new file in the user's Recordings folder - or stop the take ('on' false; it toggles "
		"when left out). The sound passes unchanged. It writes the file and nothing else: no undo step. Answers the take, the "
		"frames written and any dropped."), take, both, false, record);
	QJsonObject landing = device;
	landing.insert(QStringLiteral("position"), integerProperty(0, 1 << 30));
	addDeviceCommand(registry, "plugin.recorder_land", QStringLiteral("Put a Recorder's last take on a new audio track at 'position' "
		"(ticks, 0 when left out). One undo step removes the track."), landing, both, true, land);
}

} // namespace lmms
