/*
 * VideoTrackTest.cpp - the project's video: attach, place, decode on a worker, save and load, extract its sound
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

/*! 0.5.0 ED-054: the timecode text; a build without FFmpeg refusing every video.* command, typed; and with FFmpeg,
 *  on a clip made at test time (VideoTestClip.h: one second red, one blue, a tone) - video.attach reads the file and
 *  is one undo step, as are set_offset and detach, and junk is refused; <video> round-trips through the project file,
 *  is written only while a video is attached, and a project without it loads with none; the decoder lands on the
 *  right second going forward and back; the frame pump answers on the interface thread; the video window follows a
 *  seek, before and inside the video, and a detach; and extract_audio writes the tone to a WAV and lands it on a new
 *  audio track at the video's offset, one undo step. */

#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Song.h"
#include "SongEditor.h"
#include "TaskCenter.h"
#include "Track.h"
#include "VideoDecoder.h"
#include "VideoFramePump.h"
#include "VideoLane.h"
#include "VideoReference.h"
#include "VideoTestClip.h"
#include "VideoWindow.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString fileText(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

//! Task @a id's state in the task area (Failed when it is not listed).
TaskState taskState(int id)
{
	for (const TaskInfo& info : TaskCenter::instance().tasks())
	{
		if (info.id == id) { return info.state; }
	}
	return TaskState::Failed;
}

bool reddish(const QImage& image)
{
	const QRgb centre = image.pixel(image.width() / 2, image.height() / 2);
	return qRed(centre) > 150 && qBlue(centre) < 100;
}

bool bluish(const QImage& image)
{
	const QRgb centre = image.pixel(image.width() / 2, image.height() / 2);
	return qBlue(centre) > 150 && qRed(centre) < 100;
}

} // namespace

class VideoTrackTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	QString m_clip;

	//! The clip's path, made once; skips the calling test in a build without FFmpeg.
	bool needClip()
	{
		if (!VideoDecoder::available()) { return false; }
#ifdef ZENE_HAVE_FFMPEG
		if (m_clip.isEmpty() && videotest::makeClip(m_dir.filePath(QStringLiteral("clip.mkv"))))
		{
			m_clip = m_dir.filePath(QStringLiteral("clip.mkv"));
		}
#endif
		return !m_clip.isEmpty();
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void timecodeIsHoursMinutesSecondsFrames()
	{
		QCOMPARE(VideoReference::timecode(0.0, 25.0), QStringLiteral("00:00:00:00"));
		QCOMPARE(VideoReference::timecode(3725.5, 24.0), QStringLiteral("01:02:05:12"));
		QCOMPARE(VideoReference::timecode(1.04, 25.0), QStringLiteral("00:00:01:01"));
		QCOMPARE(VideoReference::timecode(-2.5, 30.0), QStringLiteral("-00:00:02:15"));
		QCOMPARE(VideoReference::timecode(0.5, 0.0), QStringLiteral("00:00:00:12"));  // unknown rate: 25
		QCOMPARE(VideoReference::timecode(59.999, 29.97), QStringLiteral("00:00:59:29"));
	}

	void withoutFfmpegEveryCommandIsUnavailable()
	{
		if (VideoDecoder::available()) { QSKIP("this build has FFmpeg"); }
		for (const char* id : {"video.attach", "video.detach", "video.get_state", "video.set_offset", "video.extract_audio",
				 "video.window_show"})
		{
			const QJsonObject args = QLatin1String(id) == QLatin1String("video.attach")
				? QJsonObject{{QStringLiteral("path"), QStringLiteral("/nowhere.mkv")}}
				: QLatin1String(id) == QLatin1String("video.set_offset") ? QJsonObject{{QStringLiteral("offset"), 0}} : QJsonObject();
			const ControlResult answer = run(QLatin1String(id), args);
			QVERIFY2(!answer.ok, id);
			QCOMPARE(answer.errorKind, ControlErrorKind::Requires);
			QVERIFY2(answer.errorMessage.contains(QStringLiteral("unavailable")), qPrintable(answer.errorMessage));
		}
	}

	void attachReadsTheFileAndEachEditIsOneUndoStep()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		Engine::getSong()->clearProject();
		const ControlResult attached = run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_clip}, {QStringLiteral("offset"), 192}});
		QVERIFY2(attached.ok, qPrintable(attached.errorMessage));
		QCOMPARE(attached.result.value(QStringLiteral("width")).toInt(), videotest::Width);
		QCOMPARE(attached.result.value(QStringLiteral("height")).toInt(), videotest::Height);
		QVERIFY(qAbs(attached.result.value(QStringLiteral("fps")).toDouble() - videotest::Fps) < 0.01);
		QVERIFY(qAbs(attached.result.value(QStringLiteral("duration")).toDouble() - 2.0) < 0.1);
		QVERIFY(attached.result.value(QStringLiteral("has_audio")).toBool());
		QCOMPARE(attached.result.value(QStringLiteral("offset")).toInt(), 192);
		QVERIFY(attached.result.value(QStringLiteral("end")).toInt() > 192);

		QVERIFY(run(QStringLiteral("video.set_offset"), {{QStringLiteral("offset"), 384}}).ok);
		QCOMPARE(VideoReference::instance().attachment().offset, 384);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(VideoReference::instance().attachment().offset, 192);

		QVERIFY(run(QStringLiteral("video.detach")).ok);
		QVERIFY(!VideoReference::instance().attachment().attached());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(VideoReference::instance().attachment().path, QFileInfo(m_clip).absoluteFilePath());
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the attach itself
		QVERIFY(!VideoReference::instance().attachment().attached());
	}

	void junkIsRefusedTyped()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		Engine::getSong()->clearProject();
		const QString text = m_dir.filePath(QStringLiteral("not-a-video.mkv"));
		QFile junk(text);
		QVERIFY(junk.open(QIODevice::WriteOnly));
		junk.write("this is not a video");
		junk.close();
		QCOMPARE(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), text}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_dir.filePath(QStringLiteral("missing.mkv"))}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("video.detach")).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("video.set_offset"), {{QStringLiteral("offset"), 0}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("video.extract_audio")).errorKind, ControlErrorKind::NotFound);
		QVERIFY(!VideoReference::instance().attachment().attached());
	}

	void theVideoRoundTripsThroughTheProjectFile()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		Song* song = Engine::getSong();
		const QString bare = m_dir.filePath(QStringLiteral("bare.mmp"));
		song->clearProject();
		QVERIFY(song->saveProjectFile(bare));
		QVERIFY2(!fileText(bare).contains(QStringLiteral("<video")), "a project with no video wrote a <video> element");

		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_clip}, {QStringLiteral("offset"), 96}}).ok);
		const VideoAttachment saved = VideoReference::instance().attachment();
		const QString withVideo = m_dir.filePath(QStringLiteral("video.mmp"));
		QVERIFY(song->saveProjectFile(withVideo));

		song->loadProject(bare);
		QVERIFY2(!VideoReference::instance().attachment().attached(), "loading a project without a video kept the previous one");
		song->loadProject(withVideo);
		const VideoAttachment& loaded = VideoReference::instance().attachment();
		QCOMPARE(loaded.path, saved.path);
		QCOMPARE(loaded.offset, 96);
		QCOMPARE(loaded.width, saved.width);
		QCOMPARE(loaded.height, saved.height);
		QVERIFY(qAbs(loaded.fps - saved.fps) < 1e-6);
		QVERIFY(qAbs(loaded.duration - saved.duration) < 1e-6);
		QCOMPARE(loaded.hasAudio, saved.hasAudio);
		song->clearProject();
	}

	void theDecoderLandsOnTheRightSecondForwardAndBack()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		VideoDecoder decoder;
		QString why;
		QVERIFY2(decoder.open(m_clip, &why), qPrintable(why));
		const QImage late = decoder.frameAt(1.5, QSize(80, 80));
		QCOMPARE(late.size(), QSize(80, 45));  // fitted into the box with the clip's 16:9 kept
		QVERIFY(bluish(late));
		QVERIFY(qAbs(decoder.shownSeconds() - 1.48) < 0.03);  // the frame showing at 1.5 s starts at frame 37
		const QImage early = decoder.frameAt(0.3, QSize());  // backwards: a seek
		QCOMPARE(early.size(), QSize(videotest::Width, videotest::Height));
		QVERIFY(reddish(early));
		QVERIFY(qAbs(decoder.shownSeconds() - 0.28) < 0.03);
		QVERIFY(bluish(decoder.frameAt(1.2, QSize(40, 40))));  // forward across the cut without a seek
		QVERIFY(bluish(decoder.frameAt(99.0, QSize(40, 40))));  // past the end: the last frame
	}

	void thePumpAnswersOnTheInterfaceThread()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		VideoFramePump pump;
		QImage got;
		int tag = -1;
		bool onInterfaceThread = false;
		QObject receiver;  // lives on this thread: the answer is queued to it
		connect(&pump, &VideoFramePump::frameReady, &receiver, [&](const QImage& image, double, int t) {
			got = image;
			tag = t;
			onInterfaceThread = QThread::currentThread() == receiver.thread();
		});
		pump.request(m_clip, 0.5, QSize(32, 18), 7);
		QTRY_COMPARE_WITH_TIMEOUT(tag, 7, 10000);
		QVERIFY(onInterfaceThread);
		QVERIFY(reddish(got));
		pump.request(QString(), 0.0, QSize(), 8);  // an empty path closes the file: a null answer
		QTRY_COMPARE_WITH_TIMEOUT(tag, 8, 10000);
		QVERIFY(got.isNull());
	}

	void theArrangerLaneClosesItsDecoderOnDetach()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_clip}}).ok);
		{
			gui::SongEditor editor(song);
			gui::VideoLane lane(&editor);
			auto* pump = lane.findChild<VideoFramePump*>();
			QVERIFY(pump != nullptr);
			QTRY_COMPARE_WITH_TIMEOUT(lane.thumbnailsReady(), gui::VideoLane::Thumbnails, 10000);
			QSignalSpy answers(pump, &VideoFramePump::frameReady);
			QVERIFY(answers.isValid());
			QVERIFY(run(QStringLiteral("video.detach")).ok);
			QTRY_COMPARE_WITH_TIMEOUT(answers.count(), 1, 10000);
			QVERIFY(qvariant_cast<QImage>(answers.takeFirst().at(0)).isNull());
			QVERIFY(lane.isHidden());
		}
		song->clearProject();
	}

	void theWindowFollowsTheTransport()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		Engine::getSong()->clearProject();
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_clip}, {QStringLiteral("offset"), 192}}).ok);
		gui::VideoWindow window(nullptr);
		window.resize(320, 202);
		const tick_t into = VideoReference::instance().tickAt(1.5);  // 1.5 s into the video: its blue second
		// The arrangement's play head, as the arranger's ruler moves it. (transport.seek while stopped moves
		// the stopped mode's timeline, not the song's - logged for 0.5.x, not this test's concern.)
		Engine::getSong()->setPlayPos(into, Song::PlayMode::Song);
		window.follow();
		QTRY_VERIFY_WITH_TIMEOUT(window.frameSeconds() >= 0.0, 10000);
		QVERIFY(qAbs(window.frameSeconds() - 1.5) < 0.05);
		QVERIFY2(window.timecode().startsWith(QStringLiteral("00:00:01:")), qPrintable(window.timecode()));
		QVERIFY(window.status().isEmpty());

		Engine::getSong()->setPlayPos(0, Song::PlayMode::Song);  // before the video's offset
		window.follow();
		QVERIFY2(window.timecode().startsWith(QLatin1Char('-')), qPrintable(window.timecode()));
		QVERIFY(!window.status().isEmpty());
		QVERIFY(run(QStringLiteral("video.detach")).ok);  // the window follows the detach at once (VideoReference::changed)
		QVERIFY(window.timecode().isEmpty());
		Engine::getSong()->clearProject();
	}

	void extractAudioLandsTheSoundOnANewTrackInOneStep()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_clip}, {QStringLiteral("offset"), 192}}).ok);
		const auto tracksBefore = song->tracks().size();
		const QString out = m_dir.filePath(QStringLiteral("extracted.wav"));
		const ControlResult extracted = run(QStringLiteral("video.extract_audio"), {{QStringLiteral("out"), out}});
		QVERIFY2(extracted.ok, qPrintable(extracted.errorMessage));
		QVERIFY(QFileInfo(out).size() > 0);
		QCOMPARE(extracted.result.value(QStringLiteral("sample_rate")).toInt(), videotest::Rate);
		QVERIFY(qAbs(extracted.result.value(QStringLiteral("frames")).toDouble() - 2.0 * videotest::Rate) < 0.05 * videotest::Rate);
		QCOMPARE(song->tracks().size(), tracksBefore + 1);
		const Track* landed = song->tracks().back();
		QCOMPARE(landed->type(), Track::Type::Sample);
		QCOMPARE(landed->getClips().size(), std::size_t{1});
		const auto* clip = dynamic_cast<const SampleClip*>(landed->getClips().front());
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 192);
		QCOMPARE(QFileInfo(clip->sampleFile()).fileName(), QStringLiteral("extracted.wav"));

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(song->tracks().size(), tracksBefore);
		QVERIFY2(QFileInfo::exists(out), "the undo removed the WAV, which it documents it keeps");
		song->clearProject();
	}

	//! 0.5.0 theme 24: with 'background' the decode runs on a worker listed in the task area and the reply comes at once;
	//! the track lands when it completes, as the same ONE undo step.
	void extractAudioInTheBackgroundLandsWhenItCompletes()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_clip}, {QStringLiteral("offset"), 96}}).ok);
		const auto tracksBefore = song->tracks().size();
		const QString out = m_dir.filePath(QStringLiteral("background.wav"));
		const ControlResult started = run(QStringLiteral("video.extract_audio"), {{QStringLiteral("out"), out},
			{QStringLiteral("background"), true}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QVERIFY(started.result.value(QStringLiteral("background")).toBool());
		const int task = started.result.value(QStringLiteral("task")).toInt(-1);
		QVERIFY(task > 0);
		const auto stateOf = [task] { return taskState(task); };
		QTRY_VERIFY_WITH_TIMEOUT(stateOf() != TaskState::Running, 20000);
		QCOMPARE(stateOf(), TaskState::Done);
		QCOMPARE(song->tracks().size(), tracksBefore + 1);
		const auto* clip = dynamic_cast<const SampleClip*>(song->tracks().back()->getClips().front());
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 96);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(song->tracks().size(), tracksBefore);
		song->clearProject();
	}

	//! A background extraction cancelled from the task area adds nothing and leaves no WAV behind.
	void aCancelledBackgroundExtractionAddsNothing()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_clip}}).ok);
		const auto tracksBefore = song->tracks().size();
		const QString out = m_dir.filePath(QStringLiteral("cancelled.wav"));
		const ControlResult started = run(QStringLiteral("video.extract_audio"), {{QStringLiteral("out"), out},
			{QStringLiteral("background"), true}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		const int task = started.result.value(QStringLiteral("task")).toInt(-1);
		// The worker may already be done with a two-second clip; either way the outcome is consistent.
		const bool cancelled = TaskCenter::instance().cancel(task);
		const auto stateOf = [task] { return taskState(task); };
		QTRY_VERIFY_WITH_TIMEOUT(stateOf() != TaskState::Running, 20000);
		const bool done = stateOf() == TaskState::Done;
		QVERIFY(done || stateOf() == TaskState::Cancelled);
		QVERIFY(cancelled || done);
		QCOMPARE(song->tracks().size(), tracksBefore + (done ? 1 : 0));
		if (!done) { QVERIFY2(!QFileInfo::exists(out), "a cancelled extraction left its WAV"); }
		song->clearProject();
	}
};

QTEST_MAIN(VideoTrackTest)
#include "VideoTrackTest.moc"
