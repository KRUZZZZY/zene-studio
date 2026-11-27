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

// Complete fixture method definitions; included once after the fixture declaration.

void VideoTrackTest::aCancelledBackgroundExtractionAddsNothing() {
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
void VideoTrackTest::aResamplerErrorFailsTheExtractionAndPublishesNoWav() {
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
void VideoTrackTest::failedAllocationsAreReportedNotCrashed() {
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
void VideoTrackTest::decoderFrameAllocationFailureIsReportedNotDereferenced() {
#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
	if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
	QVERIFY2(ownAllocationFailureIsReported([this] { return decoderOpenReportsOutOfMemory(); }, OwnAllocation::Frame, 1),
		"VideoDecoder::open did not report a failed shown-frame av_frame_alloc");
#elif defined(ZENE_HAVE_FFMPEG)
	QSKIP("FFmpeg allocation failure hooks require Linux or macOS");
#else
	QSKIP("this build has no FFmpeg");
#endif
}
void VideoTrackTest::decoderPendingFrameAllocationFailureIsReportedNotDereferenced() {
#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
	if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
	QVERIFY2(ownAllocationFailureIsReported([this] { return decoderOpenReportsOutOfMemory(); }, OwnAllocation::Frame, 2),
		"VideoDecoder::open did not report a failed pending-frame av_frame_alloc");
#elif defined(ZENE_HAVE_FFMPEG)
	QSKIP("FFmpeg allocation failure hooks require Linux or macOS");
#else
	QSKIP("this build has no FFmpeg");
#endif
}
void VideoTrackTest::decoderPacketAllocationFailureIsReportedNotDereferenced() {
#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
	if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
	QVERIFY2(ownAllocationFailureIsReported([this] { return decoderOpenReportsOutOfMemory(); }, OwnAllocation::Packet, 1),
		"VideoDecoder::open did not report a failed av_packet_alloc");
#elif defined(ZENE_HAVE_FFMPEG)
	QSKIP("FFmpeg allocation failure hooks require Linux or macOS");
#else
	QSKIP("this build has no FFmpeg");
#endif
}
void VideoTrackTest::audioExtractFrameAllocationFailureIsReportedNotDereferenced() {
#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
	if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
	QVERIFY2(ownAllocationFailureIsReported([this] { return extractionReportsOutOfMemory(); }, OwnAllocation::Frame, 1),
		"the audio extraction did not report a failed av_frame_alloc");
#elif defined(ZENE_HAVE_FFMPEG)
	QSKIP("FFmpeg allocation failure hooks require Linux or macOS");
#else
	QSKIP("this build has no FFmpeg");
#endif
}
void VideoTrackTest::audioExtractPacketAllocationFailureIsReportedNotDereferenced() {
#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS
	if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
	QVERIFY2(ownAllocationFailureIsReported([this] { return extractionReportsOutOfMemory(); }, OwnAllocation::Packet, 1),
		"the audio extraction did not report a failed av_packet_alloc");
#elif defined(ZENE_HAVE_FFMPEG)
	QSKIP("FFmpeg allocation failure hooks require Linux or macOS");
#else
	QSKIP("this build has no FFmpeg");
#endif
}
//! cluster new-p8-66: extraction over the video it reads would replace that file when the WAV commits. Refused for
//! every spelling of one file - the same path, a "./" segment, a symlink, a hard link - and on the worker as well.
void VideoTrackTest::refuseExtractionOverItsOwnSource(const QString& copy, const QString& out, bool background) {
	Song* song = Engine::getSong();
	song->clearProject();
	QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), copy}}).ok);
	const auto tracksBefore = song->tracks().size();
	QFile source(copy);
	QVERIFY(source.open(QIODevice::ReadOnly));
	const QByteArray bytes = source.readAll();
	source.close();

	const ControlResult answered = run(QStringLiteral("video.extract_audio"),
		{{QStringLiteral("out"), out}, {QStringLiteral("background"), background}});
	if (!background)
	{
		QCOMPARE(answered.errorKind, ControlErrorKind::Refused);
		QVERIFY2(answered.errorMessage.contains(QStringLiteral("its own source")), qPrintable(answered.errorMessage));
	}
	else
	{
		QVERIFY2(answered.ok, qPrintable(answered.errorMessage));  // 'background' acknowledges; the task reports
		const int task = answered.result.value(QStringLiteral("task")).toInt(-1);
		QVERIFY(task > 0);
		const auto stateOf = [task] { return taskState(task); };
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(), TaskState::Failed, 20000);
		QVERIFY2(taskStatus(task).contains(QStringLiteral("its own source")), qPrintable(taskStatus(task)));
	}

	QFile kept(copy);
	QVERIFY(kept.open(QIODevice::ReadOnly));
	QVERIFY2(kept.readAll() == bytes, "the extraction overwrote the video it was reading");
	QCOMPARE(song->tracks().size(), tracksBefore);  // and no track landed from the command path
	QVERIFY2(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), copy}}).ok,
		"the source is no longer a readable video");
	song->clearProject();
}
void VideoTrackTest::extractAudioRefusesItsOwnSource() {
	if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
	const QString copy = m_dir.filePath(QStringLiteral("alias-same.mkv"));
	QFile::remove(copy);
	QVERIFY(QFile::copy(m_clip, copy));  // a run without the fix damages this copy, never the shared test clip
	refuseExtractionOverItsOwnSource(copy, copy, false);
	refuseExtractionOverItsOwnSource(copy, copy, true);
}
void VideoTrackTest::extractAudioRefusesADotSegmentSpellingOfItsOwnSource() {
	if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
	const QString copy = m_dir.filePath(QStringLiteral("alias-dot.mkv"));
	QFile::remove(copy);
	QVERIFY(QFile::copy(m_clip, copy));
	const QString spelled = m_dir.path() + QStringLiteral("/./alias-dot.mkv");
	refuseExtractionOverItsOwnSource(copy, spelled, false);
	refuseExtractionOverItsOwnSource(copy, spelled, true);
}
void VideoTrackTest::extractAudioRefusesASymlinkToItsOwnSource() {
	if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
#if defined(Q_OS_UNIX)
	const QString copy = m_dir.filePath(QStringLiteral("alias-symlink.mkv"));
	const QString link = m_dir.filePath(QStringLiteral("alias-symlink-link.mkv"));
	QFile::remove(copy);
	QFile::remove(link);
	QVERIFY(QFile::copy(m_clip, copy));
	QVERIFY2(QFile::link(copy, link), "could not make a symbolic link on this platform");
	refuseExtractionOverItsOwnSource(copy, link, false);
	refuseExtractionOverItsOwnSource(copy, link, true);
#else
	QSKIP("a symbolic link needs a POSIX file system");
#endif
}
void VideoTrackTest::extractAudioRefusesAHardLinkToItsOwnSource() {
	if (!needClip()) { QSKIP("no FFmpeg in this build, or the test clip could not be made"); }
#if defined(Q_OS_UNIX)
	const QString copy = m_dir.filePath(QStringLiteral("alias-hard.mkv"));
	const QString hard = m_dir.filePath(QStringLiteral("alias-hard-link.mkv"));
	QFile::remove(copy);
	QFile::remove(hard);
	QVERIFY(QFile::copy(m_clip, copy));
	QVERIFY2(::link(QFile::encodeName(copy).constData(), QFile::encodeName(hard).constData()) == 0,
		"could not make a hard link on this file system");
	refuseExtractionOverItsOwnSource(copy, hard, false);
	refuseExtractionOverItsOwnSource(copy, hard, true);
#else
	QSKIP("a hard link needs a POSIX file system");
#endif
}
