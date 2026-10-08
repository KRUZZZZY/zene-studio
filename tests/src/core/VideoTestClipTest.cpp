/*
 * VideoTestClipTest.cpp - the libavcodec encoder behind the test video makes the clip VideoTrackTest expects
 *
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

/*! tests/src/core/VideoTestClip.h makes the two-second fixture VideoTrackTest reads: with the ffmpeg program
 *  when there is one, otherwise encoded in-process with libavcodec. On a machine WITH the ffmpeg program the
 *  in-process encoder never runs, so a defect in it stays hidden until the day the program is missing. This test
 *  runs it directly and reads the file back with libavformat/libavcodec: the streams are the two the fixture
 *  promises (video 160x90 at 25 fps, stereo 44.1 kHz audio), it lasts two seconds, and the first frame is red
 *  while the last is blue. */

#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "VideoTestClip.h"

#ifdef ZENE_HAVE_FFMPEG

namespace
{

//! The luma of the first and the last decoded video frame, and how many frames came out.
struct Decoded
{
	int firstLuma = -1;
	int lastLuma = -1;
	int frames = 0;
};

Decoded decodeVideo(AVFormatContext* input, int streamIndex)
{
	Decoded result;
	const AVStream* stream = input->streams[streamIndex];
	const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
	AVCodecContext* context = codec != nullptr ? avcodec_alloc_context3(codec) : nullptr;
	if (context == nullptr) { return result; }
	avcodec_parameters_to_context(context, stream->codecpar);
	if (avcodec_open2(context, codec, nullptr) < 0)
	{
		avcodec_free_context(&context);
		return result;
	}
	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	const auto drain = [&] {
		while (avcodec_receive_frame(context, frame) == 0)
		{
			const int luma = frame->data[0][(frame->height / 2) * frame->linesize[0] + frame->width / 2];
			if (result.frames == 0) { result.firstLuma = luma; }
			result.lastLuma = luma;
			++result.frames;
		}
	};
	while (av_read_frame(input, packet) >= 0)
	{
		if (packet->stream_index == streamIndex && avcodec_send_packet(context, packet) >= 0) { drain(); }
		av_packet_unref(packet);
	}
	avcodec_send_packet(context, nullptr);
	drain();
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&context);
	return result;
}

} // namespace

#endif

class VideoTestClipTest : public QObject
{
	Q_OBJECT

private slots:
	//! The fixture can also be made by the ffmpeg program when one is installed: that path must agree with the
	//! in-process encoder on what the clip is (makeClip prefers the program), so a machine with and one without
	//! ffmpeg run the same test on the same video.
	void makeClipProducesTheSameShapeWhicheverEncoderMadeIt()
	{
#ifndef ZENE_HAVE_FFMPEG
		QSKIP("this build has no FFmpeg, so the test clip is never made");
#else
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = QDir(dir.path()).filePath(QStringLiteral("made.mkv"));
		QVERIFY2(videotest::makeClip(path), "makeClip produced no file");
		AVFormatContext* input = nullptr;
		const QByteArray name = QFile::encodeName(path);
		QVERIFY(avformat_open_input(&input, name.constData(), nullptr, nullptr) == 0);
		QVERIFY(avformat_find_stream_info(input, nullptr) >= 0);
		bool video = false;
		bool audio = false;
		for (unsigned i = 0; i < input->nb_streams; ++i)
		{
			const AVCodecParameters* parameters = input->streams[i]->codecpar;
			if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && parameters->width == videotest::Width && parameters->height == videotest::Height) { video = true; }
			if (parameters->codec_type == AVMEDIA_TYPE_AUDIO && parameters->sample_rate == videotest::Rate) { audio = true; }
		}
		const double seconds = static_cast<double>(input->duration) / AV_TIME_BASE;
		avformat_close_input(&input);
		QVERIFY2(video, "no 160x90 video stream");
		QVERIFY2(audio, "no 44.1 kHz audio stream");
		QVERIFY2(seconds > 1.8 && seconds < 2.4, qPrintable(QStringLiteral("duration %1 s").arg(seconds)));
#endif
	}

	void theLibavcodecEncoderMakesAReadableTwoSecondClip()
	{
#ifndef ZENE_HAVE_FFMPEG
		QSKIP("this build has no FFmpeg, so the test clip is never made");
#else
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = QDir(dir.path()).filePath(QStringLiteral("clip.mkv"));
		QVERIFY2(videotest::encodeWithLibavcodec(path), "the in-process encoder failed");
		QVERIFY(QFileInfo(path).size() > 0);

		AVFormatContext* input = nullptr;
		const QByteArray name = QFile::encodeName(path);
		QVERIFY(avformat_open_input(&input, name.constData(), nullptr, nullptr) == 0);
		QVERIFY(avformat_find_stream_info(input, nullptr) >= 0);
		QCOMPARE(static_cast<int>(input->nb_streams), 2);

		int video = -1;
		int audio = -1;
		for (unsigned i = 0; i < input->nb_streams; ++i)
		{
			const AVCodecParameters* parameters = input->streams[i]->codecpar;
			if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) { video = static_cast<int>(i); }
			if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) { audio = static_cast<int>(i); }
		}
		QVERIFY(video >= 0);
		QVERIFY(audio >= 0);
		QCOMPARE(input->streams[video]->codecpar->width, videotest::Width);
		QCOMPARE(input->streams[video]->codecpar->height, videotest::Height);
		QCOMPARE(static_cast<int>(input->streams[video]->codecpar->codec_id), static_cast<int>(AV_CODEC_ID_MPEG4));
		QCOMPARE(input->streams[audio]->codecpar->sample_rate, videotest::Rate);
		QCOMPARE(input->streams[audio]->codecpar->ch_layout.nb_channels, 2);
		const double seconds = static_cast<double>(input->duration) / AV_TIME_BASE;
		QVERIFY2(seconds > 1.8 && seconds < 2.4, qPrintable(QStringLiteral("duration %1 s").arg(seconds)));

		const Decoded decoded = decodeVideo(input, video);
		avformat_close_input(&input);
		QCOMPARE(decoded.frames, videotest::Frames);
		QVERIFY2(qAbs(decoded.firstLuma - 81) < 12, qPrintable(QStringLiteral("first frame luma %1 (red is 81)").arg(decoded.firstLuma)));
		QVERIFY2(qAbs(decoded.lastLuma - 41) < 12, qPrintable(QStringLiteral("last frame luma %1 (blue is 41)").arg(decoded.lastLuma)));
#endif
	}

	void anUnwritablePathFailsInsteadOfProducingAFile()
	{
#ifndef ZENE_HAVE_FFMPEG
		QSKIP("this build has no FFmpeg, so the test clip is never made");
#else
		QVERIFY(!videotest::encodeWithLibavcodec(QStringLiteral("/nonexistent-dir-for-coverage-test/clip.mkv")));
		QVERIFY(!QFileInfo::exists(QStringLiteral("/nonexistent-dir-for-coverage-test/clip.mkv")));
#endif
	}
};

QTEST_GUILESS_MAIN(VideoTestClipTest)
#include "VideoTestClipTest.moc"
