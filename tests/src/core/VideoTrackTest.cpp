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

#include <QApplication>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include <limits>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <sys/resource.h>
#endif

#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Song.h"
#include "SongEditor.h"
#include "TaskCenter.h"
#include "VideoFfmpeg.h"
#include "Track.h"
#include "VideoAudioExtract.h"
#include "VideoDecoder.h"
#include "VideoFramePump.h"
#include "VideoFfmpeg.h"
#include "VideoLane.h"
#include "VideoReference.h"
#include "VideoTestClip.h"
#include "VideoWindow.h"

#if defined(ZENE_HAVE_FFMPEG) && (defined(Q_OS_LINUX) || defined(Q_OS_MACOS))
#define ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <functional>

extern "C" {
#include <libswresample/swresample.h>
}
#endif

using namespace lmms;

namespace
{

#if defined(Q_OS_UNIX)
//! Limit the next extraction's output size, restoring both process settings when it leaves scope.
class ScopedFileSizeLimit
{
	struct rlimit m_originalLimit{};
	using SignalHandler = void (*)(int);
	SignalHandler m_originalHandler = SIG_DFL;
	bool m_active = false;

public:
	explicit ScopedFileSizeLimit(rlim_t bytes)
	{
		if (getrlimit(RLIMIT_FSIZE, &m_originalLimit) != 0 || bytes >= m_originalLimit.rlim_cur) { return; }
		m_originalHandler = std::signal(SIGXFSZ, SIG_IGN);
		if (m_originalHandler == SIG_ERR) { return; }
		rlimit limited = m_originalLimit;
		limited.rlim_cur = bytes;
		m_active = setrlimit(RLIMIT_FSIZE, &limited) == 0;
		if (!m_active) { std::signal(SIGXFSZ, m_originalHandler); }
	}

	~ScopedFileSizeLimit()
	{
		if (m_active)
		{
			setrlimit(RLIMIT_FSIZE, &m_originalLimit);
			std::signal(SIGXFSZ, m_originalHandler);
		}
	}

	bool active() const { return m_active; }
};
#endif

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

class VideoTrackTestApplication : public QApplication
{
public:
	using QApplication::QApplication;

	int aboutToQuitReceivers() const { return receivers(SIGNAL(aboutToQuit())); }
};

#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
namespace
{

std::atomic_bool failFrameAllocation{false};
std::atomic_bool failCodecContextAllocation{false};
std::atomic_bool failCodecAfterStream{false};
std::atomic_bool failStreamAllocation{false};
//! swr_convert calls that succeed before one fails; -1 disarms the hook.
std::atomic_int swrConvertCallsBeforeFailure{-1};
std::atomic_int swrConvertFailuresInjected{0};

//! Arm one swr_convert failure for the scope's lifetime, after @a callsBeforeFailure successful conversions.
class ScopedSwrConvertFailure
{
public:
	explicit ScopedSwrConvertFailure(int callsBeforeFailure) { swrConvertCallsBeforeFailure.store(callsBeforeFailure); }
	~ScopedSwrConvertFailure() { swrConvertCallsBeforeFailure.store(-1); }
};

//! Run @a checkFailure with one FFmpeg allocation forced to fail; crashes stay inside the child.
bool allocationFailureIsReported(const std::function<bool()>& checkFailure, std::atomic_bool& fail)
{
	const pid_t child = fork();
	if (child < 0) { return false; }
	if (child == 0)
	{
		fail.store(true, std::memory_order_relaxed);
		_exit(checkFailure() ? 0 : 1);
	}
	int status = 0;
	if (waitpid(child, &status, 0) < 0) { return false; }
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace

//! At file scope so FFmpeg calls from VideoTestClip resolve to these test-only failure hooks.
#pragma GCC visibility push(default)
extern "C" AVFrame* av_frame_alloc()
{
	if (failFrameAllocation.load(std::memory_order_relaxed)) { return nullptr; }
	static const auto real = reinterpret_cast<decltype(&av_frame_alloc)>(dlsym(RTLD_NEXT, "av_frame_alloc"));
	return real == nullptr ? nullptr : real();
}

extern "C" AVCodecContext* avcodec_alloc_context3(const AVCodec* codec)
{
	if (failCodecContextAllocation.load(std::memory_order_relaxed)) { return nullptr; }
	static const auto real = reinterpret_cast<decltype(&avcodec_alloc_context3)>(dlsym(RTLD_NEXT, "avcodec_alloc_context3"));
	return real == nullptr ? nullptr : real(codec);
}

extern "C" AVStream* avformat_new_stream(AVFormatContext* context, const AVCodec* codec)
{
	if (failStreamAllocation.exchange(false, std::memory_order_relaxed)) { return nullptr; }
	static const auto real = reinterpret_cast<decltype(&avformat_new_stream)>(dlsym(RTLD_NEXT, "avformat_new_stream"));
	AVStream* stream = real == nullptr ? nullptr : real(context, codec);
	if (stream != nullptr && failCodecAfterStream.exchange(false, std::memory_order_relaxed))
	{
		failCodecContextAllocation.store(true, std::memory_order_relaxed);
	}
	return stream;
}

extern "C" int swr_convert(SwrContext* context, uint8_t* const* out, int outCount, const uint8_t* const* in, int inCount)
{
	if (swrConvertCallsBeforeFailure.load(std::memory_order_relaxed) >= 0
		&& swrConvertCallsBeforeFailure.fetch_sub(1, std::memory_order_relaxed) == 0)
	{
		swrConvertFailuresInjected.fetch_add(1, std::memory_order_relaxed);
		return AVERROR_INVALIDDATA;
	}
	static const auto real = reinterpret_cast<decltype(&swr_convert)>(dlsym(RTLD_NEXT, "swr_convert"));
	return real == nullptr ? AVERROR(ENOSYS) : real(context, out, outCount, in, inCount);
}
#pragma GCC visibility pop
#endif // ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS

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

	void invalidSavedDurationIsReprobed()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		for (const QString& duration : {QStringLiteral("-1"), QStringLiteral("nan"), QStringLiteral("inf")})
		{
			QDomDocument document;
			QDomElement element = document.createElement(QStringLiteral("video"));
			element.setAttribute(QStringLiteral("path"), m_clip);
			element.setAttribute(QStringLiteral("offset"), 192);
			element.setAttribute(QStringLiteral("duration"), duration);
			VideoReference::instance().load(element);
			const VideoAttachment loaded = VideoReference::instance().attachment();
			const tick_t end = VideoReference::instance().endTick();
			VideoReference::instance().clear();
			QVERIFY2(qAbs(loaded.duration - 2.0) < 0.1, qPrintable(duration));
			QVERIFY2(end > loaded.offset, qPrintable(duration));
		}
	}

	void negativeSubFrameTimecodeKeepsItsSign()
	{
		// a time before the video's start that rounds to frame zero still shows the minus sign
		QCOMPARE(VideoReference::timecode(-0.01, 25.0), QStringLiteral("-00:00:00:00"));
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

	void failedOpenAcceptsNullErrorOutput()
	{
#ifdef ZENE_HAVE_FFMPEG
		lmms::video::MediaStream stream;
		QVERIFY(!stream.open(m_dir.filePath(QStringLiteral("missing.mkv")), AVMEDIA_TYPE_VIDEO, nullptr));
		QVERIFY(!stream.isOpen());
#else
		QSKIP("this build has no FFmpeg");
#endif
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

	void anUnrepresentableSeekKeepsDecoderProgress()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
#ifdef ZENE_HAVE_FFMPEG
		video::MediaStream media;
		QString why;
		QVERIFY2(media.open(m_clip, AVMEDIA_TYPE_VIDEO, &why), qPrintable(why));
		AVFrame* frame = av_frame_alloc();
		AVPacket* packet = av_packet_alloc();
		QVERIFY(frame != nullptr);
		QVERIFY(packet != nullptr);

		for (int i = 0; i < 30; ++i) { QVERIFY(media.receive(frame, packet)); }
		const auto timestamp = [](const AVFrame* decoded) {
			return decoded->best_effort_timestamp != AV_NOPTS_VALUE ? decoded->best_effort_timestamp : decoded->pts;
		};
		const int64_t previous = timestamp(frame);
		QVERIFY(previous != AV_NOPTS_VALUE);

		media.seek(std::numeric_limits<double>::max());
		QVERIFY(media.receive(frame, packet));
		const int64_t next = timestamp(frame);
		QVERIFY2(next > previous, "an unrepresentable seek rewound or discarded decoded video progress");

		av_frame_free(&frame);
		av_packet_free(&packet);
#endif
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

	void thePumpRetriesAPathAfterItsFirstOpenFails()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		const QString lateClip = m_dir.filePath(QStringLiteral("late-clip.mkv"));
		VideoFramePump pump;
		QImage got;
		int tag = -1;
		QObject receiver;
		connect(&pump, &VideoFramePump::frameReady, &receiver, [&](const QImage& image, double, int t) {
			got = image;
			tag = t;
		});

		pump.request(lateClip, 0.5, QSize(32, 18), 1);
		QTRY_COMPARE_WITH_TIMEOUT(tag, 1, 10000);
		QVERIFY(got.isNull());
#ifdef ZENE_HAVE_FFMPEG
		QVERIFY(videotest::makeClip(lateClip));
#else
		QSKIP("this build has no FFmpeg");
#endif

		pump.request(lateClip, 0.5, QSize(32, 18), 2);
		QTRY_COMPARE_WITH_TIMEOUT(tag, 2, 10000);
		QVERIFY2(!got.isNull() && reddish(got), "same-path request should retry after the file becomes readable");
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

	//! Q050-021: a stopped seek must set the same position the arranger reads.
	void aStoppedSeekSynchronizesTransportAndArranger()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		constexpr int target = 777;
		const ControlResult seek = run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), target}});
		QVERIFY2(seek.ok, qPrintable(seek.errorMessage));
		const ControlResult state = run(QStringLiteral("transport.get_state"));
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(state.result.value(QStringLiteral("position_ticks")).toInt(), target);
		QCOMPARE(song->getPlayPos(Song::PlayMode::Song).getTicks(), target);
	}

	//! Q050-021: the first frame requested after playback begins is the sought video frame.
	void theFirstFrameAfterPlaybackUsesTheStoppedSeek()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_clip}}).ok);
		const tick_t target = VideoReference::instance().tickAt(1.5);
		const ControlResult seek = run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), target}});
		QVERIFY2(seek.ok, qPrintable(seek.errorMessage));

		gui::VideoWindow window(nullptr);
		window.resize(320, 202);
		song->playSong();
		song->togglePause();  // Hold the first playing position while its frame decodes.
		QCOMPARE(song->playMode(), Song::PlayMode::Song);
		QVERIFY(qAbs(song->getPlayPos(Song::PlayMode::Song).getTicks() - target) < 8);
		window.show();
		window.follow();
		QTRY_VERIFY_WITH_TIMEOUT(window.frameSeconds() >= 0.0, 10000);
		QVERIFY(qAbs(window.frameSeconds() - 1.5) < 0.05);
		QVERIFY2(bluish(window.grab().toImage()), "the first frame after playback was not the sought blue frame");

		song->stop();
		song->clearProject();
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

	void aPartialWavWriteFailsAndRemovesTheFile()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
#if defined(Q_OS_UNIX)
		const QString out = m_dir.filePath(QStringLiteral("partial.wav"));
		VideoAudio audio;
		QString error;
		bool extracted = false;
		{
			ScopedFileSizeLimit limit(4096);
			if (!limit.active()) { QSKIP("could not set a temporary file-size limit"); }
			extracted = extractVideoAudio(m_clip, out, &audio, &error);
		}
		if (extracted)
		{
			QVERIFY(QFileInfo(out).size() > 44);
			QVERIFY(QFileInfo(out).size() <= 4096);
		}
		QVERIFY2(!extracted, "a short WAV write was reported as a successful extraction");
		QVERIFY(!error.isEmpty());
		QVERIFY(!QFileInfo::exists(out));
#else
		QSKIP("file-size limits are unavailable on this platform");
#endif
	}

	void cancelledDirectExtractionPreservesExistingDestination()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		const QString out = m_dir.filePath(QStringLiteral("existing.wav"));
		const QByteArray original = "keep this existing file";
		QFile existing(out);
		QVERIFY(existing.open(QIODevice::WriteOnly));
		QCOMPARE(existing.write(original), qint64(original.size()));
		existing.close();

		VideoAudio audio;
		QString error;
		VideoExtractControl control;
		control.cancel.store(true);
		QVERIFY(!extractVideoAudio(m_clip, out, &audio, &error, &control));
		QVERIFY(error.contains(QStringLiteral("cancelled")));

		QFile preserved(out);
		QVERIFY(preserved.open(QIODevice::ReadOnly));
		QCOMPARE(preserved.readAll(), original);

		QVERIFY2(extractVideoAudio(m_clip, out, &audio, &error), qPrintable(error));
		QCOMPARE(audio.path, out);
		QVERIFY(audio.frames > 0);
		QFile completed(out);
		QVERIFY(completed.open(QIODevice::ReadOnly));
		QCOMPARE(completed.read(4), QByteArray("RIFF", 4));
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

	//! A completed background decode releases its application shutdown callback.
	void completedBackgroundExtractionDisconnectsItsShutdownHook()
	{
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		auto* app = static_cast<VideoTrackTestApplication*>(QCoreApplication::instance());
		const int receiversBefore = app->aboutToQuitReceivers();
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_clip}}).ok);
		const QString out = m_dir.filePath(QStringLiteral("quit-hook.wav"));
		const ControlResult started = run(QStringLiteral("video.extract_audio"), {{QStringLiteral("out"), out},
			{QStringLiteral("background"), true}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		const int task = started.result.value(QStringLiteral("task")).toInt(-1);
		QVERIFY(task > 0);
		const auto stateOf = [task] { return taskState(task); };
		QTRY_VERIFY_WITH_TIMEOUT(stateOf() != TaskState::Running, 20000);
		QCOMPARE(stateOf(), TaskState::Done);
		const int receiversAfter = app->aboutToQuitReceivers();
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		song->clearProject();
		QCOMPARE(receiversAfter, receiversBefore);
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

	//! A resampler error after audio was already converted fails the extraction and publishes no WAV
	//! (a negative swr_convert result used to count as an empty success and commit a truncated file).
	void aResamplerErrorFailsTheExtractionAndPublishesNoWav()
	{
#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
		if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
		const QString out = m_dir.filePath(QStringLiteral("resampler-failure.wav"));
		VideoAudio audio;
		QString error;
		const int injectedBefore = swrConvertFailuresInjected.load();
		bool extracted = true;
		{
			ScopedSwrConvertFailure failure(1);
			extracted = extractVideoAudio(m_clip, out, &audio, &error);
		}
		QCOMPARE(swrConvertFailuresInjected.load() - injectedBefore, 1);
		QVERIFY2(!extracted, "a resampler error was reported as a successful extraction");
		QVERIFY(!error.isEmpty());
		QVERIFY2(!QFileInfo::exists(out), "a truncated WAV was published after a resampler error");
#else
		QSKIP("the resampler failure hook needs Linux or macOS with FFmpeg");
#endif
	}

	//! A failed FFmpeg allocation is reported, not dereferenced.
	void failedAllocationsAreReportedNotCrashed()
	{
#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
		if (!VideoDecoder::available()) { QSKIP("no FFmpeg in this build"); }
		AVFormatContext* out = nullptr;
		QVERIFY(avformat_alloc_output_context2(&out, nullptr, "matroska", "clip.mkv") >= 0);
		videotest::Encoder failedStream;
		QVERIFY2(allocationFailureIsReported([&out, &failedStream] {
			const bool added = videotest::addStream(out, failedStream, AV_CODEC_ID_MPEG4, videotest::setupVideo);
			return !added && failedStream.stream == nullptr && failedStream.codec != nullptr;
		}, failStreamAllocation), "avformat_new_stream failure was not reported independently");
		videotest::Encoder streamCase;
		QVERIFY2(allocationFailureIsReported([&out, &streamCase] {
			const bool added = videotest::addStream(out, streamCase, AV_CODEC_ID_MPEG4, videotest::setupVideo);
			return !added && streamCase.stream != nullptr && streamCase.codec == nullptr;
		}, failCodecAfterStream), "codec allocation failure was not reported after creating its stream");
		avformat_free_context(out);
		videotest::Encoder frameCase;
		frameCase.codec = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_MPEG4));
		QVERIFY(frameCase.codec != nullptr);
		videotest::setupVideo(frameCase.codec);
		QVERIFY2(allocationFailureIsReported([&frameCase] { return !videotest::allocateFrame(frameCase); },
			failFrameAllocation),
			"allocateFrame dereferenced the failed av_frame_alloc");
#elif defined(ZENE_HAVE_FFMPEG)
		QSKIP("FFmpeg allocation failure hooks require Linux or macOS");
#else
		QSKIP("this build has no FFmpeg");
#endif
	}
};

int main(int argc, char** argv)
{
	QApplication::setAttribute(Qt::AA_Use96Dpi, true);
	VideoTrackTestApplication app(argc, argv);
	VideoTrackTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "VideoTrackTest.moc"
