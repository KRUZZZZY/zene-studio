/*
 * ControlCommandsVideo.cpp - the video.* commands: attach a video to the project, place it, extract its sound
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

/*! 0.5.0 ED-054, the project's video (VideoReference):
 *  - video.attach / video.detach / video.set_offset: one video per project, where it starts in the song; each one
 *    undo step, saved with the project;
 *  - video.get_state: what is attached, where it starts and ends, and whether the file is still there;
 *  - video.extract_audio: the video's sound to a WAV next to the project, put on a new audio track at the video's
 *    offset - one undo step (the WAV stays on disk).
 *  video.window_show lives with the window (src/gui/VideoWindow.cpp). In a build without FFmpeg every command
 *  answers `requires` with VideoDecoder::unavailableReason(). */

#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QThreadPool>

#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Song.h"
#include "TaskCenter.h"
#include "Track.h"
#include "VideoAudioExtract.h"
#include "VideoCommands.h"
#include "VideoDecoder.h"
#include "VideoReference.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxAudioNames = 100;  //!< "<video>-audio-<n>.wav" tried up to this n; past it the default name is refused
const int ExtractionOwner = 0;  //!< its address names the background extractions in the task area
int nextExtraction = 1;

ControlResult unavailable()
{
	return ControlResult::failure(ControlErrorKind::Requires, VideoDecoder::unavailableReason());
}

ControlResult noVideo()
{
	return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no video is attached to the project (video.attach)"));
}

QJsonObject stateJson()
{
	const VideoAttachment& video = VideoReference::instance().attachment();
	QJsonObject out{{QStringLiteral("available"), VideoDecoder::available()}, {QStringLiteral("attached"), video.attached()}};
	if (!video.attached()) { return out; }
	Song* song = Engine::getSong();
	out.insert(QStringLiteral("path"), video.path);
	out.insert(QStringLiteral("exists"), QFileInfo(video.path).isFile());
	out.insert(QStringLiteral("offset"), static_cast<qint64>(video.offset));
	out.insert(QStringLiteral("offset_seconds"), song->secondsAtTick(video.offset));
	out.insert(QStringLiteral("end"), static_cast<qint64>(VideoReference::instance().endTick()));
	out.insert(QStringLiteral("fps"), video.fps);
	out.insert(QStringLiteral("duration"), video.duration);
	out.insert(QStringLiteral("width"), video.width);
	out.insert(QStringLiteral("height"), video.height);
	out.insert(QStringLiteral("has_audio"), video.hasAudio);
	return out;
}

void applyAttachment(const VideoAttachment& video)
{
	VideoReference::instance().set(video);
	Engine::getSong()->setModified();
}

//! Put @a after in place of the attachment, as one undo step; nothing when they are the same.
void recordAttachment(const VideoAttachment& after)
{
	const VideoAttachment before = VideoReference::instance().attachment();
	if (before == after) { return; }
	applyAttachment(after);
	addUndoStep([before]() { applyAttachment(before); }, [after]() { applyAttachment(after); });
}

ControlResult attach(const QJsonObject& args)
{
	if (!VideoDecoder::available()) { return unavailable(); }
	const QFileInfo file(args.value(QStringLiteral("path")).toString());
	if (file.filePath().isEmpty() || !file.isFile())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no file '%1'").arg(file.filePath()));
	}
	VideoProbe probe;
	QString why;
	if (!VideoDecoder::probe(file.absoluteFilePath(), &probe, &why))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is not a video FFmpeg can read: %2").arg(file.filePath(), why));
	}
	recordAttachment(VideoAttachment{file.absoluteFilePath(), static_cast<tick_t>(args.value(QStringLiteral("offset")).toInt(0)),
		probe.fps, probe.duration, probe.width, probe.height, probe.hasAudio});
	return ControlResult::success(stateJson());
}

ControlResult detach(const QJsonObject&)
{
	if (!VideoDecoder::available()) { return unavailable(); }
	if (!VideoReference::instance().attachment().attached()) { return noVideo(); }
	recordAttachment(VideoAttachment());
	return ControlResult::success(stateJson());
}

ControlResult getState(const QJsonObject&)
{
	if (!VideoDecoder::available()) { return unavailable(); }
	return ControlResult::success(stateJson());
}

ControlResult setOffset(const QJsonObject& args)
{
	if (!VideoDecoder::available()) { return unavailable(); }
	VideoAttachment after = VideoReference::instance().attachment();
	if (!after.attached()) { return noVideo(); }
	after.offset = static_cast<tick_t>(args.value(QStringLiteral("offset")).toInt());
	recordAttachment(after);
	return ControlResult::success(stateJson());
}

//! "<video>-audio.wav" in the project's folder (the video's while the project has no file), numbered past any taken;
//! empty when every name up to MaxAudioNames is taken.
QString defaultAudioPath(const QString& videoPath)
{
	const QString project = Engine::getSong()->projectFileName();
	const QDir dir = project.isEmpty() ? QFileInfo(videoPath).absoluteDir() : QFileInfo(project).absoluteDir();
	return firstFreeAudioPath(dir, QFileInfo(videoPath).completeBaseName() + QStringLiteral("-audio"), MaxAudioNames);
}

//! A new audio track named after the video, holding @a wav at the video's offset; recorded as one undo step.
Track* landAudio(const VideoAttachment& video, const QString& wav)
{
	Track* track = Track::create(Track::Type::Sample, Engine::getSong());
	track->setName(QObject::tr("%1 (video audio)").arg(QFileInfo(video.path).completeBaseName()));
	if (auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(video.offset)))) { clip->setSampleFile(wav); }
	recordCreatedTrack(track);
	return track;
}

/*! A background extraction's end, back on the main thread (0.5.0 theme 24): the track lands as ONE undo step - the
 *  same step the synchronous run records - when the project still has the video it started from; otherwise nothing is
 *  added and the task says where the WAV is. */
void landExtracted(const VideoAttachment& video, const QString& wav, const TaskToken& taskToken,
	const QString& why, bool cancelled)
{
	if (!why.isEmpty())
	{
		TaskCenter::instance().finish(taskToken, cancelled ? TaskState::Cancelled : TaskState::Failed, why);
		return;
	}
	if (!(VideoReference::instance().attachment() == video))
	{
		TaskCenter::instance().finish(taskToken, TaskState::Failed,
			QObject::tr("the project's video changed while its audio was extracted; the WAV is kept at %1").arg(wav));
		return;
	}
	const Track* track = landAudio(video, wav);
	TaskCenter::instance().finish(taskToken, TaskState::Done, track->name());
}

//! Decode on a worker, listed in the task area with its progress and Cancel; the reply is an acknowledgement.
ControlResult extractInBackground(const VideoAttachment& video, const QString& wav)
{
	auto control = std::make_shared<VideoExtractControl>();
	const int local = nextExtraction++;
	TaskCenter& tasks = TaskCenter::instance();
	const TaskToken taskToken = tasks.begin(&ExtractionOwner, local, {QStringLiteral("video_audio"), QObject::tr("Extract audio from %1")
		.arg(QFileInfo(video.path).fileName()), [control] { control->cancel.store(true); },
		[control] { return static_cast<double>(control->progress.load()); }, false});
	QMetaObject::Connection shutdownConnection;
	if (QCoreApplication::instance() != nullptr)  // quitting stops it, so the pool is not held up by a long decode
	{
		shutdownConnection = QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &tasks,
			[weak = std::weak_ptr<VideoExtractControl>(control)] { if (auto live = weak.lock()) { live->cancel.store(true); } });
	}
	QThreadPool::globalInstance()->start([control, video, wav, taskToken, shutdownConnection] {
		VideoAudio audio;
		QString why;
		extractVideoAudio(video.path, wav, &audio, &why, control.get());
		QMetaObject::invokeMethod(&TaskCenter::instance(), [video, wav, taskToken, why, shutdownConnection,
			cancelled = control->cancel.load()] {
			QObject::disconnect(shutdownConnection);
			landExtracted(video, wav, taskToken, why, cancelled);
		}, Qt::QueuedConnection);
	});
	return ControlResult::success({{QStringLiteral("background"), true}, {QStringLiteral("task"), tasks.idOf(&ExtractionOwner, local)},
		{QStringLiteral("path"), wav}, {QStringLiteral("position"), static_cast<qint64>(video.offset)}});
}

ControlResult extractAudio(const QJsonObject& args)
{
	if (!VideoDecoder::available()) { return unavailable(); }
	const VideoAttachment video = VideoReference::instance().attachment();
	if (!video.attached()) { return noVideo(); }
	if (!video.hasAudio) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the video has no audio stream")); }
	const QString out = args.value(QStringLiteral("out")).toString();
	const QString wav = out.isEmpty() ? defaultAudioPath(video.path) : QFileInfo(out).absoluteFilePath();
	// Refused before anything is decoded or recorded: an existing WAV is never reused as the default target.
	if (wav.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("every default WAV name in the video's folder is taken "
			"('<video>-audio.wav' and '-audio-2' to '-audio-%1'); pass 'out' to choose a new path").arg(MaxAudioNames));
	}
	if (args.value(QStringLiteral("background")).toBool()) { return extractInBackground(video, wav); }
	VideoAudio audio;
	QString why;
	// The decode happens before anything is recorded: a failed one leaves the project and the journal untouched.
	if (!extractVideoAudio(video.path, wav, &audio, &why)) { return ControlResult::failure(ControlErrorKind::Refused, why); }
	const Track* track = landAudio(video, wav);
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("path"), audio.path},
		{QStringLiteral("sample_rate"), audio.sampleRate}, {QStringLiteral("frames"), static_cast<qint64>(audio.frames)},
		{QStringLiteral("position"), static_cast<qint64>(video.offset)}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerVideoCommands(ControlRegistry& registry)
{
	VideoReference::instance();  // its project section exists before a project loads
	add(registry, "video.attach", QStringLiteral("Attach the video file 'path' to the project (one video per project: an "
		"attached one is replaced), its first frame shown at song tick 'offset' (0 when left out). FFmpeg reads its frame "
		"rate, length, size and whether it has sound. The video window and the arranger's video lane follow it. Saved with "
		"the project (the path absolute); one undo step."),
		{{QStringLiteral("path"), stringProperty()}, {QStringLiteral("offset"), tickProperty()}}, {QStringLiteral("path")}, true, attach);
	add(registry, "video.detach", QStringLiteral("Take the video off the project (the file is left alone). One undo step."),
		{}, {}, true, detach);
	add(registry, "video.get_state", QStringLiteral("The project's video: whether one is attached, its 'path' (and whether "
		"the file still 'exists'), the song tick it starts at ('offset', also in seconds) and ends at ('end'), its 'fps', "
		"'duration' (seconds), 'width', 'height' and 'has_audio'."), {}, {}, false, getState);
	add(registry, "video.set_offset", QStringLiteral("Move the video to start at song tick 'offset'. Saved with the project; "
		"one undo step."), {{QStringLiteral("offset"), tickProperty()}}, {QStringLiteral("offset")}, true, setOffset);
	add(registry, "video.extract_audio", QStringLiteral("Decode the video's sound to a 32-bit float stereo WAV - 'out', or "
		"'<video>-audio.wav' next to the project (next to the video while the project is unsaved), numbered when taken - and "
		"put it on a new audio track at the video's offset. Runs to the end before it answers - or, with 'background' true, "
		"decodes on a worker listed in the task area (task.list: its progress, and task.cancel stops it) and answers at once "
		"with the 'task' id; the track then lands when the decode completes. One undo step removes the track; the WAV stays "
		"on disk."), {{QStringLiteral("out"), stringProperty()}, {QStringLiteral("background"), booleanProperty()}}, {}, true,
		extractAudio);
	registerVideoWindowCommand(registry);
}

} // namespace lmms
