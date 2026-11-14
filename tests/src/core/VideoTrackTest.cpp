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

//! The packet or frame allocation of this executable's own code that fails: the seam for the allocation-failure cases.
//! Calls made inside FFmpeg's shared libraries (stream probing, for one) never count, so they cannot consume the fault.
enum class OwnAllocation : int { None, Frame, Packet };
std::atomic<OwnAllocation> targetAllocation{OwnAllocation::None};
std::atomic_int targetOrdinal{0};  //!< the 1-based call of the targeted kind that fails
std::atomic_int targetCalls{0};    //!< such calls seen since the target was armed (only counted in the child)

//! True when @a caller is code of this executable (the product is linked into it), not of a shared library.
bool calledFromThisExecutable(const void* caller)
{
	const void* self = reinterpret_cast<const void*>(&calledFromThisExecutable);
	Dl_info callerInfo{};
	Dl_info selfInfo{};
	return dladdr(caller, &callerInfo) != 0 && dladdr(self, &selfInfo) != 0 && callerInfo.dli_fbase == selfInfo.dli_fbase;
}

//! True when this call of @a kind from @a caller is the one the armed target says to fail.
bool failsTargetedCall(OwnAllocation kind, const void* caller)
{
	if (targetAllocation.load(std::memory_order_relaxed) != kind || !calledFromThisExecutable(caller)) { return false; }
	return targetCalls.fetch_add(1, std::memory_order_relaxed) + 1 == targetOrdinal.load(std::memory_order_relaxed);
}

//! Run @a checkFailure in a forked child after @a arm has set the failure; crashes stay inside the child.
bool failureIsReportedInChild(const std::function<bool()>& checkFailure, const std::function<void()>& arm)
{
	const pid_t child = fork();
	if (child < 0) { return false; }
	if (child == 0)
	{
		arm();
		_exit(checkFailure() ? 0 : 1);
	}
	int status = 0;
	if (waitpid(child, &status, 0) < 0) { return false; }
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

//! Run @a checkFailure with every FFmpeg allocation of the persistent @a fail flag forced to fail.
bool allocationFailureIsReported(const std::function<bool()>& checkFailure, std::atomic_bool& fail)
{
	return failureIsReportedInChild(checkFailure, [&fail] { fail.store(true, std::memory_order_relaxed); });
}

//! Run @a checkFailure with this executable's @a ordinal-th @a kind allocation forced to fail.
bool ownAllocationFailureIsReported(const std::function<bool()>& checkFailure, OwnAllocation kind, int ordinal)
{
	return failureIsReportedInChild(checkFailure, [kind, ordinal] {
		targetOrdinal.store(ordinal, std::memory_order_relaxed);
		targetCalls.store(0, std::memory_order_relaxed);
		targetAllocation.store(kind, std::memory_order_relaxed);
	});
}

} // namespace

//! At file scope so FFmpeg calls from VideoTestClip resolve to these test-only failure hooks.
#pragma GCC visibility push(default)
extern "C" AVFrame* av_frame_alloc()
{
	if (failFrameAllocation.load(std::memory_order_relaxed)) { return nullptr; }
	if (failsTargetedCall(OwnAllocation::Frame, __builtin_return_address(0))) { return nullptr; }
	static const auto real = reinterpret_cast<decltype(&av_frame_alloc)>(dlsym(RTLD_NEXT, "av_frame_alloc"));
	return real == nullptr ? nullptr : real();
}

extern "C" AVPacket* av_packet_alloc()
{
	if (failsTargetedCall(OwnAllocation::Packet, __builtin_return_address(0))) { return nullptr; }
	static const auto real = reinterpret_cast<decltype(&av_packet_alloc)>(dlsym(RTLD_NEXT, "av_packet_alloc"));
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

#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
	//! The decoder's open() of the test clip fails with an out-of-memory reason and stays closed.
	bool decoderOpenReportsOutOfMemory() const
	{
		VideoDecoder decoder;
		QString why;
		return !decoder.open(m_clip, &why) && why.contains(QStringLiteral("out of memory")) && !decoder.isOpen();
	}

	//! The audio extraction of the test clip fails with an out-of-memory reason and writes no WAV.
	bool extractionReportsOutOfMemory() const
	{
		const QString wav = m_dir.filePath(QStringLiteral("allocation-failure.wav"));
		VideoAudio audio;
		QString error;
		return !extractVideoAudio(m_clip, wav, &audio, &error) && error.contains(QStringLiteral("out of memory"))
			&& !QFileInfo::exists(wav);
	}
#endif

private slots:
	void initTestCase();

	void cleanupTestCase();

	void timecodeIsHoursMinutesSecondsFrames();

	void invalidSavedDurationIsReprobed();

	void negativeSubFrameTimecodeKeepsItsSign();

	void withoutFfmpegEveryCommandIsUnavailable();

	void failedOpenAcceptsNullErrorOutput();

	void attachReadsTheFileAndEachEditIsOneUndoStep();

	void junkIsRefusedTyped();

	void theVideoRoundTripsThroughTheProjectFile();

	void theDecoderLandsOnTheRightSecondForwardAndBack();

	void anUnrepresentableSeekKeepsDecoderProgress();

	void thePumpAnswersOnTheInterfaceThread();

	void thePumpRetriesAPathAfterItsFirstOpenFails();

	void theArrangerLaneClosesItsDecoderOnDetach();

	void theWindowFollowsTheTransport();

	//! Q050-021: a stopped seek must set the same position the arranger reads.
	void aStoppedSeekSynchronizesTransportAndArranger();

	//! Q050-021: the first frame requested after playback begins is the sought video frame.
	void theFirstFrameAfterPlaybackUsesTheStoppedSeek();

	void extractAudioLandsTheSoundOnANewTrackInOneStep();

	void aPartialWavWriteFailsAndRemovesTheFile();

	void cancelledDirectExtractionPreservesExistingDestination();

	//! 0.5.0 theme 24: with 'background' the decode runs on a worker listed in the task area and the reply comes at once;
	//! the track lands when it completes, as the same ONE undo step.
	void extractAudioInTheBackgroundLandsWhenItCompletes();

	//! A completed background decode releases its application shutdown callback.
	void completedBackgroundExtractionDisconnectsItsShutdownHook();

	//! A background extraction cancelled from the task area adds nothing and leaves no WAV behind.
	void aCancelledBackgroundExtractionAddsNothing();

	//! A resampler error after audio was already converted fails the extraction and publishes no WAV
	//! (a negative swr_convert result used to count as an empty success and commit a truncated file).
	void aResamplerErrorFailsTheExtractionAndPublishesNoWav();

	//! A failed FFmpeg allocation is reported, not dereferenced.
	void failedAllocationsAreReportedNotCrashed();

	//! A failed shown-frame allocation in the decoder's open() is reported with a reason, never dereferenced.
	void decoderFrameAllocationFailureIsReportedNotDereferenced();

	//! A failed pending-frame allocation in the decoder's open(), after the shown frame exists, is reported.
	void decoderPendingFrameAllocationFailureIsReportedNotDereferenced();

	//! A failed packet allocation in the decoder's open(), after both frames exist, is reported with a reason.
	void decoderPacketAllocationFailureIsReportedNotDereferenced();

	//! A failed frame allocation in the audio extraction is reported and leaves no WAV behind.
	void audioExtractFrameAllocationFailureIsReportedNotDereferenced();

	//! A failed packet allocation in the audio extraction is reported and leaves no WAV behind.
	void audioExtractPacketAllocationFailureIsReportedNotDereferenced();
};

#include "VideoTrackTestParts1.h"
#include "VideoTrackTestParts2.h"

int main(int argc, char** argv)
{
	QApplication::setAttribute(Qt::AA_Use96Dpi, true);
	VideoTrackTestApplication app(argc, argv);
	VideoTrackTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "VideoTrackTest.moc"
