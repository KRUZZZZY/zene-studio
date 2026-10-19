/*
 * VideoFfmpegTest.cpp - a rejected FFmpeg seek must not restart the stream
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

/*! The FFmpeg stream behind the video track: a seek the demuxer refuses must not be taken for a seek that
 *  worked. The clip is made at test time (VideoTestClip.h) in the AVI container, whose demuxer reports a
 *  refused seek instead of falling back to a forward scan, and the source is then made unseekable through
 *  the public AVIOContext, so the seek really fails. The stream must keep its state - the decoder is not
 *  flushed and the demuxer is not restarted - while a seek that does work still restarts it. */

#include <QtTest>

#include <QTemporaryDir>

#include "VideoFfmpeg.h"
#include "VideoTestClip.h"

#ifdef ZENE_HAVE_FFMPEG
namespace
{

//! Read the clip to its end: the stream is draining and the decoder is flushed.
void readToEnd(lmms::video::MediaStream& media)
{
	AVFrame* frame = av_frame_alloc();
	AVPacket* packet = av_packet_alloc();
	if (frame != nullptr && packet != nullptr)
	{
		while (media.receive(frame, packet)) { av_frame_unref(frame); }
	}
	av_frame_free(&frame);
	av_packet_free(&packet);
}

} // namespace
#endif

class VideoFfmpegTest : public QObject
{
	Q_OBJECT

private slots:
	void failedSeekDoesNotRestartTheStream()
	{
#ifdef ZENE_HAVE_FFMPEG
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("clip.avi"));
		QVERIFY(videotest::makeClip(path));

		lmms::video::MediaStream media;
		QString error;
		QVERIFY2(media.open(path, AVMEDIA_TYPE_VIDEO, &error), qPrintable(error));
		readToEnd(media);
		QVERIFY(media.draining);

		// The source cannot seek any more: the public AVIOContext loses its seek, so the demuxer's
		// seek fails - the AVI demuxer reports that, where the other demuxers scan forward instead.
		QVERIFY(media.format->pb != nullptr);
		media.format->pb->seek = nullptr;
		QVERIFY2(av_seek_frame(media.format, media.index, media.startPts(), AVSEEK_FLAG_BACKWARD) < 0,
			"the demuxer must report the refused seek");

		media.seek(1.0);

		QVERIFY2(media.draining, "a refused seek must not flush the decoder nor restart the stream");
#else
		QSKIP("this build has no FFmpeg");
#endif
	}

	void successfulSeekRestartsTheStream()
	{
#ifdef ZENE_HAVE_FFMPEG
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("clip.avi"));
		QVERIFY(videotest::makeClip(path));

		lmms::video::MediaStream media;
		QString error;
		QVERIFY2(media.open(path, AVMEDIA_TYPE_VIDEO, &error), qPrintable(error));
		readToEnd(media);
		QVERIFY(media.draining);

		QVERIFY2(av_seek_frame(media.format, media.index, media.startPts(), AVSEEK_FLAG_BACKWARD) >= 0,
			"the seekable source must accept the seek");

		media.seek(1.0);

		QVERIFY2(!media.draining, "a seek that worked must restart the stream");
#else
		QSKIP("this build has no FFmpeg");
#endif
	}
};

QTEST_GUILESS_MAIN(VideoFfmpegTest)

#include "VideoFfmpegTest.moc"
