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
